#include <volePSI/Paxos.h>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <fstream>
using namespace volePSI;
using Clock=std::chrono::steady_clock;
double ms(Clock::time_point t) {return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
int main(int argc,char** argv) {
 try {
    unsigned logn=argc>1?std::stoul(argv[1]):20,trials=argc>2?std::stoul(argv[2]):8;
    std::string mode=argc>3?argv[3]:"both";
    std::size_t n=std::size_t(1)<<logn;
    PRNG input(oc::toBlock(20261115));
    std::vector<block> keys(n),values(n),queries(n),out(n);
    input.get(keys.data(),n);input.get(values.data(),n);input.get(queries.data(),n);
    std::copy_n(keys.data(),n/2,queries.data());
    std::cout<<"trial,n,m,dense,encode_ms,decode_ms\n"<<std::fixed<<std::setprecision(5);
    for(unsigned trial=0;trial<trials;++trial) {
        auto t=Clock::now();
        Baxos b;b.init(n,1<<14,3,40,PaxosParam::GF128,oc::toBlock(trial+100));
        std::vector<block> cells(b.size());PRNG rng(oc::toBlock(trial+200));
        b.solve<block>(keys,values,cells,&rng,1);
        double enc=ms(t),dec=0;
        if(mode!="encode") {
            t=Clock::now();b.decode<block>(queries,out,cells,1);dec=ms(t);
            for(std::size_t i=0;i<n/2;++i) if(out[i]!=values[i]) throw std::runtime_error("mixed member mismatch");
            if(mode=="decode") for(unsigned j=0;j<20;++j) b.decode<block>(queries,out,cells,1);
        }
        if(mode=="both") {
            b.decode<block>(keys,out,cells,1);
            if(out!=values) throw std::runtime_error("member mismatch");
        }
        std::cout<<trial<<','<<n<<','<<b.size()<<','<<b.mPaxosParam.mDenseSize<<','<<enc<<','<<dec<<'\n'<<std::flush;
        if(mode=="dump") {
            std::ofstream f(argv[4],std::ios::binary);
            f.write(reinterpret_cast<char*>(cells.data()),cells.size()*sizeof(block));
            b.decode<block>(queries,out,cells,1);
            f.write(reinterpret_cast<char*>(out.data()),out.size()*sizeof(block));
        }
    }
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
