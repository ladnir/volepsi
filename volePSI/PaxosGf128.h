#pragma once
#include "volePSI/Defines.h"

// VOLEPSI_PAXOS_FORCE_SCALAR selects the block backend for validation.
namespace volePSI::details
{
#if !defined(VOLEPSI_PAXOS_FORCE_SCALAR) && defined(OC_ENABLE_PCLMUL) && defined(__VPCLMULQDQ__) && defined(__AVX512F__) && defined(__AVX512BW__)
    inline __m512i paxosMulGF128(__m512i x, __m512i y)
    {
        auto lo = _mm512_clmulepi64_epi128(x, y, 0x00);
        auto hi = _mm512_clmulepi64_epi128(x, y, 0x11);
        auto mid = _mm512_xor_si512(_mm512_clmulepi64_epi128(x, y, 0x01),
                                   _mm512_clmulepi64_epi128(x, y, 0x10));
        lo = _mm512_xor_si512(lo, _mm512_bslli_epi128(mid, 8));
        hi = _mm512_xor_si512(hi, _mm512_bsrli_epi128(mid, 8));
        const auto modulus = _mm512_set1_epi64(0x87);
        auto t = _mm512_clmulepi64_epi128(hi, modulus, 0x01);
        lo = _mm512_xor_si512(lo, _mm512_bslli_epi128(t, 8));
        hi = _mm512_xor_si512(hi, _mm512_bsrli_epi128(t, 8));
        return _mm512_xor_si512(lo, _mm512_clmulepi64_epi128(hi, modulus, 0x00));
    }
#elif !defined(VOLEPSI_PAXOS_FORCE_SCALAR) && defined(OC_ENABLE_PCLMUL) && defined(__VPCLMULQDQ__) && defined(__AVX2__)
    inline __m256i paxosMulGF128(__m256i x, __m256i y)
    {
        auto lo = _mm256_clmulepi64_epi128(x, y, 0x00);
        auto hi = _mm256_clmulepi64_epi128(x, y, 0x11);
        auto mid = _mm256_xor_si256(_mm256_clmulepi64_epi128(x, y, 0x01),
                                   _mm256_clmulepi64_epi128(x, y, 0x10));
        lo = _mm256_xor_si256(lo, _mm256_slli_si256(mid, 8));
        hi = _mm256_xor_si256(hi, _mm256_srli_si256(mid, 8));
        const auto modulus = _mm256_set1_epi64x(0x87);
        auto t = _mm256_clmulepi64_epi128(hi, modulus, 0x01);
        lo = _mm256_xor_si256(lo, _mm256_slli_si256(t, 8));
        hi = _mm256_xor_si256(hi, _mm256_srli_si256(t, 8));
        return _mm256_xor_si256(lo, _mm256_clmulepi64_epi128(hi, modulus, 0x00));
    }
#endif

    // Evaluate p[0]*x + ... + p[count-1]*x^count, count >= 1.
    // Horner uses count multiplications instead of 2*count-1.
    inline block paxosDenseGF128(block x, const block* p, u64 count)
    {
        block a = p[count-1];
        for (u64 j=count-1; j; --j) a = a.gf128Mul(x) ^ p[j-1];
        return a.gf128Mul(x);
    }

    inline void paxosDenseGF128Eight(const block* x, const block* p, u64 count, block* out)
    {
#if !defined(VOLEPSI_PAXOS_FORCE_SCALAR) && defined(OC_ENABLE_PCLMUL) && defined(__VPCLMULQDQ__) && defined(__AVX512F__) && defined(__AVX512BW__)
        auto x0 = _mm512_loadu_si512(x), x1 = _mm512_loadu_si512(x+4);
        auto a = _mm512_broadcast_i32x4(p[count-1]), b = a;
        for (u64 j=count-1; j; --j)
        {
            auto c = _mm512_broadcast_i32x4(p[j-1]);
            a = _mm512_xor_si512(paxosMulGF128(a, x0), c);
            b = _mm512_xor_si512(paxosMulGF128(b, x1), c);
        }
        _mm512_storeu_si512(out, paxosMulGF128(a, x0));
        _mm512_storeu_si512(out+4, paxosMulGF128(b, x1));
#elif !defined(VOLEPSI_PAXOS_FORCE_SCALAR) && defined(OC_ENABLE_PCLMUL) && defined(__VPCLMULQDQ__) && defined(__AVX2__)
        auto x0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x));
        auto x1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x+2));
        auto x2 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x+4));
        auto x3 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(x+6));
        auto a = _mm256_broadcastsi128_si256(p[count-1]), b=a, c=a, d=a;
        for (u64 j=count-1; j; --j)
        {
            auto v = _mm256_broadcastsi128_si256(p[j-1]);
            a = _mm256_xor_si256(paxosMulGF128(a, x0), v);
            b = _mm256_xor_si256(paxosMulGF128(b, x1), v);
            c = _mm256_xor_si256(paxosMulGF128(c, x2), v);
            d = _mm256_xor_si256(paxosMulGF128(d, x3), v);
        }
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), paxosMulGF128(a, x0));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out+2), paxosMulGF128(b, x1));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out+4), paxosMulGF128(c, x2));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out+6), paxosMulGF128(d, x3));
#else
        block a=p[count-1],b=a,c=a,d=a,e=a,f=a,g=a,h=a;
        for (u64 j=count-1; j; --j)
        {
            a=a.gf128Mul(x[0])^p[j-1]; b=b.gf128Mul(x[1])^p[j-1];
            c=c.gf128Mul(x[2])^p[j-1]; d=d.gf128Mul(x[3])^p[j-1];
            e=e.gf128Mul(x[4])^p[j-1]; f=f.gf128Mul(x[5])^p[j-1];
            g=g.gf128Mul(x[6])^p[j-1]; h=h.gf128Mul(x[7])^p[j-1];
        }
        out[0]=a.gf128Mul(x[0]); out[1]=b.gf128Mul(x[1]);
        out[2]=c.gf128Mul(x[2]); out[3]=d.gf128Mul(x[3]);
        out[4]=e.gf128Mul(x[4]); out[5]=f.gf128Mul(x[5]);
        out[6]=g.gf128Mul(x[6]); out[7]=h.gf128Mul(x[7]);
#endif
    }
}
