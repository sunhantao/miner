/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2017-2019 XMR-Stak    <https://github.com/fireice-uk>, <https://github.com/psychocrypt>
 * Copyright 2018      Lee Clagett <https://github.com/vtnerd>
 * Copyright 2018-2019 SChernykh   <https://github.com/SChernykh>
 * Copyright 2016-2019 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef XMRIG_CRYPTONIGHT_X86_H
#define XMRIG_CRYPTONIGHT_X86_H


#ifdef __GNUC__
#   include <x86intrin.h>
#else
#   include <intrin.h>
#   define __restrict__ __restrict
#endif


#include "backend/cpu/Cpu.h"
#include "crypto/cn/CnAlgo.h"
#include "crypto/cn/CryptoNight_monero.h"
#include "crypto/cn/CryptoNight.h"
#include "crypto/cn/soft_aes.h"
#include "crypto/common/keccak.h"


extern "C"
{
#include "crypto/cn/c_groestl.h"
#include "crypto/cn/c_blake256.h"
#include "crypto/cn/c_jh.h"
#include "crypto/cn/c_skein.h"
}


static inline void do_blake_hash(const uint8_t *input, size_t len, uint8_t *output) {
    blake256_hash(output, input, len);
}


static inline void do_groestl_hash(const uint8_t *input, size_t len, uint8_t *output) {
    groestl(input, len * 8, output);
}


static inline void do_jh_hash(const uint8_t *input, size_t len, uint8_t *output) {
    jh_hash(32 * 8, input, 8 * len, output);
}


static inline void do_skein_hash(const uint8_t *input, size_t len, uint8_t *output) {
    xmr_skein(input, output);
}


void (* const extra_hashes[4])(const uint8_t *, size_t, uint8_t *) = {do_blake_hash, do_groestl_hash, do_jh_hash, do_skein_hash};


#if defined(__x86_64__) || defined(_M_AMD64)
#   ifdef __GNUC__
static inline uint64_t __umul128(uint64_t a, uint64_t b, uint64_t* hi)
{
    unsigned __int128 r = (unsigned __int128) a * (unsigned __int128) b;
    *hi = r >> 64;
    return (uint64_t) r;
}
#   else
    #define __umul128 _umul128
#   endif
#elif defined(__i386__) || defined(_M_IX86)
static inline int64_t _mm_cvtsi128_si64(__m128i a)
{
    return ((uint64_t)(uint32_t)_mm_cvtsi128_si32(a) | ((uint64_t)(uint32_t)_mm_cvtsi128_si32(_mm_srli_si128(a, 4)) << 32));
}

static inline __m128i _mm_cvtsi64_si128(int64_t a) {
    return _mm_set_epi64x(0, a);
}

static inline uint64_t __umul128(uint64_t multiplier, uint64_t multiplicand, uint64_t *product_hi) {
    // multiplier   = ab = a * 2^32 + b
    // multiplicand = cd = c * 2^32 + d
    // ab * cd = a * c * 2^64 + (a * d + b * c) * 2^32 + b * d
    uint64_t a = multiplier >> 32;
    uint64_t b = multiplier & 0xFFFFFFFF;
    uint64_t c = multiplicand >> 32;
    uint64_t d = multiplicand & 0xFFFFFFFF;

    //uint64_t ac = a * c;
    uint64_t ad = a * d;
    //uint64_t bc = b * c;
    uint64_t bd = b * d;

    uint64_t adbc = ad + (b * c);
    uint64_t adbc_carry = adbc < ad ? 1 : 0;

    // multiplier * multiplicand = product_hi * 2^64 + product_lo
    uint64_t product_lo = bd + (adbc << 32);
    uint64_t product_lo_carry = product_lo < bd ? 1 : 0;
    *product_hi = (a * c) + (adbc >> 32) + (adbc_carry << 32) + product_lo_carry;

    return product_lo;
}
#endif


// This will shift and xor tmp1 into itself as 4 32-bit vals such as
// sl_xor(a1 a2 a3 a4) = a1 (a2^a1) (a3^a2^a1) (a4^a3^a2^a1)
static inline __m128i sl_xor(__m128i tmp1)
{
    __m128i tmp4;
    tmp4 = _mm_slli_si128(tmp1, 0x04);
    tmp1 = _mm_xor_si128(tmp1, tmp4);
    tmp4 = _mm_slli_si128(tmp4, 0x04);
    tmp1 = _mm_xor_si128(tmp1, tmp4);
    tmp4 = _mm_slli_si128(tmp4, 0x04);
    tmp1 = _mm_xor_si128(tmp1, tmp4);
    return tmp1;
}


template<uint8_t rcon>
static inline void aes_genkey_sub(__m128i* xout0, __m128i* xout2)
{
    __m128i xout1 = _mm_aeskeygenassist_si128(*xout2, rcon);
    xout1  = _mm_shuffle_epi32(xout1, 0xFF); // see PSHUFD, set all elems to 4th elem
    *xout0 = sl_xor(*xout0);
    *xout0 = _mm_xor_si128(*xout0, xout1);
    xout1  = _mm_aeskeygenassist_si128(*xout0, 0x00);
    xout1  = _mm_shuffle_epi32(xout1, 0xAA); // see PSHUFD, set all elems to 3rd elem
    *xout2 = sl_xor(*xout2);
    *xout2 = _mm_xor_si128(*xout2, xout1);
}


template<uint8_t rcon>
static inline void soft_aes_genkey_sub(__m128i* xout0, __m128i* xout2)
{
    __m128i xout1 = soft_aeskeygenassist<rcon>(*xout2);
    xout1  = _mm_shuffle_epi32(xout1, 0xFF); // see PSHUFD, set all elems to 4th elem
    *xout0 = sl_xor(*xout0);
    *xout0 = _mm_xor_si128(*xout0, xout1);
    xout1  = soft_aeskeygenassist<0x00>(*xout0);
    xout1  = _mm_shuffle_epi32(xout1, 0xAA); // see PSHUFD, set all elems to 3rd elem
    *xout2 = sl_xor(*xout2);
    *xout2 = _mm_xor_si128(*xout2, xout1);
}


template<bool SOFT_AES>
static inline void aes_genkey(const __m128i* memory, __m128i* k0, __m128i* k1, __m128i* k2, __m128i* k3, __m128i* k4, __m128i* k5, __m128i* k6, __m128i* k7, __m128i* k8, __m128i* k9)
{
    __m128i xout0 = _mm_load_si128(memory);
    __m128i xout2 = _mm_load_si128(memory + 1);
    *k0 = xout0;
    *k1 = xout2;

    SOFT_AES ? soft_aes_genkey_sub<0x01>(&xout0, &xout2) : aes_genkey_sub<0x01>(&xout0, &xout2);
    *k2 = xout0;
    *k3 = xout2;

    SOFT_AES ? soft_aes_genkey_sub<0x02>(&xout0, &xout2) : aes_genkey_sub<0x02>(&xout0, &xout2);
    *k4 = xout0;
    *k5 = xout2;

    SOFT_AES ? soft_aes_genkey_sub<0x04>(&xout0, &xout2) : aes_genkey_sub<0x04>(&xout0, &xout2);
    *k6 = xout0;
    *k7 = xout2;

    SOFT_AES ? soft_aes_genkey_sub<0x08>(&xout0, &xout2) : aes_genkey_sub<0x08>(&xout0, &xout2);
    *k8 = xout0;
    *k9 = xout2;
}


static FORCEINLINE void soft_aesenc(void* __restrict ptr, const void* __restrict key, const uint32_t* __restrict t)
{
    uint32_t x0 = ((const uint32_t*)(ptr))[0];
    uint32_t x1 = ((const uint32_t*)(ptr))[1];
    uint32_t x2 = ((const uint32_t*)(ptr))[2];
    uint32_t x3 = ((const uint32_t*)(ptr))[3];

    uint32_t y0 = t[x0 & 0xff]; x0 >>= 8;
    uint32_t y1 = t[x1 & 0xff]; x1 >>= 8;
    uint32_t y2 = t[x2 & 0xff]; x2 >>= 8;
    uint32_t y3 = t[x3 & 0xff]; x3 >>= 8;
    t += 256;

    y0 ^= t[x1 & 0xff]; x1 >>= 8;
    y1 ^= t[x2 & 0xff]; x2 >>= 8;
    y2 ^= t[x3 & 0xff]; x3 >>= 8;
    y3 ^= t[x0 & 0xff]; x0 >>= 8;
    t += 256;

    y0 ^= t[x2 & 0xff]; x2 >>= 8;
    y1 ^= t[x3 & 0xff]; x3 >>= 8;
    y2 ^= t[x0 & 0xff]; x0 >>= 8;
    y3 ^= t[x1 & 0xff]; x1 >>= 8;
    t += 256;

    y0 ^= t[x3];
    y1 ^= t[x0];
    y2 ^= t[x1];
    y3 ^= t[x2];

    ((uint32_t*)ptr)[0] = y0 ^ ((uint32_t*)key)[0];
    ((uint32_t*)ptr)[1] = y1 ^ ((uint32_t*)key)[1];
    ((uint32_t*)ptr)[2] = y2 ^ ((uint32_t*)key)[2];
    ((uint32_t*)ptr)[3] = y3 ^ ((uint32_t*)key)[3];
}

static FORCEINLINE __m128i soft_aesenc(const void* __restrict ptr, const __m128i key, const uint32_t* __restrict t)
{
    uint32_t x0 = ((const uint32_t*)(ptr))[0];
    uint32_t x1 = ((const uint32_t*)(ptr))[1];
    uint32_t x2 = ((const uint32_t*)(ptr))[2];
    uint32_t x3 = ((const uint32_t*)(ptr))[3];

    uint32_t y0 = t[x0 & 0xff]; x0 >>= 8;
    uint32_t y1 = t[x1 & 0xff]; x1 >>= 8;
    uint32_t y2 = t[x2 & 0xff]; x2 >>= 8;
    uint32_t y3 = t[x3 & 0xff]; x3 >>= 8;
    t += 256;

    y0 ^= t[x1 & 0xff]; x1 >>= 8;
    y1 ^= t[x2 & 0xff]; x2 >>= 8;
    y2 ^= t[x3 & 0xff]; x3 >>= 8;
    y3 ^= t[x0 & 0xff]; x0 >>= 8;
    t += 256;

    y0 ^= t[x2 & 0xff]; x2 >>= 8;
    y1 ^= t[x3 & 0xff]; x3 >>= 8;
    y2 ^= t[x0 & 0xff]; x0 >>= 8;
    y3 ^= t[x1 & 0xff]; x1 >>= 8;

    y0 ^= t[x3 + 256];
    y1 ^= t[x0 + 256];
    y2 ^= t[x1 + 256];
    y3 ^= t[x2 + 256];

    return _mm_xor_si128(_mm_set_epi32(y3, y2, y1, y0), key);
}

template<bool SOFT_AES>
void aes_round(__m128i key, __m128i* x0, __m128i* x1, __m128i* x2, __m128i* x3, __m128i* x4, __m128i* x5, __m128i* x6, __m128i* x7);

template<>
NOINLINE void aes_round<true>(__m128i key, __m128i* x0, __m128i* x1, __m128i* x2, __m128i* x3, __m128i* x4, __m128i* x5, __m128i* x6, __m128i* x7)
{
    *x0 = soft_aesenc((uint32_t*)x0, key, (const uint32_t*)saes_table);
    *x1 = soft_aesenc((uint32_t*)x1, key, (const uint32_t*)saes_table);
    *x2 = soft_aesenc((uint32_t*)x2, key, (const uint32_t*)saes_table);
    *x3 = soft_aesenc((uint32_t*)x3, key, (const uint32_t*)saes_table);
    *x4 = soft_aesenc((uint32_t*)x4, key, (const uint32_t*)saes_table);
    *x5 = soft_aesenc((uint32_t*)x5, key, (const uint32_t*)saes_table);
    *x6 = soft_aesenc((uint32_t*)x6, key, (const uint32_t*)saes_table);
    *x7 = soft_aesenc((uint32_t*)x7, key, (const uint32_t*)saes_table);
}

template<>
FORCEINLINE void aes_round<false>(__m128i key, __m128i* x0, __m128i* x1, __m128i* x2, __m128i* x3, __m128i* x4, __m128i* x5, __m128i* x6, __m128i* x7)
{
    *x0 = _mm_aesenc_si128(*x0, key);
    *x1 = _mm_aesenc_si128(*x1, key);
    *x2 = _mm_aesenc_si128(*x2, key);
    *x3 = _mm_aesenc_si128(*x3, key);
    *x4 = _mm_aesenc_si128(*x4, key);
    *x5 = _mm_aesenc_si128(*x5, key);
    *x6 = _mm_aesenc_si128(*x6, key);
    *x7 = _mm_aesenc_si128(*x7, key);
}

inline void mix_and_propagate(__m128i& x0, __m128i& x1, __m128i& x2, __m128i& x3, __m128i& x4, __m128i& x5, __m128i& x6, __m128i& x7)
{
    __m128i tmp0 = x0;
    x0 = _mm_xor_si128(x0, x1);
    x1 = _mm_xor_si128(x1, x2);
    x2 = _mm_xor_si128(x2, x3);
    x3 = _mm_xor_si128(x3, x4);
    x4 = _mm_xor_si128(x4, x5);
    x5 = _mm_xor_si128(x5, x6);
    x6 = _mm_xor_si128(x6, x7);
    x7 = _mm_xor_si128(x7, tmp0);
}


namespace xmrig {


template<Algorithm::Id ALGO, bool SOFT_AES>
static inline void cn_explode_scratchpad(const __m128i *input, __m128i *output)
{
    constexpr CnAlgo<ALGO> props;

    __m128i xin0, xin1, xin2, xin3, xin4, xin5, xin6, xin7;
    __m128i k0, k1, k2, k3, k4, k5, k6, k7, k8, k9;

    aes_genkey<SOFT_AES>(input, &k0, &k1, &k2, &k3, &k4, &k5, &k6, &k7, &k8, &k9);

    xin0 = _mm_load_si128(input + 4);
    xin1 = _mm_load_si128(input + 5);
    xin2 = _mm_load_si128(input + 6);
    xin3 = _mm_load_si128(input + 7);
    xin4 = _mm_load_si128(input + 8);
    xin5 = _mm_load_si128(input + 9);
    xin6 = _mm_load_si128(input + 10);
    xin7 = _mm_load_si128(input + 11);

    if (props.isHeavy()) {
        for (size_t i = 0; i < 16; i++) {
            aes_round<SOFT_AES>(k0, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k1, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k2, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k3, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k4, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k5, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k6, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k7, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k8, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
            aes_round<SOFT_AES>(k9, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);

            mix_and_propagate(xin0, xin1, xin2, xin3, xin4, xin5, xin6, xin7);
        }
    }

    for (size_t i = 0; i < props.memory() / sizeof(__m128i); i += 8) {
        aes_round<SOFT_AES>(k0, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k1, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k2, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k3, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k4, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k5, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k6, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k7, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k8, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);
        aes_round<SOFT_AES>(k9, &xin0, &xin1, &xin2, &xin3, &xin4, &xin5, &xin6, &xin7);

        _mm_store_si128(output + i + 0, xin0);
        _mm_store_si128(output + i + 1, xin1);
        _mm_store_si128(output + i + 2, xin2);
        _mm_store_si128(output + i + 3, xin3);
        _mm_store_si128(output + i + 4, xin4);
        _mm_store_si128(output + i + 5, xin5);
        _mm_store_si128(output + i + 6, xin6);
        _mm_store_si128(output + i + 7, xin7);
    }
}


template<Algorithm::Id ALGO, bool SOFT_AES>
static inline void cn_implode_scratchpad(const __m128i *input, __m128i *output)
{
    constexpr CnAlgo<ALGO> props;

#   ifdef XMRIG_ALGO_CN_GPU
    constexpr bool IS_HEAVY = props.isHeavy() || ALGO == Algorithm::CN_GPU;
#   else
    constexpr bool IS_HEAVY = props.isHeavy();
#   endif

    __m128i xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7;
    __m128i k0, k1, k2, k3, k4, k5, k6, k7, k8, k9;

    aes_genkey<SOFT_AES>(output + 2, &k0, &k1, &k2, &k3, &k4, &k5, &k6, &k7, &k8, &k9);

    xout0 = _mm_load_si128(output + 4);
    xout1 = _mm_load_si128(output + 5);
    xout2 = _mm_load_si128(output + 6);
    xout3 = _mm_load_si128(output + 7);
    xout4 = _mm_load_si128(output + 8);
    xout5 = _mm_load_si128(output + 9);
    xout6 = _mm_load_si128(output + 10);
    xout7 = _mm_load_si128(output + 11);

    for (size_t i = 0; i < props.memory() / sizeof(__m128i); i += 8) {
        xout0 = _mm_xor_si128(_mm_load_si128(input + i + 0), xout0);
        xout1 = _mm_xor_si128(_mm_load_si128(input + i + 1), xout1);
        xout2 = _mm_xor_si128(_mm_load_si128(input + i + 2), xout2);
        xout3 = _mm_xor_si128(_mm_load_si128(input + i + 3), xout3);
        xout4 = _mm_xor_si128(_mm_load_si128(input + i + 4), xout4);
        xout5 = _mm_xor_si128(_mm_load_si128(input + i + 5), xout5);
        xout6 = _mm_xor_si128(_mm_load_si128(input + i + 6), xout6);
        xout7 = _mm_xor_si128(_mm_load_si128(input + i + 7), xout7);

        aes_round<SOFT_AES>(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round<SOFT_AES>(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);

        if (IS_HEAVY) {
            mix_and_propagate(xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7);
        }
    }

    if (IS_HEAVY) {
        for (size_t i = 0; i < props.memory() / sizeof(__m128i); i += 8) {
            xout0 = _mm_xor_si128(_mm_load_si128(input + i + 0), xout0);
            xout1 = _mm_xor_si128(_mm_load_si128(input + i + 1), xout1);
            xout2 = _mm_xor_si128(_mm_load_si128(input + i + 2), xout2);
            xout3 = _mm_xor_si128(_mm_load_si128(input + i + 3), xout3);
            xout4 = _mm_xor_si128(_mm_load_si128(input + i + 4), xout4);
            xout5 = _mm_xor_si128(_mm_load_si128(input + i + 5), xout5);
            xout6 = _mm_xor_si128(_mm_load_si128(input + i + 6), xout6);
            xout7 = _mm_xor_si128(_mm_load_si128(input + i + 7), xout7);

            aes_round<SOFT_AES>(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);

            mix_and_propagate(xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7);
        }

        for (size_t i = 0; i < 16; i++) {
            aes_round<SOFT_AES>(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
            aes_round<SOFT_AES>(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);

            mix_and_propagate(xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7);
        }
    }

    _mm_store_si128(output + 4, xout0);
    _mm_store_si128(output + 5, xout1);
    _mm_store_si128(output + 6, xout2);
    _mm_store_si128(output + 7, xout3);
    _mm_store_si128(output + 8, xout4);
    _mm_store_si128(output + 9, xout5);
    _mm_store_si128(output + 10, xout6);
    _mm_store_si128(output + 11, xout7);
}


} /* namespace xmrig */


static inline __m128i aes_round_tweak_div(const __m128i &in, const __m128i &key)
{
    alignas(16) uint32_t k[4];
    alignas(16) uint32_t x[4];

    _mm_store_si128((__m128i*) k, key);
    _mm_store_si128((__m128i*) x, _mm_xor_si128(in, _mm_set_epi64x(0xffffffffffffffff, 0xffffffffffffffff)));

    #define BYTE(p, i) ((unsigned char*)&x[p])[i]
    k[0] ^= saes_table[0][BYTE(0, 0)] ^ saes_table[1][BYTE(1, 1)] ^ saes_table[2][BYTE(2, 2)] ^ saes_table[3][BYTE(3, 3)];
    x[0] ^= k[0];
    k[1] ^= saes_table[0][BYTE(1, 0)] ^ saes_table[1][BYTE(2, 1)] ^ saes_table[2][BYTE(3, 2)] ^ saes_table[3][BYTE(0, 3)];
    x[1] ^= k[1];
    k[2] ^= saes_table[0][BYTE(2, 0)] ^ saes_table[1][BYTE(3, 1)] ^ saes_table[2][BYTE(0, 2)] ^ saes_table[3][BYTE(1, 3)];
    x[2] ^= k[2];
    k[3] ^= saes_table[0][BYTE(3, 0)] ^ saes_table[1][BYTE(0, 1)] ^ saes_table[2][BYTE(1, 2)] ^ saes_table[3][BYTE(2, 3)];
    #undef BYTE

    return _mm_load_si128((__m128i*)k);
}


static inline __m128i int_sqrt_v2(const uint64_t n0)
{
    __m128d x = _mm_castsi128_pd(_mm_add_epi64(_mm_cvtsi64_si128(n0 >> 12), _mm_set_epi64x(0, 1023ULL << 52)));
    x = _mm_sqrt_sd(_mm_setzero_pd(), x);
    uint64_t r = static_cast<uint64_t>(_mm_cvtsi128_si64(_mm_castpd_si128(x)));

    const uint64_t s = r >> 20;
    r >>= 19;

    uint64_t x2 = (s - (1022ULL << 32)) * (r - s - (1022ULL << 32) + 1);
#   if (defined(_MSC_VER) || __GNUC__ > 7 || (__GNUC__ == 7 && __GNUC_MINOR__ > 1)) && (defined(__x86_64__) || defined(_M_AMD64))
    _addcarry_u64(_subborrow_u64(0, x2, n0, (unsigned long long int*)&x2), r, 0, (unsigned long long int*)&r);
#   else
    if (x2 < n0) ++r;
#   endif

    return _mm_cvtsi64_si128(r);
}


void wow_soft_aes_compile_code(const V4_Instruction *code, int code_size, void *machine_code, xmrig::Assembly ASM);
void v4_soft_aes_compile_code(const V4_Instruction *code, int code_size, void *machine_code, xmrig::Assembly ASM);


namespace xmrig {


template<Algorithm::Id ALGO>
static inline void cryptonight_monero_tweak(uint64_t *mem_out, const uint8_t *l, uint64_t idx, __m128i ax0, __m128i bx0, __m128i bx1, __m128i& cx)
{
    constexpr CnAlgo<ALGO> props;

    if (props.base() == Algorithm::CN_2) {
        VARIANT2_SHUFFLE(l, idx, ax0, bx0, bx1, cx, (ALGO == Algorithm::CN_RWZ ? 1 : 0));
        _mm_store_si128(reinterpret_cast<__m128i *>(mem_out), _mm_xor_si128(bx0, cx));
    } else {
        __m128i tmp = _mm_xor_si128(bx0, cx);
        mem_out[0] = _mm_cvtsi128_si64(tmp);

        tmp = _mm_castps_si128(_mm_movehl_ps(_mm_castsi128_ps(tmp), _mm_castsi128_ps(tmp)));
        uint64_t vh = _mm_cvtsi128_si64(tmp);

        uint8_t x = static_cast<uint8_t>(vh >> 24);
        static const uint16_t table = 0x7531;
        const uint8_t index = (((x >> (3)) & 6) | (x & 1)) << 1;
        vh ^= ((table >> index) & 0x3) << 28;

        mem_out[1] = vh;
    }
}


template<Algorithm::Id ALGO, bool SOFT_AES>
inline void cryptonight_single_hash(const uint8_t *__restrict__ input, size_t size, uint8_t *__restrict__ output, cryptonight_ctx **__restrict__ ctx, uint64_t height)
{
    constexpr CnAlgo<ALGO> props;
    constexpr size_t MASK        = props.mask();
    constexpr Algorithm::Id BASE = props.base();

#   ifdef XMRIG_ALGO_CN_HEAVY
    constexpr bool IS_CN_HEAVY_TUBE = ALGO == Algorithm::CN_HEAVY_TUBE;
#   else
    constexpr bool IS_CN_HEAVY_TUBE = false;
#   endif

    if (BASE == Algorithm::CN_1 && size < 43) {
        memset(output, 0, 32);
        return;
    }

    keccak(input, size, ctx[0]->state);
    cn_explode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i *>(ctx[0]->state), reinterpret_cast<__m128i *>(ctx[0]->memory));

    BBC_INIT();

    uint64_t *h0 = reinterpret_cast<uint64_t*>(ctx[0]->state);
    uint8_t *l0   = ctx[0]->memory;

#   ifdef XMRIG_FEATURE_ASM
    if (SOFT_AES && props.isR()) {
        if (!ctx[0]->generated_code_data.match(ALGO, height)) {
            V4_Instruction code[256];
            const int code_size = v4_random_math_init<ALGO>(code, height);

            if (ALGO == Algorithm::CN_WOW) {
                wow_soft_aes_compile_code(code, code_size, reinterpret_cast<void*>(ctx[0]->generated_code), Assembly::NONE);
            }
            else if (ALGO == Algorithm::CN_R) {
                v4_soft_aes_compile_code(code, code_size, reinterpret_cast<void*>(ctx[0]->generated_code), Assembly::NONE);
            }

            ctx[0]->generated_code_data = { ALGO, height };
        }

        ctx[0]->saes_table = reinterpret_cast<const uint32_t*>(saes_table);
        ctx[0]->generated_code(ctx);
    } else {
#   endif

    VARIANT1_INIT(0);
    VARIANT2_INIT(0);
    VARIANT2_SET_ROUNDING_MODE();
    VARIANT4_RANDOM_MATH_INIT(0);

    uint64_t al0  = h0[0] ^ h0[4];
    uint64_t ah0  = h0[1] ^ h0[5];
    uint64_t idx0 = al0;
    __m128i bx0   = _mm_set_epi64x(static_cast<int64_t>(h0[3] ^ h0[7]), static_cast<int64_t>(h0[2] ^ h0[6]));
    __m128i bx1   = _mm_set_epi64x(static_cast<int64_t>(h0[9] ^ h0[11]), static_cast<int64_t>(h0[8] ^ h0[10]));

    for (size_t i = 0; i < props.iterations(); i++) {
        __m128i cx;
        if (IS_CN_HEAVY_TUBE || !SOFT_AES) {
            cx = _mm_load_si128(reinterpret_cast<const __m128i *>(&l0[idx0 & MASK]));
        }

        const __m128i ax0 = _mm_set_epi64x(static_cast<int64_t>(ah0), static_cast<int64_t>(al0));

        if (IS_CN_HEAVY_TUBE) {
            cx = aes_round_tweak_div(cx, ax0);
        }
        else if (SOFT_AES) {
            cx = soft_aesenc(&l0[idx0 & MASK], ax0, reinterpret_cast<const uint32_t*>(saes_table));
        }
        else {
            cx = _mm_aesenc_si128(cx, ax0);
        }

        BBC_POST_AES();

        if (BASE == Algorithm::CN_1 || BASE == Algorithm::CN_2) {
            cryptonight_monero_tweak<ALGO>(reinterpret_cast<uint64_t*>(&l0[idx0 & MASK]), l0, idx0 & MASK, ax0, bx0, bx1, cx);
        } else {
            _mm_store_si128(reinterpret_cast<__m128i *>(&l0[idx0 & MASK]), _mm_xor_si128(bx0, cx));
        }

        idx0 = static_cast<uint64_t>(_mm_cvtsi128_si64(cx));

        uint64_t hi, lo, cl, ch;
        cl = (reinterpret_cast<uint64_t*>(&l0[idx0 & MASK]))[0];
        ch = (reinterpret_cast<uint64_t*>(&l0[idx0 & MASK]))[1];

        if (BASE == Algorithm::CN_2) {
            if (props.isR()) {
                VARIANT4_RANDOM_MATH(0, al0, ah0, cl, bx0, bx1);
                if (ALGO == Algorithm::CN_R) {
                    al0 ^= r0[2] | (static_cast<uint64_t>(r0[3]) << 32);
                    ah0 ^= r0[0] | (static_cast<uint64_t>(r0[1]) << 32);
                }
            } else {
                VARIANT2_INTEGER_MATH(0, cl, cx);
            }
        }

        lo = __umul128(idx0, cl, &hi);

        if (BASE == Algorithm::CN_2) {
            if (ALGO == Algorithm::CN_R) {
                VARIANT2_SHUFFLE(l0, idx0 & MASK, ax0, bx0, bx1, cx, 0);
            } else {
                VARIANT2_SHUFFLE2(l0, idx0 & MASK, ax0, bx0, bx1, hi, lo, (ALGO == Algorithm::CN_RWZ ? 1 : 0));
            }
        }

        al0 += hi;
        ah0 += lo;

        reinterpret_cast<uint64_t*>(&l0[idx0 & MASK])[0] = al0;

        if (IS_CN_HEAVY_TUBE || ALGO == Algorithm::CN_RTO) {
            reinterpret_cast<uint64_t*>(&l0[idx0 & MASK])[1] = ah0 ^ tweak1_2_0 ^ al0;
        } else if (BASE == Algorithm::CN_1) {
            reinterpret_cast<uint64_t*>(&l0[idx0 & MASK])[1] = ah0 ^ tweak1_2_0;
        } else {
            reinterpret_cast<uint64_t*>(&l0[idx0 & MASK])[1] = ah0;
        }

        al0 ^= cl;
        ah0 ^= ch;

        BBC_PPOST_AES();

        idx0 = al0;

#       ifdef XMRIG_ALGO_CN_HEAVY
        if (props.isHeavy()) {
            int64_t n = ((int64_t*)&l0[idx0 & MASK])[0];
            int32_t d = ((int32_t*)&l0[idx0 & MASK])[2];
            int64_t q = n / (d | 0x5);

            ((int64_t*)&l0[idx0 & MASK])[0] = n ^ q;

            if (ALGO == Algorithm::CN_HEAVY_XHV) {
                d = ~d;
            }

            idx0 = d ^ q;
        }
#       endif

        if (BASE == Algorithm::CN_2) {
            bx1 = bx0;
        }

        bx0 = cx;
    }

#   ifdef XMRIG_FEATURE_ASM
    }
#   endif

    cn_implode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i *>(ctx[0]->memory), reinterpret_cast<__m128i *>(ctx[0]->state));
    keccakf(h0, 24);
    extra_hashes[ctx[0]->state[0] & 3](ctx[0]->state, 200, output);
}


} /* namespace xmrig */


#ifdef XMRIG_ALGO_CN_GPU
template<size_t ITER, uint32_t MASK>
void cn_gpu_inner_avx(const uint8_t *spad, uint8_t *lpad);


template<size_t ITER, uint32_t MASK>
void cn_gpu_inner_ssse3(const uint8_t *spad, uint8_t *lpad);


namespace xmrig {


template<size_t MEM>
void cn_explode_scratchpad_gpu(const uint8_t *input, uint8_t *output)
{
    constexpr size_t hash_size = 200; // 25x8 bytes
    alignas(16) uint64_t hash[25];

    for (uint64_t i = 0; i < MEM / 512; i++) {
        memcpy(hash, input, hash_size);
        hash[0] ^= i;

        xmrig::keccakf(hash, 24);
        memcpy(output, hash, 160);
        output += 160;

        xmrig::keccakf(hash, 24);
        memcpy(output, hash, 176);
        output += 176;

        xmrig::keccakf(hash, 24);
        memcpy(output, hash, 176);
        output += 176;
    }
}


template<Algorithm::Id ALGO, bool SOFT_AES>
inline void cryptonight_single_hash_gpu(const uint8_t *__restrict__ input, size_t size, uint8_t *__restrict__ output, cryptonight_ctx **__restrict__ ctx, uint64_t)
{
    constexpr CnAlgo<ALGO> props;

    keccak(input, size, ctx[0]->state);
    cn_explode_scratchpad_gpu<props.memory()>(ctx[0]->state, ctx[0]->memory);

#   ifdef _MSC_VER
    _control87(RC_NEAR, MCW_RC);
#   else
    fesetround(FE_TONEAREST);
#   endif

    if (xmrig::Cpu::info()->hasAVX2()) {
        cn_gpu_inner_avx<props.iterations(), props.mask()>(ctx[0]->state, ctx[0]->memory);
    } else {
        cn_gpu_inner_ssse3<props.iterations(), props.mask()>(ctx[0]->state, ctx[0]->memory);
    }

    cn_implode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i *>(ctx[0]->memory), reinterpret_cast<__m128i *>(ctx[0]->state));
    keccakf(reinterpret_cast<uint64_t*>(ctx[0]->state), 24);
    memcpy(output, ctx[0]->state, 32);
}


} /* namespace xmrig */
#endif


#ifdef XMRIG_FEATURE_ASM
extern "C" void cnv2_mainloop_ivybridge_asm(cryptonight_ctx **ctx);
extern "C" void cnv2_mainloop_ryzen_asm(cryptonight_ctx **ctx);
extern "C" void cnv2_mainloop_bulldozer_asm(cryptonight_ctx **ctx);
extern "C" void cnv2_double_mainloop_sandybridge_asm(cryptonight_ctx **ctx);
extern "C" void cnv2_rwz_mainloop_asm(cryptonight_ctx **ctx);
extern "C" void cnv2_rwz_double_mainloop_asm(cryptonight_ctx **ctx);


namespace xmrig {


typedef void (*cn_mainloop_fun)(cryptonight_ctx **ctx);


extern cn_mainloop_fun cn_half_mainloop_ivybridge_asm;
extern cn_mainloop_fun cn_half_mainloop_ryzen_asm;
extern cn_mainloop_fun cn_half_mainloop_bulldozer_asm;
extern cn_mainloop_fun cn_half_double_mainloop_sandybridge_asm;

extern cn_mainloop_fun cn_trtl_mainloop_ivybridge_asm;
extern cn_mainloop_fun cn_trtl_mainloop_ryzen_asm;
extern cn_mainloop_fun cn_trtl_mainloop_bulldozer_asm;
extern cn_mainloop_fun cn_trtl_double_mainloop_sandybridge_asm;

extern cn_mainloop_fun cn_zls_mainloop_ivybridge_asm;
extern cn_mainloop_fun cn_zls_mainloop_ryzen_asm;
extern cn_mainloop_fun cn_zls_mainloop_bulldozer_asm;
extern cn_mainloop_fun cn_zls_double_mainloop_sandybridge_asm;

extern cn_mainloop_fun cn_double_mainloop_ivybridge_asm;
extern cn_mainloop_fun cn_double_mainloop_ryzen_asm;
extern cn_mainloop_fun cn_double_mainloop_bulldozer_asm;
extern cn_mainloop_fun cn_double_double_mainloop_sandybridge_asm;


} // namespace xmrig


void wow_compile_code(const V4_Instruction* code, int code_size, void* machine_code, xmrig::Assembly ASM);
void v4_compile_code(const V4_Instruction* code, int code_size, void* machine_code, xmrig::Assembly ASM);
void wow_compile_code_double(const V4_Instruction* code, int code_size, void* machine_code, xmrig::Assembly ASM);
void v4_compile_code_double(const V4_Instruction* code, int code_size, void* machine_code, xmrig::Assembly ASM);


template<xmrig::Algorithm::Id ALGO>
void cn_r_compile_code(const V4_Instruction* code, int code_size, void* machine_code, xmrig::Assembly ASM)
{
    v4_compile_code(code, code_size, machine_code, ASM);
}


template<xmrig::Algorithm::Id ALGO>
void cn_r_compile_code_double(const V4_Instruction* code, int code_size, void* machine_code, xmrig::Assembly ASM)
{
    v4_compile_code_double(code, code_size, machine_code, ASM);
}


template<>
void cn_r_compile_code<xmrig::Algorithm::CN_WOW>(const V4_Instruction* code, int code_size, void* machine_code, xmrig::Assembly ASM)
{
    wow_compile_code(code, code_size, machine_code, ASM);
}


template<>
void cn_r_compile_code_double<xmrig::Algorithm::CN_WOW>(const V4_Instruction* code, int code_size, void* machine_code, xmrig::Assembly ASM)
{
    wow_compile_code_double(code, code_size, machine_code, ASM);
}


namespace xmrig {


template<Algorithm::Id ALGO, Assembly::Id ASM>
inline void cryptonight_single_hash_asm(const uint8_t *__restrict__ input, size_t size, uint8_t *__restrict__ output, cryptonight_ctx **__restrict__ ctx, uint64_t height)
{
    constexpr CnAlgo<ALGO> props;

    if (props.isR() && !ctx[0]->generated_code_data.match(ALGO, height)) {
        V4_Instruction code[256];
        const int code_size = v4_random_math_init<ALGO>(code, height);
        cn_r_compile_code<ALGO>(code, code_size, reinterpret_cast<void*>(ctx[0]->generated_code), ASM);

        ctx[0]->generated_code_data = { ALGO, height };
    }

    keccak(input, size, ctx[0]->state);
    cn_explode_scratchpad<ALGO, false>(reinterpret_cast<const __m128i*>(ctx[0]->state), reinterpret_cast<__m128i*>(ctx[0]->memory));

    if (ALGO == Algorithm::CN_2) {
        if (ASM == Assembly::INTEL) {
            cnv2_mainloop_ivybridge_asm(ctx);
        }
        else if (ASM == Assembly::RYZEN) {
            cnv2_mainloop_ryzen_asm(ctx);
        }
        else {
            cnv2_mainloop_bulldozer_asm(ctx);
        }
    }
    else if (ALGO == Algorithm::CN_HALF) {
        if (ASM == Assembly::INTEL) {
            cn_half_mainloop_ivybridge_asm(ctx);
        }
        else if (ASM == Assembly::RYZEN) {
            cn_half_mainloop_ryzen_asm(ctx);
        }
        else {
            cn_half_mainloop_bulldozer_asm(ctx);
        }
    }
#   ifdef XMRIG_ALGO_CN_PICO
    else if (ALGO == Algorithm::CN_PICO_0) {
        if (ASM == Assembly::INTEL) {
            cn_trtl_mainloop_ivybridge_asm(ctx);
        }
        else if (ASM == Assembly::RYZEN) {
            cn_trtl_mainloop_ryzen_asm(ctx);
        }
        else {
            cn_trtl_mainloop_bulldozer_asm(ctx);
        }
    }
#   endif
    else if (ALGO == Algorithm::CN_RWZ) {
        cnv2_rwz_mainloop_asm(ctx);
    }
    else if (ALGO == Algorithm::CN_ZLS) {
        if (ASM == Assembly::INTEL) {
            cn_zls_mainloop_ivybridge_asm(ctx);
        }
        else if (ASM == Assembly::RYZEN) {
            cn_zls_mainloop_ryzen_asm(ctx);
        }
        else {
            cn_zls_mainloop_bulldozer_asm(ctx);
        }
    }
    else if (ALGO == Algorithm::CN_DOUBLE) {
        if (ASM == Assembly::INTEL) {
            cn_double_mainloop_ivybridge_asm(ctx);
        }
        else if (ASM == Assembly::RYZEN) {
            cn_double_mainloop_ryzen_asm(ctx);
        }
        else {
            cn_double_mainloop_bulldozer_asm(ctx);
        }
    }
    else if (props.isR()) {
        ctx[0]->generated_code(ctx);
    }

    cn_implode_scratchpad<ALGO, false>(reinterpret_cast<const __m128i*>(ctx[0]->memory), reinterpret_cast<__m128i*>(ctx[0]->state));
    keccakf(reinterpret_cast<uint64_t*>(ctx[0]->state), 24);
    extra_hashes[ctx[0]->state[0] & 3](ctx[0]->state, 200, output);
}


template<Algorithm::Id ALGO, Assembly::Id ASM>
inline void cryptonight_double_hash_asm(const uint8_t *__restrict__ input, size_t size, uint8_t *__restrict__ output, cryptonight_ctx **__restrict__ ctx, uint64_t height)
{
    constexpr CnAlgo<ALGO> props;

    if (props.isR() && !ctx[0]->generated_code_data.match(ALGO, height)) {
        V4_Instruction code[256];
        const int code_size = v4_random_math_init<ALGO>(code, height);
        cn_r_compile_code_double<ALGO>(code, code_size, reinterpret_cast<void*>(ctx[0]->generated_code), ASM);

        ctx[0]->generated_code_data = { ALGO, height };
    }

    keccak(input,        size, ctx[0]->state);
    keccak(input + size, size, ctx[1]->state);

    cn_explode_scratchpad<ALGO, false>(reinterpret_cast<const __m128i*>(ctx[0]->state), reinterpret_cast<__m128i*>(ctx[0]->memory));
    cn_explode_scratchpad<ALGO, false>(reinterpret_cast<const __m128i*>(ctx[1]->state), reinterpret_cast<__m128i*>(ctx[1]->memory));

    if (ALGO == Algorithm::CN_2) {
        cnv2_double_mainloop_sandybridge_asm(ctx);
    }
    else if (ALGO == Algorithm::CN_HALF) {
        cn_half_double_mainloop_sandybridge_asm(ctx);
    }
#   ifdef XMRIG_ALGO_CN_PICO
    else if (ALGO == Algorithm::CN_PICO_0) {
        cn_trtl_double_mainloop_sandybridge_asm(ctx);
    }
#   endif
    else if (ALGO == Algorithm::CN_RWZ) {
        cnv2_rwz_double_mainloop_asm(ctx);
    }
    else if (ALGO == Algorithm::CN_ZLS) {
        cn_zls_double_mainloop_sandybridge_asm(ctx);
    }
    else if (ALGO == Algorithm::CN_DOUBLE) {
        cn_double_double_mainloop_sandybridge_asm(ctx);
    }
    else if (props.isR()) {
        ctx[0]->generated_code(ctx);
    }

    cn_implode_scratchpad<ALGO, false>(reinterpret_cast<const __m128i*>(ctx[0]->memory), reinterpret_cast<__m128i*>(ctx[0]->state));
    cn_implode_scratchpad<ALGO, false>(reinterpret_cast<const __m128i*>(ctx[1]->memory), reinterpret_cast<__m128i*>(ctx[1]->state));

    keccakf(reinterpret_cast<uint64_t*>(ctx[0]->state), 24);
    keccakf(reinterpret_cast<uint64_t*>(ctx[1]->state), 24);

    extra_hashes[ctx[0]->state[0] & 3](ctx[0]->state, 200, output);
    extra_hashes[ctx[1]->state[0] & 3](ctx[1]->state, 200, output + 32);
}


} /* namespace xmrig */
#endif


namespace xmrig {


template<Algorithm::Id ALGO, bool SOFT_AES>
inline void cryptonight_double_hash(const uint8_t *__restrict__ input, size_t size, uint8_t *__restrict__ output, cryptonight_ctx **__restrict__ ctx, uint64_t height)
{
    constexpr CnAlgo<ALGO> props;
    constexpr size_t MASK        = props.mask();
    constexpr Algorithm::Id BASE = props.base();

#   ifdef XMRIG_ALGO_CN_HEAVY
    constexpr bool IS_CN_HEAVY_TUBE = ALGO == Algorithm::CN_HEAVY_TUBE;
#   else
    constexpr bool IS_CN_HEAVY_TUBE = false;
#   endif

    if (BASE == Algorithm::CN_1 && size < 43) {
        memset(output, 0, 64);
        return;
    }

    keccak(input,        size, ctx[0]->state);
    keccak(input + size, size, ctx[1]->state);

    uint8_t *l0  = ctx[0]->memory;
    uint8_t *l1  = ctx[1]->memory;
    uint64_t *h0 = reinterpret_cast<uint64_t*>(ctx[0]->state);
    uint64_t *h1 = reinterpret_cast<uint64_t*>(ctx[1]->state);

    VARIANT1_INIT(0);
    VARIANT1_INIT(1);
    VARIANT2_INIT(0);
    VARIANT2_INIT(1);
    VARIANT2_SET_ROUNDING_MODE();
    VARIANT4_RANDOM_MATH_INIT(0);
    VARIANT4_RANDOM_MATH_INIT(1);

    cn_explode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i *>(h0), reinterpret_cast<__m128i *>(l0));
    cn_explode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i *>(h1), reinterpret_cast<__m128i *>(l1));

    uint64_t al0 = h0[0] ^ h0[4];
    uint64_t al1 = h1[0] ^ h1[4];
    uint64_t ah0 = h0[1] ^ h0[5];
    uint64_t ah1 = h1[1] ^ h1[5];

    __m128i bx00 = _mm_set_epi64x(h0[3] ^ h0[7], h0[2] ^ h0[6]);
    __m128i bx01 = _mm_set_epi64x(h0[9] ^ h0[11], h0[8] ^ h0[10]);
    __m128i bx10 = _mm_set_epi64x(h1[3] ^ h1[7], h1[2] ^ h1[6]);
    __m128i bx11 = _mm_set_epi64x(h1[9] ^ h1[11], h1[8] ^ h1[10]);

    uint64_t idx0 = al0;
    uint64_t idx1 = al1;

    for (size_t i = 0; i < props.iterations(); i++) {
        __m128i cx0, cx1;
        if (IS_CN_HEAVY_TUBE || !SOFT_AES) {
            cx0 = _mm_load_si128(reinterpret_cast<const __m128i *>(&l0[idx0 & MASK]));
            cx1 = _mm_load_si128(reinterpret_cast<const __m128i *>(&l1[idx1 & MASK]));
        }

        const __m128i ax0 = _mm_set_epi64x(ah0, al0);
        const __m128i ax1 = _mm_set_epi64x(ah1, al1);
        if (IS_CN_HEAVY_TUBE) {
            cx0 = aes_round_tweak_div(cx0, ax0);
            cx1 = aes_round_tweak_div(cx1, ax1);
        }
        else if (SOFT_AES) {
            cx0 = soft_aesenc(&l0[idx0 & MASK], ax0, reinterpret_cast<const uint32_t*>(saes_table));
            cx1 = soft_aesenc(&l1[idx1 & MASK], ax1, reinterpret_cast<const uint32_t*>(saes_table));
        }
        else {
            cx0 = _mm_aesenc_si128(cx0, ax0);
            cx1 = _mm_aesenc_si128(cx1, ax1);
        }

        if (BASE == Algorithm::CN_1 || BASE == Algorithm::CN_2) {
            cryptonight_monero_tweak<ALGO>((uint64_t*)&l0[idx0 & MASK], l0, idx0 & MASK, ax0, bx00, bx01, cx0);
            cryptonight_monero_tweak<ALGO>((uint64_t*)&l1[idx1 & MASK], l1, idx1 & MASK, ax1, bx10, bx11, cx1);
        } else {
            _mm_store_si128((__m128i *) &l0[idx0 & MASK], _mm_xor_si128(bx00, cx0));
            _mm_store_si128((__m128i *) &l1[idx1 & MASK], _mm_xor_si128(bx10, cx1));
        }

        idx0 = _mm_cvtsi128_si64(cx0);
        idx1 = _mm_cvtsi128_si64(cx1);

        uint64_t hi, lo, cl, ch;
        cl = ((uint64_t*) &l0[idx0 & MASK])[0];
        ch = ((uint64_t*) &l0[idx0 & MASK])[1];

        if (BASE == Algorithm::CN_2) {
            if (props.isR()) {
                VARIANT4_RANDOM_MATH(0, al0, ah0, cl, bx00, bx01);
                if (ALGO == Algorithm::CN_R) {
                    al0 ^= r0[2] | ((uint64_t)(r0[3]) << 32);
                    ah0 ^= r0[0] | ((uint64_t)(r0[1]) << 32);
                }
            } else {
                VARIANT2_INTEGER_MATH(0, cl, cx0);
            }
        }

        lo = __umul128(idx0, cl, &hi);

        if (BASE == Algorithm::CN_2) {
            if (ALGO == Algorithm::CN_R) {
                VARIANT2_SHUFFLE(l0, idx0 & MASK, ax0, bx00, bx01, cx0, 0);
            } else {
                VARIANT2_SHUFFLE2(l0, idx0 & MASK, ax0, bx00, bx01, hi, lo, (ALGO == Algorithm::CN_RWZ ? 1 : 0));
            }
        }

        al0 += hi;
        ah0 += lo;

        ((uint64_t*)&l0[idx0 & MASK])[0] = al0;

        if (IS_CN_HEAVY_TUBE || ALGO == Algorithm::CN_RTO) {
            ((uint64_t*) &l0[idx0 & MASK])[1] = ah0 ^ tweak1_2_0 ^ al0;
        } else if (BASE == Algorithm::CN_1) {
            ((uint64_t*) &l0[idx0 & MASK])[1] = ah0 ^ tweak1_2_0;
        } else {
            ((uint64_t*) &l0[idx0 & MASK])[1] = ah0;
        }

        al0 ^= cl;
        ah0 ^= ch;
        idx0 = al0;

#       ifdef XMRIG_ALGO_CN_HEAVY
        if (props.isHeavy()) {
            int64_t n = ((int64_t*)&l0[idx0 & MASK])[0];
            int32_t d = ((int32_t*)&l0[idx0 & MASK])[2];
            int64_t q = n / (d | 0x5);

            ((int64_t*)&l0[idx0 & MASK])[0] = n ^ q;

            if (ALGO == Algorithm::CN_HEAVY_XHV) {
                d = ~d;
            }

            idx0 = d ^ q;
        }
#       endif

        cl = ((uint64_t*) &l1[idx1 & MASK])[0];
        ch = ((uint64_t*) &l1[idx1 & MASK])[1];

        if (BASE == Algorithm::CN_2) {
            if (props.isR()) {
                VARIANT4_RANDOM_MATH(1, al1, ah1, cl, bx10, bx11);
                if (ALGO == Algorithm::CN_R) {
                    al1 ^= r1[2] | ((uint64_t)(r1[3]) << 32);
                    ah1 ^= r1[0] | ((uint64_t)(r1[1]) << 32);
                }
            } else {
                VARIANT2_INTEGER_MATH(1, cl, cx1);
            }
        }

        lo = __umul128(idx1, cl, &hi);

        if (BASE == Algorithm::CN_2) {
            if (ALGO == Algorithm::CN_R) {
                VARIANT2_SHUFFLE(l1, idx1 & MASK, ax1, bx10, bx11, cx1, 0);
            } else {
                VARIANT2_SHUFFLE2(l1, idx1 & MASK, ax1, bx10, bx11, hi, lo, (ALGO == Algorithm::CN_RWZ ? 1 : 0));
            }
        }

        al1 += hi;
        ah1 += lo;

        ((uint64_t*)&l1[idx1 & MASK])[0] = al1;

        if (IS_CN_HEAVY_TUBE || ALGO == Algorithm::CN_RTO) {
            ((uint64_t*)&l1[idx1 & MASK])[1] = ah1 ^ tweak1_2_1 ^ al1;
        } else if (BASE == Algorithm::CN_1) {
            ((uint64_t*)&l1[idx1 & MASK])[1] = ah1 ^ tweak1_2_1;
        } else {
            ((uint64_t*)&l1[idx1 & MASK])[1] = ah1;
        }

        al1 ^= cl;
        ah1 ^= ch;
        idx1 = al1;

#       ifdef XMRIG_ALGO_CN_HEAVY
        if (props.isHeavy()) {
            int64_t n = ((int64_t*)&l1[idx1 & MASK])[0];
            int32_t d = ((int32_t*)&l1[idx1 & MASK])[2];
            int64_t q = n / (d | 0x5);

            ((int64_t*)&l1[idx1 & MASK])[0] = n ^ q;

            if (ALGO == Algorithm::CN_HEAVY_XHV) {
                d = ~d;
            }

            idx1 = d ^ q;
        }
#       endif

        if (BASE == Algorithm::CN_2) {
            bx01 = bx00;
            bx11 = bx10;
        }

        bx00 = cx0;
        bx10 = cx1;
    }

    cn_implode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i *>(l0), reinterpret_cast<__m128i *>(h0));
    cn_implode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i *>(l1), reinterpret_cast<__m128i *>(h1));

    keccakf(h0, 24);
    keccakf(h1, 24);

    extra_hashes[ctx[0]->state[0] & 3](ctx[0]->state, 200, output);
    extra_hashes[ctx[1]->state[0] & 3](ctx[1]->state, 200, output + 32);
}


#define CN_STEP1(a, b0, b1, c, l, ptr, idx)           \
    ptr = reinterpret_cast<__m128i*>(&l[idx & MASK]); \
    c = _mm_load_si128(ptr);


#define CN_STEP2(a, b0, b1, c, l, ptr, idx)                                             \
    if (IS_CN_HEAVY_TUBE) {                                                             \
        c = aes_round_tweak_div(c, a);                                                  \
    }                                                                                   \
    else if (SOFT_AES) {                                                                \
        c = soft_aesenc(&c, a, (const uint32_t*)saes_table);                            \
    } else {                                                                            \
        c = _mm_aesenc_si128(c, a);                                                     \
    }                                                                                   \
                                                                                        \
    if (BASE == Algorithm::CN_1 || BASE == Algorithm::CN_2) {                           \
        cryptonight_monero_tweak<ALGO>((uint64_t*)ptr, l, idx & MASK, a, b0, b1, c);    \
    } else {                                                                            \
        _mm_store_si128(ptr, _mm_xor_si128(b0, c));                                     \
    }


#define CN_STEP3(part, a, b0, b1, c, l, ptr, idx)     \
    idx = _mm_cvtsi128_si64(c);                       \
    ptr = reinterpret_cast<__m128i*>(&l[idx & MASK]); \
    uint64_t cl##part = ((uint64_t*)ptr)[0];          \
    uint64_t ch##part = ((uint64_t*)ptr)[1];


#define CN_STEP4(part, a, b0, b1, c, l, mc, ptr, idx)                                                       \
    uint64_t al##part, ah##part;                                                                            \
    if (BASE == Algorithm::CN_2) {                                                                          \
        if (props.isR()) {                                                                                  \
            al##part = _mm_cvtsi128_si64(a);                                                                \
            ah##part = _mm_cvtsi128_si64(_mm_srli_si128(a, 8));                                             \
            VARIANT4_RANDOM_MATH(part, al##part, ah##part, cl##part, b0, b1);                               \
            if (ALGO == Algorithm::CN_R) {                                                                  \
                al##part ^= r##part[2] | ((uint64_t)(r##part[3]) << 32);                                    \
                ah##part ^= r##part[0] | ((uint64_t)(r##part[1]) << 32);                                    \
            }                                                                                               \
        } else {                                                                                            \
            VARIANT2_INTEGER_MATH(part, cl##part, c);                                                       \
        }                                                                                                   \
    }                                                                                                       \
    lo = __umul128(idx, cl##part, &hi);                                                                     \
    if (BASE == Algorithm::CN_2) {                                                                          \
        if (ALGO == Algorithm::CN_R) {                                                                      \
            VARIANT2_SHUFFLE(l, idx & MASK, a, b0, b1, c, 0);                                               \
        } else {                                                                                            \
            VARIANT2_SHUFFLE2(l, idx & MASK, a, b0, b1, hi, lo, (ALGO == Algorithm::CN_RWZ ? 1 : 0));       \
        }                                                                                                   \
    }                                                                                                       \
    if (ALGO == Algorithm::CN_R) {                                                                          \
        a = _mm_set_epi64x(ah##part, al##part);                                                             \
    }                                                                                                       \
    a = _mm_add_epi64(a, _mm_set_epi64x(lo, hi));                                                           \
                                                                                                            \
    if (BASE == Algorithm::CN_1) {                                                                          \
        _mm_store_si128(ptr, _mm_xor_si128(a, mc));                                                         \
                                                                                                            \
        if (IS_CN_HEAVY_TUBE || ALGO == Algorithm::CN_RTO) {                                                \
            ((uint64_t*)ptr)[1] ^= ((uint64_t*)ptr)[0];                                                     \
        }                                                                                                   \
    } else {                                                                                                \
        _mm_store_si128(ptr, a);                                                                            \
    }                                                                                                       \
                                                                                                            \
    a = _mm_xor_si128(a, _mm_set_epi64x(ch##part, cl##part));                                               \
    idx = _mm_cvtsi128_si64(a);                                                                             \
    if (props.isHeavy()) {                                                                                  \
        int64_t n = ((int64_t*)&l[idx & MASK])[0];                                                          \
        int32_t d = ((int32_t*)&l[idx & MASK])[2];                                                          \
        int64_t q = n / (d | 0x5);                                                                          \
        ((int64_t*)&l[idx & MASK])[0] = n ^ q;                                                              \
        if (IS_CN_HEAVY_XHV) {                                                                              \
            d = ~d;                                                                                         \
        }                                                                                                   \
                                                                                                            \
        idx = d ^ q;                                                                                        \
    }                                                                                                       \
    if (BASE == Algorithm::CN_2) {                                                                          \
        b1 = b0;                                                                                            \
    }                                                                                                       \
    b0 = c;


#define CONST_INIT(ctx, n)                                                                       \
    __m128i mc##n;                                                                               \
    __m128i division_result_xmm_##n;                                                             \
    __m128i sqrt_result_xmm_##n;                                                                 \
    if (BASE == Algorithm::CN_1) {                                                               \
        mc##n = _mm_set_epi64x(*reinterpret_cast<const uint64_t*>(input + n * size + 35) ^       \
                               *(reinterpret_cast<const uint64_t*>((ctx)->state) + 24), 0);      \
    }                                                                                            \
    if (BASE == Algorithm::CN_2) {                                                               \
        division_result_xmm_##n = _mm_cvtsi64_si128(h##n[12]);                                   \
        sqrt_result_xmm_##n = _mm_cvtsi64_si128(h##n[13]);                                       \
    }                                                                                            \
    __m128i ax##n = _mm_set_epi64x(h##n[1] ^ h##n[5], h##n[0] ^ h##n[4]);                        \
    __m128i bx##n##0 = _mm_set_epi64x(h##n[3] ^ h##n[7], h##n[2] ^ h##n[6]);                     \
    __m128i bx##n##1 = _mm_set_epi64x(h##n[9] ^ h##n[11], h##n[8] ^ h##n[10]);                   \
    __m128i cx##n = _mm_setzero_si128();                                                         \
    VARIANT4_RANDOM_MATH_INIT(n);


template<Algorithm::Id ALGO, bool SOFT_AES>
inline void cryptonight_triple_hash(const uint8_t *__restrict__ input, size_t size, uint8_t *__restrict__ output, cryptonight_ctx **__restrict__ ctx, uint64_t height)
{
    constexpr CnAlgo<ALGO> props;
    constexpr size_t MASK        = props.mask();
    constexpr Algorithm::Id BASE = props.base();

#   ifdef XMRIG_ALGO_CN_HEAVY
    constexpr bool IS_CN_HEAVY_TUBE = ALGO == Algorithm::CN_HEAVY_TUBE;
    constexpr bool IS_CN_HEAVY_XHV  = ALGO == Algorithm::CN_HEAVY_XHV;
#   else
    constexpr bool IS_CN_HEAVY_TUBE = false;
    constexpr bool IS_CN_HEAVY_XHV  = false;
#   endif

    if (BASE == Algorithm::CN_1 && size < 43) {
        memset(output, 0, 32 * 3);
        return;
    }

    for (size_t i = 0; i < 3; i++) {
        keccak(input + size * i, size, ctx[i]->state);
        cn_explode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i*>(ctx[i]->state), reinterpret_cast<__m128i*>(ctx[i]->memory));
    }

    uint8_t* l0  = ctx[0]->memory;
    uint8_t* l1  = ctx[1]->memory;
    uint8_t* l2  = ctx[2]->memory;
    uint64_t* h0 = reinterpret_cast<uint64_t*>(ctx[0]->state);
    uint64_t* h1 = reinterpret_cast<uint64_t*>(ctx[1]->state);
    uint64_t* h2 = reinterpret_cast<uint64_t*>(ctx[2]->state);

    CONST_INIT(ctx[0], 0);
    CONST_INIT(ctx[1], 1);
    CONST_INIT(ctx[2], 2);
    VARIANT2_SET_ROUNDING_MODE();

    uint64_t idx0, idx1, idx2;
    idx0 = _mm_cvtsi128_si64(ax0);
    idx1 = _mm_cvtsi128_si64(ax1);
    idx2 = _mm_cvtsi128_si64(ax2);

    for (size_t i = 0; i < props.iterations(); i++) {
        uint64_t hi, lo;
        __m128i *ptr0, *ptr1, *ptr2;

        CN_STEP1(ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP1(ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP1(ax2, bx20, bx21, cx2, l2, ptr2, idx2);

        CN_STEP2(ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP2(ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP2(ax2, bx20, bx21, cx2, l2, ptr2, idx2);

        CN_STEP3(0, ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP3(1, ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP3(2, ax2, bx20, bx21, cx2, l2, ptr2, idx2);

        CN_STEP4(0, ax0, bx00, bx01, cx0, l0, mc0, ptr0, idx0);
        CN_STEP4(1, ax1, bx10, bx11, cx1, l1, mc1, ptr1, idx1);
        CN_STEP4(2, ax2, bx20, bx21, cx2, l2, mc2, ptr2, idx2);
    }

    for (size_t i = 0; i < 3; i++) {
        cn_implode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i*>(ctx[i]->memory), reinterpret_cast<__m128i*>(ctx[i]->state));
        keccakf(reinterpret_cast<uint64_t*>(ctx[i]->state), 24);
        extra_hashes[ctx[i]->state[0] & 3](ctx[i]->state, 200, output + 32 * i);
    }
}


template<Algorithm::Id ALGO, bool SOFT_AES>
inline void cryptonight_quad_hash(const uint8_t *__restrict__ input, size_t size, uint8_t *__restrict__ output, cryptonight_ctx **__restrict__ ctx, uint64_t height)
{
    constexpr CnAlgo<ALGO> props;
    constexpr size_t MASK        = props.mask();
    constexpr Algorithm::Id BASE = props.base();

#   ifdef XMRIG_ALGO_CN_HEAVY
    constexpr bool IS_CN_HEAVY_TUBE = ALGO == Algorithm::CN_HEAVY_TUBE;
    constexpr bool IS_CN_HEAVY_XHV  = ALGO == Algorithm::CN_HEAVY_XHV;
#   else
    constexpr bool IS_CN_HEAVY_TUBE = false;
    constexpr bool IS_CN_HEAVY_XHV  = false;
#   endif

    if (BASE == Algorithm::CN_1 && size < 43) {
        memset(output, 0, 32 * 4);
        return;
    }

    for (size_t i = 0; i < 4; i++) {
        keccak(input + size * i, size, ctx[i]->state);
        cn_explode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i*>(ctx[i]->state), reinterpret_cast<__m128i*>(ctx[i]->memory));
    }

    uint8_t* l0  = ctx[0]->memory;
    uint8_t* l1  = ctx[1]->memory;
    uint8_t* l2  = ctx[2]->memory;
    uint8_t* l3  = ctx[3]->memory;
    uint64_t* h0 = reinterpret_cast<uint64_t*>(ctx[0]->state);
    uint64_t* h1 = reinterpret_cast<uint64_t*>(ctx[1]->state);
    uint64_t* h2 = reinterpret_cast<uint64_t*>(ctx[2]->state);
    uint64_t* h3 = reinterpret_cast<uint64_t*>(ctx[3]->state);

    CONST_INIT(ctx[0], 0);
    CONST_INIT(ctx[1], 1);
    CONST_INIT(ctx[2], 2);
    CONST_INIT(ctx[3], 3);
    VARIANT2_SET_ROUNDING_MODE();

    uint64_t idx0, idx1, idx2, idx3;
    idx0 = _mm_cvtsi128_si64(ax0);
    idx1 = _mm_cvtsi128_si64(ax1);
    idx2 = _mm_cvtsi128_si64(ax2);
    idx3 = _mm_cvtsi128_si64(ax3);

    for (size_t i = 0; i < props.iterations(); i++) {
        uint64_t hi, lo;
        __m128i *ptr0, *ptr1, *ptr2, *ptr3;

        CN_STEP1(ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP1(ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP1(ax2, bx20, bx21, cx2, l2, ptr2, idx2);
        CN_STEP1(ax3, bx30, bx31, cx3, l3, ptr3, idx3);

        CN_STEP2(ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP2(ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP2(ax2, bx20, bx21, cx2, l2, ptr2, idx2);
        CN_STEP2(ax3, bx30, bx31, cx3, l3, ptr3, idx3);

        CN_STEP3(0, ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP3(1, ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP3(2, ax2, bx20, bx21, cx2, l2, ptr2, idx2);
        CN_STEP3(3, ax3, bx30, bx31, cx3, l3, ptr3, idx3);

        CN_STEP4(0, ax0, bx00, bx01, cx0, l0, mc0, ptr0, idx0);
        CN_STEP4(1, ax1, bx10, bx11, cx1, l1, mc1, ptr1, idx1);
        CN_STEP4(2, ax2, bx20, bx21, cx2, l2, mc2, ptr2, idx2);
        CN_STEP4(3, ax3, bx30, bx31, cx3, l3, mc3, ptr3, idx3);
    }

    for (size_t i = 0; i < 4; i++) {
        cn_implode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i*>(ctx[i]->memory), reinterpret_cast<__m128i*>(ctx[i]->state));
        keccakf(reinterpret_cast<uint64_t*>(ctx[i]->state), 24);
        extra_hashes[ctx[i]->state[0] & 3](ctx[i]->state, 200, output + 32 * i);
    }
}


template<Algorithm::Id ALGO, bool SOFT_AES>
inline void cryptonight_penta_hash(const uint8_t *__restrict__ input, size_t size, uint8_t *__restrict__ output, cryptonight_ctx **__restrict__ ctx, uint64_t height)
{
    constexpr CnAlgo<ALGO> props;
    constexpr size_t MASK        = props.mask();
    constexpr Algorithm::Id BASE = props.base();

#   ifdef XMRIG_ALGO_CN_HEAVY
    constexpr bool IS_CN_HEAVY_TUBE = ALGO == Algorithm::CN_HEAVY_TUBE;
    constexpr bool IS_CN_HEAVY_XHV  = ALGO == Algorithm::CN_HEAVY_XHV;
#   else
    constexpr bool IS_CN_HEAVY_TUBE = false;
    constexpr bool IS_CN_HEAVY_XHV  = false;
#   endif

    if (BASE == Algorithm::CN_1 && size < 43) {
        memset(output, 0, 32 * 5);
        return;
    }

    for (size_t i = 0; i < 5; i++) {
        keccak(input + size * i, size, ctx[i]->state);
        cn_explode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i*>(ctx[i]->state), reinterpret_cast<__m128i*>(ctx[i]->memory));
    }

    uint8_t* l0  = ctx[0]->memory;
    uint8_t* l1  = ctx[1]->memory;
    uint8_t* l2  = ctx[2]->memory;
    uint8_t* l3  = ctx[3]->memory;
    uint8_t* l4  = ctx[4]->memory;
    uint64_t* h0 = reinterpret_cast<uint64_t*>(ctx[0]->state);
    uint64_t* h1 = reinterpret_cast<uint64_t*>(ctx[1]->state);
    uint64_t* h2 = reinterpret_cast<uint64_t*>(ctx[2]->state);
    uint64_t* h3 = reinterpret_cast<uint64_t*>(ctx[3]->state);
    uint64_t* h4 = reinterpret_cast<uint64_t*>(ctx[4]->state);

    CONST_INIT(ctx[0], 0);
    CONST_INIT(ctx[1], 1);
    CONST_INIT(ctx[2], 2);
    CONST_INIT(ctx[3], 3);
    CONST_INIT(ctx[4], 4);
    VARIANT2_SET_ROUNDING_MODE();

    uint64_t idx0, idx1, idx2, idx3, idx4;
    idx0 = _mm_cvtsi128_si64(ax0);
    idx1 = _mm_cvtsi128_si64(ax1);
    idx2 = _mm_cvtsi128_si64(ax2);
    idx3 = _mm_cvtsi128_si64(ax3);
    idx4 = _mm_cvtsi128_si64(ax4);

    for (size_t i = 0; i < props.iterations(); i++) {
        uint64_t hi, lo;
        __m128i *ptr0, *ptr1, *ptr2, *ptr3, *ptr4;

        CN_STEP1(ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP1(ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP1(ax2, bx20, bx21, cx2, l2, ptr2, idx2);
        CN_STEP1(ax3, bx30, bx31, cx3, l3, ptr3, idx3);
        CN_STEP1(ax4, bx40, bx41, cx4, l4, ptr4, idx4);

        CN_STEP2(ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP2(ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP2(ax2, bx20, bx21, cx2, l2, ptr2, idx2);
        CN_STEP2(ax3, bx30, bx31, cx3, l3, ptr3, idx3);
        CN_STEP2(ax4, bx40, bx41, cx4, l4, ptr4, idx4);

        CN_STEP3(0, ax0, bx00, bx01, cx0, l0, ptr0, idx0);
        CN_STEP3(1, ax1, bx10, bx11, cx1, l1, ptr1, idx1);
        CN_STEP3(2, ax2, bx20, bx21, cx2, l2, ptr2, idx2);
        CN_STEP3(3, ax3, bx30, bx31, cx3, l3, ptr3, idx3);
        CN_STEP3(4, ax4, bx40, bx41, cx4, l4, ptr4, idx4);

        CN_STEP4(0, ax0, bx00, bx01, cx0, l0, mc0, ptr0, idx0);
        CN_STEP4(1, ax1, bx10, bx11, cx1, l1, mc1, ptr1, idx1);
        CN_STEP4(2, ax2, bx20, bx21, cx2, l2, mc2, ptr2, idx2);
        CN_STEP4(3, ax3, bx30, bx31, cx3, l3, mc3, ptr3, idx3);
        CN_STEP4(4, ax4, bx40, bx41, cx4, l4, mc4, ptr4, idx4);
    }

    for (size_t i = 0; i < 5; i++) {
        cn_implode_scratchpad<ALGO, SOFT_AES>(reinterpret_cast<const __m128i*>(ctx[i]->memory), reinterpret_cast<__m128i*>(ctx[i]->state));
        keccakf(reinterpret_cast<uint64_t*>(ctx[i]->state), 24);
        extra_hashes[ctx[i]->state[0] & 3](ctx[i]->state, 200, output + 32 * i);
    }
}


} /* namespace xmrig */


#endif /* XMRIG_CRYPTONIGHT_X86_H */
