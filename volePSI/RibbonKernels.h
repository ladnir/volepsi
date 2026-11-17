#pragma once

// Random-band OKVS kernels. The rank bound models start/coefficient hashes
// as independent random functions; AES hashing is the concrete instantiation.
// Rows have fully random coefficients, including the leading bit.
#include <cryptoTools/Crypto/AES.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#if defined(__AVX2__) || defined(_MSC_VER) && defined(_M_X64)
#include <immintrin.h>
#endif
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef __linux__
#include <sys/mman.h>
#endif

namespace volePSI::ribbon_detail {
#ifndef RIBBON_DOT
#define RIBBON_DOT 2
#endif
#ifndef RIBBON_HUGE_PAGES
#define RIBBON_HUGE_PAGES 1
#endif
#ifndef RIBBON_FORWARDING
#define RIBBON_FORWARDING 1
#endif
#ifndef RIBBON_RADIX_BITS
#define RIBBON_RADIX_BITS 11
#endif
#ifndef RIBBON_BATCH
#define RIBBON_BATCH 8
#endif
#ifndef RIBBON_DIRECT_PIVOTS
#define RIBBON_DIRECT_PIVOTS 0
#endif
// Large scratch buffers are aligned for optional transparent huge pages.
// Default construction deliberately leaves trivial elements uninitialized;
// every read is preceded by an explicit write. No global VM setting changes.
template<class T> struct BufferAllocator {
    using value_type=T;
    BufferAllocator()=default;
    template<class U> BufferAllocator(const BufferAllocator<U>&) noexcept {}
    static std::size_t alignment(std::size_t bytes) {
        return RIBBON_HUGE_PAGES && bytes>=2*1024*1024 ? 2*1024*1024 : std::max<std::size_t>(16,alignof(T));
    }
    T* allocate(std::size_t n) {
        if(n>std::size_t(-1)/sizeof(T)) throw std::bad_array_new_length();
        auto bytes=n*sizeof(T),align=alignment(bytes);
        auto p=static_cast<T*>(::operator new(bytes,std::align_val_t(align)));
#if defined(__linux__) && RIBBON_HUGE_PAGES
        if(align==2*1024*1024) (void)madvise(p,bytes/(2*1024*1024)*(2*1024*1024),MADV_HUGEPAGE);
#endif
        return p;
    }
    void deallocate(T* p,std::size_t n) noexcept {::operator delete(p,std::align_val_t(alignment(n*sizeof(T))));}
    template<class U,class... Args> void construct(U* p,Args&&... args) {
        if constexpr(sizeof...(Args)==0) ::new(static_cast<void*>(p)) U;
        else ::new(static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
    template<class U> bool operator==(const BufferAllocator<U>&) const noexcept {return true;}
};
template<class T> using Buffer=std::vector<T,BufferAllocator<T>>;
using oc::block;
template<bool Writing,class T> inline void prefetch(const T* p) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(p,Writing?1:0,3);
#else
    (void)p;
#endif
}
using Clock = std::chrono::steady_clock;
inline double elapsed(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now()-t).count();
}
struct Timings {
    double init=0, starts=0, sort=0, coefficients=0, eliminate=0, backsolve=0, total=0;
    std::uint64_t additions=0;
};
struct Entry { std::uint32_t start, index; };
enum class Order { sorted, bucket, direct, shared };

template<class Allocator>
[[gnu::noinline]] inline void orderEntries(std::vector<Entry,Allocator>& entries, Order mode) {
    if(mode==Order::direct) return;
    std::vector<Entry,Allocator> temp(entries.size());
    if(mode==Order::bucket) {
        std::uint32_t max=0;
        for(auto e:entries) max=std::max(max,e.start>>12);
        std::vector<std::size_t> count(max+1);
        for(auto e:entries) ++count[e.start>>12];
        std::size_t sum=0;
        for(auto& c:count) { auto old=c; c=sum; sum+=old; }
        for(auto e:entries) temp[count[e.start>>12]++]=e;
        entries.swap(temp);
        return;
    }
    // Small radix buckets keep active scatter destinations in L1.
    std::uint32_t max=0;
    for(auto e:entries) max=std::max(max,e.start);
    constexpr unsigned buckets=1u<<RIBBON_RADIX_BITS;
    if constexpr(RIBBON_RADIX_BITS==11) {
        if(max<(1u<<22) && entries.size()<=UINT32_MAX) {
            // Digit histograms are independent of ordering. Count both in one
            // scan; 32-bit counters keep the pair within 16 KiB of L1.
            std::array<std::uint32_t,2048> low{},high{};
            for(auto e:entries) {++low[e.start&2047];++high[e.start>>11];}
            std::uint32_t a=0,b=0;
            for(unsigned j=0;j<2048;++j) {
                auto x=low[j],y=high[j];low[j]=a;high[j]=b;a+=x;b+=y;
            }
            for(auto e:entries) temp[low[e.start&2047]++]=e;
            for(auto e:temp) entries[high[e.start>>11]++]=e;
            return;
        }
    }
    for(unsigned shift=0;shift<32 && (max>>shift);shift+=RIBBON_RADIX_BITS) {
        std::array<std::size_t,buckets> count{};
        for(auto e:entries) ++count[(e.start>>shift)&(buckets-1)];
        std::size_t sum=0;
        for(auto& c:count) { auto old=c;c=sum;sum+=old; }
        for(auto e:entries) temp[count[(e.start>>shift)&(buckets-1)]++]=e;
        entries.swap(temp);
    }
}

template<unsigned W> struct Mask {
    static_assert(W && W % 16 == 0);
    static constexpr unsigned words=(W+63)/64;
    std::array<std::uint64_t, words> v;
    bool empty() const {
        std::uint64_t z=0;
        #pragma GCC unroll 16
        for(unsigned i=0;i<words;++i) z|=v[i];
        return z==0;
    }
    void add(const Mask& other) {
        #pragma GCC unroll 16
        for(unsigned i=0;i<words;++i) v[i]^=other.v[i];
    }
    // Caller checks nonzero. The common path shifts by <64 bits.
    unsigned normalize() {
        unsigned whole=0;
        while(v[0]==0) {
            #pragma GCC unroll 16
            for(unsigned i=0;i+1<words;++i) v[i]=v[i+1];
            v[words-1]=0;
            whole+=64;
        }
        unsigned bits=std::countr_zero(v[0]);
        if(bits) {
            #pragma GCC unroll 16
            for(unsigned i=0;i+1<words;++i)
                v[i]=(v[i]>>bits)|(v[i+1]<<(64-bits));
            v[words-1]>>=bits;
        }
        return whole+bits;
    }
};

#if defined(__AVX512F__) && defined(__AVX512VL__) && defined(__AVX512DQ__) && defined(__BMI2__) && !defined(VOLEPSI_RIBBON_PORTABLE)
inline block reduce(__m512i x) {
    auto lo=_mm256_xor_si256(_mm512_castsi512_si256(x), _mm512_extracti64x4_epi64(x,1));
    return block(_mm_xor_si128(_mm256_castsi256_si128(lo),_mm256_extracti128_si256(lo,1)));
}
template<unsigned W,bool Forwarding=false> inline block dot(const Mask<W>& mask, const block* cells) {
    [[maybe_unused]] static constexpr unsigned char lane[16]={0,3,12,15,48,51,60,63,192,195,204,207,240,243,252,255};
    __m512i a=_mm512_setzero_si512(), b=a, c=a, d=a;
    #pragma GCC unroll 16
    for(unsigned k=0;k<Mask<W>::words;++k) {
        auto bits=mask.v[k];
#if RIBBON_DOT == 2
        #pragma GCC unroll 2
        for(unsigned half=0;half<2 && k*64+half*32<W;++half) {
            auto spread=_pdep_u64(bits>>(half*32),0x5555555555555555ULL);
            auto masks=spread|(spread<<1);
            #pragma GCC unroll 2
            for(unsigned j=0;j<32 && k*64+half*32+j<W;j+=16) {
                auto p=cells+k*64+half*32+j;
                if(!Forwarding || k || half || j)
                    a=_mm512_mask_xor_epi64(a,__mmask8(masks>>(2*j)),a,_mm512_loadu_si512(p));
                b=_mm512_mask_xor_epi64(b,__mmask8(masks>>(2*j+8)),b,_mm512_loadu_si512(p+4));
                c=_mm512_mask_xor_epi64(c,__mmask8(masks>>(2*j+16)),c,_mm512_loadu_si512(p+8));
                d=_mm512_mask_xor_epi64(d,__mmask8(masks>>(2*j+24)),d,_mm512_loadu_si512(p+12));
            }
        }
#else
        #pragma GCC unroll 4
        for(unsigned j=0;j<64 && k*64+j<W;j+=16) {
#if RIBBON_DOT == 1
            a=_mm512_mask_xor_epi64(a,lane[(bits>>j)&15],a,_mm512_loadu_si512(cells+k*64+j));
            b=_mm512_mask_xor_epi64(b,lane[(bits>>(j+4))&15],b,_mm512_loadu_si512(cells+k*64+j+4));
            c=_mm512_mask_xor_epi64(c,lane[(bits>>(j+8))&15],c,_mm512_loadu_si512(cells+k*64+j+8));
            d=_mm512_mask_xor_epi64(d,lane[(bits>>(j+12))&15],d,_mm512_loadu_si512(cells+k*64+j+12));
#else
            a=_mm512_xor_si512(a,_mm512_maskz_loadu_epi64(lane[(bits>>j)&15],cells+k*64+j));
            b=_mm512_xor_si512(b,_mm512_maskz_loadu_epi64(lane[(bits>>(j+4))&15],cells+k*64+j+4));
            c=_mm512_xor_si512(c,_mm512_maskz_loadu_epi64(lane[(bits>>(j+8))&15],cells+k*64+j+8));
            d=_mm512_xor_si512(d,_mm512_maskz_loadu_epi64(lane[(bits>>(j+12))&15],cells+k*64+j+12));
#endif
        }
#endif
    }
    auto result=reduce(_mm512_xor_si512(_mm512_xor_si512(a,b),_mm512_xor_si512(c,d)));
    if constexpr(Forwarding && RIBBON_DOT==2) {
        // Exact-width loads can forward the most recent 16-byte stores.
        // A 64-byte load overlapping those stores incurs a forwarding stall.
        __m128i prefix=_mm_setzero_si128();
        #pragma GCC unroll 4
        for(unsigned j=0;j<4;++j)
            prefix=_mm_mask_xor_epi64(prefix,__mmask8(((mask.v[0]>>j)&1)*3),prefix,
                                     _mm_loadu_si128(reinterpret_cast<const __m128i*>(cells+j)));
        result^=block(prefix);
    }
    return result;
}
#else
template<unsigned W,bool Forwarding=false> inline block dot(const Mask<W>& mask, const block* cells) {
    block result=oc::ZeroBlock;
    for(unsigned k=0;k<Mask<W>::words;++k) {
        auto bits=mask.v[k];
        while(bits) {
            result^=cells[k*64+std::countr_zero(bits)];
            bits&=bits-1;
        }
    }
    return result;
}
#endif
#if defined(__GFNI__) && defined(__AVX512VBMI__) && !defined(VOLEPSI_RIBBON_PORTABLE)
// Transpose each 8x8 bit matrix within its 64-bit lane. GFNI numbers
// matrix rows in reverse byte order, so reverse the bytes after transposing.
inline __m512i gfniMatrix(__m512i x) {
    auto t=_mm512_and_si512(_mm512_xor_si512(x,_mm512_srli_epi64(x,7)),_mm512_set1_epi64(0x00aa00aa00aa00aaULL));
    x=_mm512_xor_si512(x,_mm512_xor_si512(t,_mm512_slli_epi64(t,7)));
    t=_mm512_and_si512(_mm512_xor_si512(x,_mm512_srli_epi64(x,14)),_mm512_set1_epi64(0x0000cccc0000ccccULL));
    x=_mm512_xor_si512(x,_mm512_xor_si512(t,_mm512_slli_epi64(t,14)));
    t=_mm512_and_si512(_mm512_xor_si512(x,_mm512_srli_epi64(x,28)),_mm512_set1_epi64(0x00000000f0f0f0f0ULL));
    x=_mm512_xor_si512(x,_mm512_xor_si512(t,_mm512_slli_epi64(t,28)));
    return _mm512_shuffle_epi8(x,_mm512_set4_epi64(0x08090a0b0c0d0e0fULL,0x0001020304050607ULL,0x08090a0b0c0d0e0fULL,0x0001020304050607ULL));
}
inline void gfniTableChunk(const block* cells,block* result) {
    alignas(64) std::array<unsigned char,64> low,high;
    for(unsigned byte=0;byte<8;++byte) for(unsigned q=0;q<8;++q) {
        low[byte*8+q]=q*16+byte;high[byte*8+q]=q*16+byte+8;
    }
    auto il=_mm512_load_si512(low.data()),ih=_mm512_load_si512(high.data());
    auto a=_mm512_loadu_si512(cells),b=_mm512_loadu_si512(cells+4);
    _mm512_storeu_si512(result,gfniMatrix(_mm512_permutex2var_epi8(a,il,b)));
    _mm512_storeu_si512(result+4,gfniMatrix(_mm512_permutex2var_epi8(a,ih,b)));
}
// Eight queries share each pair of table loads and two GFNI instructions.
// All starts must be within 32 cells of the eight-cell-aligned anchor.
// The table pointer starts at that anchor and covers W+32 cells.
template<unsigned W> inline void gfniDot(const Mask<W>* masks,const Entry* entries,
                                        const block* table,block* values) {
    auto anchor=entries[0].start&~7u;
    alignas(64) std::uint64_t shifted[Mask<W+32>::words][8];
    #pragma GCC unroll 8
    for(unsigned q=0;q<8;++q) {
        auto shift=entries[q].start-anchor;
        #pragma GCC unroll 16
        for(unsigned k=0;k<Mask<W+32>::words;++k) {
            auto lo=k<Mask<W>::words?masks[q].v[k]:0;
            auto hi=k?masks[q].v[k-1]:0;
            shifted[k][q]=(lo<<shift)|(shift?hi>>(64-shift):0);
        }
    }
    __m512i a=_mm512_setzero_si512(),b=a;
    auto pack=_mm512_set1_epi64(0x3830282018100800ULL);
    #pragma GCC unroll 16
    for(unsigned k=0;k<Mask<W+32>::words;++k) {
        auto coefficients=_mm512_load_si512(shifted[k]);
        #pragma GCC unroll 8
        for(unsigned j=0;j<64 && k*64+j<W+32;j+=8) {
            auto input=_mm512_permutexvar_epi8(pack,coefficients);
            const auto* p=table+k*64+j;
            a=_mm512_xor_si512(a,_mm512_gf2p8affine_epi64_epi8(input,_mm512_loadu_si512(p),0));
            b=_mm512_xor_si512(b,_mm512_gf2p8affine_epi64_epi8(input,_mm512_loadu_si512(p+4),0));
            coefficients=_mm512_srli_epi64(coefficients,8);
        }
    }
    alignas(64) std::array<unsigned char,64> low,high;
    for(unsigned q=0;q<4;++q) for(unsigned byte=0;byte<16;++byte) {
        low[q*16+byte]=byte*8+q;high[q*16+byte]=byte*8+q+4;
    }
    auto x=_mm512_permutex2var_epi8(a,_mm512_load_si512(low.data()),b);
    auto y=_mm512_permutex2var_epi8(a,_mm512_load_si512(high.data()),b);
    #pragma GCC unroll 4
    for(unsigned q=0;q<4;++q) {
        values[entries[q].index]=block(_mm512_extracti32x4_epi32(x,q));
        values[entries[q+4].index]=block(_mm512_extracti32x4_epi32(y,q));
    }
}

#endif

template<unsigned W> block scalarDot(const Mask<W>& mask, const block* cells) {
    block r=oc::ZeroBlock;
    for(unsigned k=0;k<Mask<W>::words;++k) {
        auto bits=mask.v[k];
        while(bits) {
            r^=cells[k*64+std::countr_zero(bits)];
            bits&=bits-1;
        }
    }
    return r;
}

template<unsigned W> class Ribbon {
public:
    using M=Mask<W>;
    struct Row { M mask; block value; };
    struct Query { Entry entry; M mask; };
    std::uint32_t columns, starts;
    oc::AES startHash;
    std::array<oc::AES,(W+127)/128> coefficientHash;
    explicit Ribbon(std::uint32_t m, block seed) : columns(m), starts(m-W+1) {
        if(m<W) throw std::invalid_argument("Ribbon columns < width");
        oc::PRNG rng(seed);
        startHash.setKey(rng.get<block>());
        for(auto& h:coefficientHash) h.setKey(rng.get<block>());
    }

    std::uint32_t start(block key, block hashed) const {
        // Exact uniform reduction under ideal independent hashes. The rejection
        // branch is exceedingly rare, but avoids a statistical modulo-bias claim.
        std::uint64_t x=hashed.get<std::uint64_t>(0);
        std::uint64_t rejectionThreshold=(std::uint64_t(0)-starts)%starts;
        std::uint64_t attempt=0;
        while(x<rejectionThreshold) {
            hashed=startHash.hashBlock(key ^ oc::toBlock(++attempt,0x72656a656374ULL));
            x=hashed.get<std::uint64_t>(0);
        }
        return x%starts;
    }
    void makeEntries(oc::span<const block> keys, oc::span<Entry> entries) const {
        std::size_t i=0;
        for(;i+8<=keys.size();i+=8) {
            std::array<block,8> hashed;
            startHash.template hashBlocks<8>(keys.data()+i,hashed.data());
            #pragma GCC unroll 8
            for(unsigned j=0;j<8;++j) entries[i+j]={start(keys[i+j],hashed[j]),std::uint32_t(i+j)};
        }
        for(;i<keys.size();++i) entries[i]={start(keys[i],startHash.hashBlock(keys[i])),std::uint32_t(i)};
    }
    template<bool Writing,bool Grouped=false,class F> void coefficientBatches(oc::span<const block> keys,
                    oc::span<const Entry> entries,const block* valueBase,F&& consume) const {
        constexpr unsigned batch=RIBBON_BATCH;
        for(std::size_t i=0;i<entries.size();i+=batch) {
            if(i+64+batch<=entries.size()) {
                #pragma GCC unroll 32
                for(unsigned j=0;j<batch;++j) {
                    auto index=entries[i+64+j].index;
                    prefetch<false>(keys.data()+index);
                    prefetch<Writing>(valueBase+index);
                }
            }
            unsigned size=std::min<std::size_t>(batch,entries.size()-i);
            std::array<block,batch> input, hashed;
            if(size!=batch) std::fill(input.begin()+size,input.end(),oc::ZeroBlock);
            std::array<M,batch> masks;
            for(unsigned j=0;j<size;++j) input[j]=keys[entries[i+j].index];
            #pragma GCC unroll 8
            for(unsigned k=0;k<(W+127)/128;++k) {
                coefficientHash[k].template hashBlocks<batch>(input.data(),hashed.data());
                #pragma GCC unroll 32
                for(unsigned j=0;j<batch;++j) {
                    masks[j].v[2*k]=hashed[j].template get<std::uint64_t>(0);
                    if(2*k+1<M::words) masks[j].v[2*k+1]=hashed[j].template get<std::uint64_t>(1);
                }
            }
            for(unsigned j=0;j<size;++j) {
                if constexpr(W%64) masks[j].v.back()&=(std::uint64_t(1)<<(W%64))-1;
                if constexpr(!Grouped) consume(i+j,masks[j]);
            }
            if constexpr(Grouped) consume(i,masks.data(),size);
        }
    }
    // Generate all hash material while input keys are contiguous. The callback
    // is a template and inlines into the streaming decoder or record producer.
    template<class F> void queryBatches(oc::span<const block> keys,F&& consume) const {
        for(std::size_t i=0;i<keys.size();i+=8) {
            unsigned size=std::min<std::size_t>(8,keys.size()-i);
            std::array<block,8> tail{}, hash, hashedStart;
            const block* input=keys.data()+i;
            if(size!=8) { std::copy_n(input,size,tail.data()); input=tail.data(); }
            std::array<M,8> masks;
            startHash.template hashBlocks<8>(input,hashedStart.data());
            #pragma GCC unroll 8
            for(unsigned k=0;k<(W+127)/128;++k) {
                coefficientHash[k].template hashBlocks<8>(input,hash.data());
                #pragma GCC unroll 8
                for(unsigned j=0;j<8;++j) {
                    masks[j].v[2*k]=hash[j].template get<std::uint64_t>(0);
                    if(2*k+1<M::words) masks[j].v[2*k+1]=hash[j].template get<std::uint64_t>(1);
                }
            }
            std::array<Query,8> batch;
            for(unsigned j=0;j<size;++j) {
                if constexpr(W%64) masks[j].v.back()&=(std::uint64_t(1)<<(W%64))-1;
                batch[j]={{start(input[j],hashedStart[j]),std::uint32_t(i+j)},masks[j]};
            }
            consume(batch,size);
        }
    }
    bool encode(oc::span<const block> keys, oc::span<const block> values,
                oc::span<block> cells, oc::PRNG* rng, Timings& time) const {
        auto total=Clock::now(), t=total;
        Buffer<Entry> entries(keys.size());
        // Values live at their pivot column in the output table.
#if RIBBON_DIRECT_PIVOTS
        Buffer<M> pivots(columns);
        for(auto& pivot:pivots) pivot.v[0]=0;
#else
        Buffer<M> rows(keys.size());
        Buffer<std::uint32_t> pivots(columns,UINT32_MAX);
#endif
        if(rng) rng->get(cells.data(),columns);
        else std::fill(cells.begin(),cells.end(),oc::ZeroBlock);
        time.init=elapsed(t); t=Clock::now();
        makeEntries(keys,entries);
        time.starts=elapsed(t); t=Clock::now();
        orderEntries(entries,Order::sorted);
        time.sort=elapsed(t); t=Clock::now();
        bool success=true;
        coefficientBatches<false>(keys,entries,values.data(),[&](std::size_t i,const M& inputMask) {
            if(!success) return;
            M mask=inputMask;
            block value=values[entries[i].index];
            auto pos=entries[i].start;
            for(;;) {
                if(mask.empty()) {success=false;return;}
                pos+=mask.normalize();
                if(pos>=columns) throw std::logic_error("pivot outside band");
                auto& pivot=pivots[pos];
#if RIBBON_DIRECT_PIVOTS
                // A stored row is normalized, so its low bit is always one.
                if(pivot.v[0]==0) {pivot=mask;cells[pos]=value;break;}
                mask.add(pivot);
#else
                if(pivot==UINT32_MAX) {
                    pivot=std::uint32_t(i);rows[i]=mask;cells[pos]=value;break;
                }
                mask.add(rows[pivot]);
#endif
                value^=cells[pos];
                ++time.additions;
            }
        });
        if(!success) return false;
        // Includes coefficient hashing and elimination, now fused.
        time.eliminate=elapsed(t); t=Clock::now();
        for(std::size_t pos=columns;pos--;) {
#if RIBBON_DIRECT_PIVOTS
            auto& row=pivots[pos];
            if(row.v[0]==0) continue;
#else
            if(pivots[pos]==UINT32_MAX) continue;
            auto& row=rows[pivots[pos]];
#endif
            row.v[0]^=1;
            // Normalization shifts the band toward the end. Nonzero bits remain
            // within the table, but a full-width SIMD load might not.
            cells[pos]^=pos+W<=columns ? dot<W,bool(RIBBON_FORWARDING)>(row,cells.data()+pos)
                                      : scalarDot(row,cells.data()+pos);
        }
        time.backsolve=elapsed(t); time.total=elapsed(total);
        return true;
    }
    void decode(oc::span<const block> keys, oc::span<block> values,
                oc::span<const block> cells, Order mode, Timings& time) const {
        auto total=Clock::now(), t=total;
        if(mode==Order::direct) {
            queryBatches(keys,[&](const auto& batch,unsigned size) {
                for(unsigned j=0;j<size;++j) {
                    const auto* p=cells.data()+batch[j].entry.start;
                    #pragma GCC unroll 8
                    for(unsigned k=0;k<W;k+=16) prefetch<false>(p+k);
                }
                for(unsigned j=0;j<size;++j) {
                    const auto& q=batch[j];
                    values[q.entry.index]=dot(q.mask,cells.data()+q.entry.start);
                }
            });
            time.coefficients=elapsed(t);time.total=elapsed(total);return;
        }
        Buffer<Entry> entries(keys.size());
        time.init=elapsed(t); t=Clock::now();
        makeEntries(keys,entries);
        time.starts=elapsed(t); t=Clock::now();
        orderEntries(entries,mode);
        time.sort=elapsed(t); t=Clock::now();
#if defined(__GFNI__) && defined(__AVX512VBMI__) && !defined(VOLEPSI_RIBBON_PORTABLE)
        if(mode==Order::shared && W>=32) {
            constexpr unsigned ringSize=std::bit_ceil(W+32u);
            alignas(64) std::array<block,2*ringSize> cache;
            std::size_t next=0;
            coefficientBatches<true,true>(keys,entries,values.data(),[&](std::size_t i,const M* masks,unsigned size) {
                unsigned j=0;
                for(;j+8<=size;j+=8) {
                    auto anchor=entries[i+j].start&~7u;
                    if(entries[i+j+7].start-anchor<=32) {
                        next=std::max<std::size_t>(next,anchor);
                        for(;next<std::size_t(anchor)+W+32;next+=8) {
                            auto pos=next&(ringSize-1);
                            if(next+8<=columns) gfniTableChunk(cells.data()+next,cache.data()+pos);
                            else {
                                std::array<block,8> tail{};
                                if(next<columns) std::copy_n(cells.data()+next,columns-next,tail.data());
                                gfniTableChunk(tail.data(),cache.data()+pos);
                            }
                            // Mirror the ring so every band is contiguous.
                            std::copy_n(cache.data()+pos,8,cache.data()+pos+ringSize);
                        }
                        gfniDot(masks+j,entries.data()+i+j,cache.data()+(anchor&(ringSize-1)),values.data());
                    } else for(unsigned q=0;q<8;++q)
                        values[entries[i+j+q].index]=dot(masks[j+q],cells.data()+entries[i+j+q].start);
                }
                for(;j<size;++j) values[entries[i+j].index]=dot(masks[j],cells.data()+entries[i+j].start);
            });
        } else
#endif
        {
            coefficientBatches<true>(keys,entries,values.data(),[&](std::size_t i,const M& mask) {
                values[entries[i].index]=dot(mask,cells.data()+entries[i].start);
            });
        }
        time.coefficients=elapsed(t); time.total=elapsed(total);
    }
    block referenceDecode(block key, oc::span<const block> cells) const {
        M mask;
        for(unsigned k=0;k<(W+127)/128;++k) {
            auto hashed=coefficientHash[k].hashBlock(key);
            mask.v[2*k]=hashed.template get<std::uint64_t>(0);
            if(2*k+1<M::words) mask.v[2*k+1]=hashed.template get<std::uint64_t>(1);
        }
        if constexpr(W%64) mask.v.back()&=(std::uint64_t(1)<<(W%64))-1;
        return scalarDot(mask,cells.data()+start(key,startHash.hashBlock(key)));
    }
};
}
