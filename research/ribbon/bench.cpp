#include "Ribbon.h"
#include <volePSI/Paxos.h>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>

using namespace ribbon;
using oc::span;
static void require(bool ok, const char* reason) {
    if(!ok) throw std::runtime_error(reason);
}
static void print(const char* kind,unsigned width,double overhead,unsigned trial,
                  std::size_t n,std::size_t m,const char* mode,const Timings& t) {
    std::cout<<kind<<','<<width<<','<<overhead<<','<<trial<<','<<n<<','<<m<<','<<mode
             <<','<<t.init<<','<<t.starts<<','<<t.sort<<','<<t.coefficients
             <<','<<t.eliminate<<','<<t.backsolve<<','<<t.total<<','<<t.additions<<'\n'<<std::flush;
}
template<unsigned W> void test() {
    oc::PRNG rng(oc::toBlock(42));
    for(std::size_t n:{1,7,8,9,37,257,1024}) {
        Ribbon<W> r(n*2+W, rng.get<block>());
        std::vector<block> keys(n),values(n),out(n),queries(n);
        Buffer<block> cells;
        rng.get(keys.data(),n);rng.get(values.data(),n);rng.get(queries.data(),n);
        Timings enc;
        require(r.encode(keys,values,cells,rng.get<block>(),enc),"test encode failed");
        for(auto mode:{Order::sorted,Order::bucket,Order::direct,Order::preparedSorted,Order::preparedBucket,Order::shared}) {
            Timings dec;
            r.decode(keys,out,cells,mode,dec);
            for(std::size_t i=0;i<n;++i) {
                require(out[i]==values[i],"member decode failed");
                require(r.referenceDecode(keys[i],cells)==values[i],"scalar member decode failed");
            }
            r.decode(queries,out,cells,mode,dec);
            for(std::size_t i=0;i<n;++i)
                require(out[i]==r.referenceDecode(queries[i],cells),"scalar nonmember decode failed");
        }
        if(n>1) {
            keys[1]=keys[0]; values[1]=values[0];
            Timings failed;
            require(!r.encode(keys,values,cells,rng.get<block>(),failed),"duplicate rank failure missed");
        }
    }
    // Independent mask/dot checks cover word boundaries and padding loads.
    std::vector<block> data(W+16);
    rng.get(data.data(),data.size());
    for(unsigned k=0;k<W;++k) {
        Mask<W> m{}; m.v[k/64]=std::uint64_t(1)<<(k%64);
        require(dot(m,data.data())==data[k],"single-bit SIMD mask failed");
        auto shift=m.normalize();
        require(shift==k && m.v[0]==1,"mask normalize failed");
    }
    for(unsigned i=0;i<1000;++i) {
        Mask<W> m;
        for(auto& x:m.v) x=rng.get<std::uint64_t>();
        if constexpr(W%64) m.v.back()&=(std::uint64_t(1)<<(W%64))-1;
        require(dot(m,data.data()+3)==scalarDot(m,data.data()+3),"random SIMD dot failed");
        require(dot<W,true>(m,data.data()+3)==scalarDot(m,data.data()+3),"forwarding SIMD dot failed");
    }
#if defined(__GFNI__) && defined(__AVX512VBMI__)
    // Independent one-hot and random masks cover every shift and word boundary.
    Buffer<block> gfniCells(W+32);
    rng.get(gfniCells.data(),gfniCells.size());
    Buffer<block> transformed(gfniCells.size());
    for(unsigned i=0;i<gfniCells.size();i+=8)
        gfniTableChunk(gfniCells.data()+i,transformed.data()+i);
    std::array<Mask<W>,8> gm;
    std::array<Entry,8> ge;
    std::array<block,8> gv;
    for(unsigned offset=0;offset<=32;++offset) for(unsigned bit=0;bit<=W;++bit) {
        for(unsigned q=0;q<8;++q) {
            gm[q]={};
            if(bit<W) {
                auto selected=(bit+q)%W;
                gm[q].v[selected/64]=std::uint64_t(1)<<(selected%64);
            } else {
                for(auto& word:gm[q].v) word=rng.get<std::uint64_t>();
                if constexpr(W%64) gm[q].v.back()&=(std::uint64_t(1)<<(W%64))-1;
            }
            ge[q]={q?offset:0,7-q};
        }
        gfniDot(gm.data(),ge.data(),transformed.data(),gv.data());
        for(unsigned q=0;q<8;++q)
            require(gv[ge[q].index]==scalarDot(gm[q],gfniCells.data()+ge[q].start),"GFNI shared dot failed");
    }

#endif
}

#ifndef RIBBON_TEST_ONLY
template<unsigned W> void bench(std::size_t n,double overhead,unsigned trials,const std::string& selection) {
    std::size_t m=std::ceil(n*overhead);
    oc::PRNG input(oc::toBlock(20261115));
    std::vector<block> keys(n),values(n),queries(n),out(n);
    input.get(keys.data(),n);input.get(values.data(),n);input.get(queries.data(),n);
    std::copy_n(keys.data(),n/2,queries.data());
    for(unsigned trial=0;trial<trials;++trial) {
        auto wall=Clock::now();
        Buffer<block> cells; // Match Baxos: fresh output allocation each trial.
        Ribbon<W> r(m,oc::toBlock(trial+100));
        Timings enc;
        require(r.encode(keys,values,cells,oc::toBlock(trial+200),enc),"benchmark rank failure");
        enc.total=elapsed(wall); // Include workspace destruction and key setup.
        print("encode",W,overhead,trial,n,m,"sorted",enc);
        if(trial==0) {
            std::ifstream memory("/proc/self/smaps_rollup");std::string line;
            while(std::getline(memory,line))
                if(line.starts_with("AnonHugePages:")) std::cerr<<line<<'\n';
        }
        Timings verify;
        r.decode(keys,out,cells,Order::direct,verify);
        for(std::size_t i=0;i<n;++i) require(out[i]==values[i],"full member check failed");
        for(auto mode:{Order::sorted,Order::bucket,Order::direct,Order::preparedSorted,Order::preparedBucket,Order::shared}) {
            if(selection=="sorted-only" && mode!=Order::sorted) continue;
            if(selection=="shared-only" && mode!=Order::shared) continue;
            Timings dec;
            wall=Clock::now();
            r.decode(queries,out,cells,mode,dec);
            dec.total=elapsed(wall);
            for(std::size_t i=0;i<n/2;++i) require(out[i]==values[i],"mixed member check failed");
            for(std::size_t i=n/2;i<n;i+=std::max<std::size_t>(1,n/1024))
                require(out[i]==r.referenceDecode(queries[i],cells),"nonmember reference failed");
            print("decode",W,overhead,trial,n,m,mode==Order::shared?"shared":mode==Order::sorted?"sorted":mode==Order::bucket?"bucket":mode==Order::direct?"direct":mode==Order::preparedSorted?"prepared_sorted":"prepared_bucket",dec);
        }
    }
}

void baxos(std::size_t n,unsigned trials) {
    oc::PRNG input(oc::toBlock(20261115));
    std::vector<block> keys(n),values(n),queries(n),out(n);
    input.get(keys.data(),n);input.get(values.data(),n);input.get(queries.data(),n);
    std::copy_n(keys.data(),n/2,queries.data());
    for(unsigned trial=0;trial<trials;++trial) {
        volePSI::Baxos b;
        Timings enc,dec;
        auto total=Clock::now(), t=total;
        b.init(n,1<<14,3,40,volePSI::PaxosParam::GF128,oc::toBlock(trial+100));
        std::vector<block> cells(b.size());
        oc::PRNG rng(oc::toBlock(trial+200));
        enc.init=elapsed(t);t=Clock::now();
        b.solve<block>(keys,values,cells,&rng,1);
        enc.eliminate=elapsed(t);enc.total=elapsed(total);
        print("baxos_encode",0,double(b.size())/n,trial,n,b.size(),"native",enc);
        b.decode<block>(keys,out,cells,1);
        for(std::size_t i=0;i<n;++i) require(out[i]==values[i],"Baxos member check failed");
        t=Clock::now();
        b.decode<block>(queries,out,cells,1);
        dec.total=elapsed(t);dec.coefficients=dec.total;
        for(std::size_t i=0;i<n/2;++i) require(out[i]==values[i],"Baxos query check failed");
        print("baxos_decode",0,double(b.size())/n,trial,n,b.size(),"native",dec);
    }
}
#endif
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="test") {
            test<16>();test<32>();test<48>();test<64>();test<80>();test<112>();test<128>();test<144>();test<192>();test<320>();test<640>();
            std::cout<<"PASS: members, nonmembers, tails, zero residuals, SIMD/scalar dots\n";
            return 0;
        }
#ifndef RIBBON_TEST_ONLY
        if(argc<5) throw std::runtime_error("usage: bench baxos|WIDTH LOG_N OVERHEAD TRIALS");
        unsigned logn=std::stoul(argv[2]);
        if(logn>26) throw std::runtime_error("benchmark log_n must be <=26");
        std::size_t n=std::size_t(1)<<logn;
        double overhead=std::stod(argv[3]);unsigned trials=std::stoul(argv[4]);
        std::string selection=argc>5?argv[5]:"all";
        require(selection=="all" || selection=="sorted-only" || selection=="shared-only","unknown decode mode");
        std::cout<<std::fixed<<std::setprecision(5)
                 <<"kind,width,overhead,trial,n,m,order,init_ms,start_hash_ms,sort_ms,coefficient_or_query_ms,eliminate_ms,backsolve_ms,total_ms,additions\n";
        if(std::string(argv[1])=="baxos") baxos(n,trials);
        else switch(std::stoul(argv[1])) {
        case 64:bench<64>(n,overhead,trials,selection);break;
        case 80:bench<80>(n,overhead,trials,selection);break;
        case 112:bench<112>(n,overhead,trials,selection);break;
        case 128:bench<128>(n,overhead,trials,selection);break;
        case 144:bench<144>(n,overhead,trials,selection);break;
        case 192:bench<192>(n,overhead,trials,selection);break;
        case 256:bench<256>(n,overhead,trials,selection);break;
        case 320:bench<320>(n,overhead,trials,selection);break;
        case 384:bench<384>(n,overhead,trials,selection);break;
        case 512:bench<512>(n,overhead,trials,selection);break;
        case 640:bench<640>(n,overhead,trials,selection);break;
        default:throw std::runtime_error("unsupported width");
        }
#endif
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
