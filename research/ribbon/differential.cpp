#include "Ribbon.h"
#include "BaselineRibbon.h" // Generated from the hashed, preserved baseline.
#include <iostream>

template<unsigned W> void compare(std::size_t n) {
    oc::PRNG rng(oc::toBlock(n+W));
    std::vector<oc::block> keys(n),values(n),oldCells;
    rng.get(keys.data(),n);rng.get(values.data(),n);
    ribbon::Buffer<oc::block> cells;
    auto seed=rng.get<oc::block>(),freeSeed=rng.get<oc::block>();
    std::uint32_t m=std::uint32_t(n*1.4)+W;
    ribbon::Ribbon<W> current(m,seed);
    ribbon_baseline::Ribbon<W> previous(m,seed);
    ribbon::Timings now;
    ribbon_baseline::Timings before;
    bool a=current.encode(keys,values,cells,freeSeed,now);
    bool b=previous.encode(keys,values,oldCells,freeSeed,before);
    if(a!=b || !a || cells.size()!=oldCells.size()) throw std::runtime_error("encode status mismatch");
    for(std::size_t i=0;i<cells.size();++i)
        if(cells[i]!=oldCells[i]) throw std::runtime_error("encoded table differs from baseline");
    std::vector<oc::block> out(n);
    for(auto mode:{ribbon::Order::sorted,ribbon::Order::shared}) {
        current.decode(keys,out,cells,mode,now);
        for(std::size_t i=0;i<n;++i)
            if(out[i]!=values[i]) throw std::runtime_error("large member decode mismatch");
    }
    // Independent nonmembers exercise equality with the preserved decoder,
    // including arbitrary values outside the encoded set.
    rng.get(keys.data(),n);
    std::vector<oc::block> reference(n);
    previous.decode(keys,reference,oldCells,ribbon_baseline::Order::sorted,before);
    for(auto mode:{ribbon::Order::shared}) {
        current.decode(keys,out,cells,mode,now);
        if(out!=reference) throw std::runtime_error("large nonmember decode mismatch");
    }
    std::cout<<"IDENTICAL width="<<W<<" n="<<n<<" columns="<<m<<'\n';
}
int main() {
    try {
        for(auto n:{17u,513u,262147u}) {
            compare<80>(n);compare<112>(n);compare<128>(n);compare<192>(n);
        }
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
