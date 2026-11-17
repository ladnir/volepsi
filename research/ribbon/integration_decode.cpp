// Isolate the impact of external table allocation on the production decoder.
#include "volePSI/RibbonOkvs.h"
#include "volePSI/RibbonKernels.h"
#include <iostream>

int main()
{
    using namespace volePSI;
    constexpr u64 n=1<<20;
    RibbonOkvs code;code.init(n,40,oc::toBlock(123,45));
    PRNG rng(oc::toBlock(234,56));
    std::vector<block> keys(n),output(n),expected(n),ordinary(code.size());
    ribbon_detail::Buffer<block> advised(code.size());
    rng.get(keys.data(),n);rng.get(ordinary.data(),ordinary.size());
    std::copy(ordinary.begin(),ordinary.end(),advised.begin());
    code.decode(keys,expected,ordinary);
    std::cout<<"allocation,trial,decode_ms\n";
    for(bool huge:{false,true,false,true})
    for(unsigned trial=0;trial<8;++trial)
    {
        auto table=huge?span<const block>(advised):span<const block>(ordinary);
        const auto start=std::chrono::steady_clock::now();
        code.decode(keys,output,table);
        const auto end=std::chrono::steady_clock::now();
        if(output!=expected) throw std::runtime_error("decode allocation mismatch");
        std::cout<<(huge?"advised":"ordinary")<<','<<trial<<','
            <<std::chrono::duration<double,std::milli>(end-start).count()<<'\n';
    }
}
