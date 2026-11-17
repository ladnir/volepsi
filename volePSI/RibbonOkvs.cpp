#include "RibbonOkvs.h"
#include "RibbonKernels.h"
#include <cmath>
#include <limits>
#include <thread>
#if defined(_MSC_VER)
#define VOLEPSI_RIBBON_NOINLINE __declspec(noinline)
#else
#define VOLEPSI_RIBBON_NOINLINE __attribute__((noinline))
#endif

namespace volePSI
{
    namespace
    {
        // This grid only chooses a valid witness. It does not estimate or fit
        // the failure probability. Use log space and reserve two extra bits.
        RibbonParams witness(u64 n, u64 m, unsigned w)
        {
            RibbonParams best{n,m,w};
            const long double ln2=std::log(2.L), starts=m-w+1;
            if(starts<=n) return best;
            for(unsigned j=1;j<32;++j)
            {
                const long double rho=n/starts+(1-n/starts)*j/32;
                const long double mu=starts*rho;
                const long double delta=-mu+n+n*std::log(mu/n);
                for(unsigned k=1;k<128;++k)
                {
                    const long double theta=ln2*k/128, t=std::exp(theta);
                    const long double loga=rho*(t-1)-theta;
                    if(loga>=0) continue;
                    const long double b=(1-1/t)*std::exp(rho*(t/2-1));
                    const long double c=std::max(1.L,b/-std::expm1(loga));
                    const long double rank=std::log(mu*c)+rho*(t-1)-w*theta;
                    const long double hi=std::max(delta,rank),lo=std::min(delta,rank);
                    const long double bits=-(hi+std::log1p(std::exp(lo-hi)))/ln2;
                    if(bits>best.failureBits)
                    {
                        best.failureBits=static_cast<double>(bits);
                        best.rho=static_cast<double>(rho);
                        best.theta=static_cast<double>(theta);
                    }
                }
            }
            return best;
        }

        template<class F> decltype(auto) dispatch(unsigned width,F&& f)
        {
            switch(width)
            {
            case 64: return f(std::integral_constant<unsigned,64>{});
            case 128: return f(std::integral_constant<unsigned,128>{});
            case 256: return f(std::integral_constant<unsigned,256>{});
            case 512: return f(std::integral_constant<unsigned,512>{});
            case 1024: return f(std::integral_constant<unsigned,1024>{});
            default: throw std::logic_error("Ribbon is not initialized");
            }
        }
    }

    RibbonParams RibbonParams::select(u64 n,u64 ssp)
    {
        if(n>UINT32_MAX || ssp>1022)
            throw std::invalid_argument("Ribbon parameters exceed supported widths/index range");
        const u64 target=ssp+2;
        if(!n) return {0,64,64,1024,0,0};
        // For small sets, a single dense band avoids loose Poissonization.
        if(n<=256)
            for(unsigned w:{64,128,256,512,1024})
                if(n+target<=w) return {n,w,w,double(w-n),0,0};

        const long double L=(target+2)*std::log(2.L);
        const u64 slack=static_cast<u64>(std::ceil(2*std::sqrt(n*L)+2*L));
        RibbonParams best;
        // Prefer 25% overhead. For high requested security and intermediate
        // sizes, widen the table if no compiled band width certifies it.
        for(u64 scale:{4,5,6,8,12,16,32})
        {
            for(unsigned w:{64,128,256,512,1024})
            {
                const u64 m=std::max((5*n*scale+15)/16,n+w+slack);
                if(m>UINT32_MAX) continue;
                auto p=witness(n,m,w);
                if(p.failureBits>=target && (!best.width || m<best.columns)) best=p;
                if(best.width && m==(5*n*scale+15)/16) break;
            }
            if(best.width) break;
        }
        if(!best.width) throw std::invalid_argument("Ribbon cannot certify the requested failure bound with supported widths");
        return best;
    }

    void RibbonOkvs::init(u64 n,u64 ssp,block seed)
    {
        mParams=RibbonParams::select(n,ssp);
        mSeed=seed;
    }

    bool RibbonOkvs::labelKeyIndependent(block seed, block labelKey) const
    {
        if(seed == labelKey || !mParams.width) return false;
        oc::PRNG rng(seed);
        // Match Ribbon<W>'s start key followed by each coefficient key.
        for(unsigned i=0, count=1+(mParams.width+127)/128; i<count; ++i)
            if(rng.get<block>() == labelKey) return false;
        return true;
    }

    // These ordinary functions own all SIMD scratch; no scratch enters a
    // coroutine frame. Dispatch happens once per operation, outside hot loops.
    VOLEPSI_RIBBON_NOINLINE bool RibbonOkvs::solve(span<const block> keys,span<const block> values,
                           span<block> table,oc::PRNG* rng) const
    {
        if(keys.size()!=mParams.items || keys.size()!=values.size() || table.size()!=size())
            throw std::invalid_argument("Ribbon solve dimensions");
        return dispatch(mParams.width,[&](auto width) {
            ribbon_detail::Ribbon<decltype(width)::value> code(static_cast<uint32_t>(size()),mSeed);
            ribbon_detail::Timings timings;
            return code.encode(keys,values,table,rng,timings);
        });
    }

    VOLEPSI_RIBBON_NOINLINE void RibbonOkvs::decode(span<const block> keys,span<block> values,
                            span<const block> table,u64 numThreads) const
    {
        if(keys.size()!=values.size() || table.size()!=size() || keys.size()>UINT32_MAX)
            throw std::invalid_argument("Ribbon decode dimensions");
        const u64 threads=std::max<u64>(1,std::min<u64>(numThreads,keys.size()/4096));
        dispatch(mParams.width,[&](auto width) {
            ribbon_detail::Ribbon<decltype(width)::value> code(static_cast<uint32_t>(size()),mSeed);
            auto work=[&](u64 i) {
                auto begin=keys.size()*i/threads, end=keys.size()*(i+1)/threads;
                ribbon_detail::Timings timings;
                code.decode(keys.subspan(begin,end-begin),values.subspan(begin,end-begin),table,
                    end-begin<512?ribbon_detail::Order::direct:ribbon_detail::Order::shared,timings);
            };
            if(threads==1) { work(0); return; }
            std::vector<std::exception_ptr> errors(threads);
            auto guarded=[&](u64 i) { try {work(i);} catch(...) {errors[i]=std::current_exception();} };
            std::vector<std::jthread> workers;
            workers.reserve(threads-1);
            for(u64 i=1;i<threads;++i) workers.emplace_back(guarded,i);
            guarded(0);
            for(auto& worker:workers) worker.join();
            for(auto error:errors) if(error) std::rethrow_exception(error);
        });
    }
}

#undef VOLEPSI_RIBBON_NOINLINE
