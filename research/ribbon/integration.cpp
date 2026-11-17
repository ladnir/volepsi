#include "volePSI/RsPsi.h"
#include "tests/Ribbon_Tests.h"
#include "tests/RsOprf_Tests.h"
#include <cryptoTools/Network/Channel.h>
#include <macoro/when_all.h>
#include <macoro/sync_wait.h>
#include <chrono>
#include <iomanip>
#include <iostream>

using namespace volePSI;
using namespace oc;

double interval(const Timer& timer,const char* begin,const char* end)
{
    return std::chrono::duration<double,std::milli>(timer[end]-timer[begin]).count();
}
int main(int argc,char** argv)
{
    try {
        const std::string mode=argc>1?argv[1]:"test";
        if(mode=="test")
        {
            CLP cmd;
            RsOprf_seed_rejection_test(cmd);std::cout<<"PASS RsOprf_seed_rejection_test\n"<<std::flush;
            RsOprf_ribbon_seed_test(cmd);std::cout<<"PASS RsOprf_ribbon_seed_test\n"<<std::flush;
            RsOprf_salt_order_test(cmd);std::cout<<"PASS RsOprf_salt_order_test\n"<<std::flush;
            RsOprf_mal_test(cmd);std::cout<<"PASS RsOprf_mal_test\n"<<std::flush;
            Ribbon_okvs_test(cmd);std::cout<<"PASS Ribbon_okvs_test\n"<<std::flush;
            Ribbon_protocol_test(cmd);std::cout<<"PASS Ribbon_protocol_test\n"<<std::flush;
            Ribbon_failure_test(cmd);std::cout<<"PASS Ribbon_failure_test\n"<<std::flush;
            return 0;
        }
        if(mode=="params")
        {
            std::cout<<"n,ssp,columns,width,bits,rho,theta\n"<<std::setprecision(17);
            for(u64 ssp:{20,40,80,128,256})
            for(u64 n:{1,17,86,87,214,215,257,1024,4096,16384,65536,1048576,16777216,1073741824})
            {
                const auto p=RibbonParams::select(n,ssp);
                std::cout<<n<<','<<ssp<<','<<p.columns<<','<<p.width<<','<<p.failureBits<<','<<p.rho<<','<<p.theta<<'\n';
            }
            return 0;
        }
        const auto backend=parseOkvsType(mode);
        const u64 n=1ull<<(argc>2?std::stoul(argv[2]):20);
        const unsigned trials=argc>3?std::stoul(argv[3]):8;
        const bool malicious=argc>4?std::stoul(argv[4]):false;
        PRNG rng(toBlock(193,57));
        std::vector<block> send(n),recv(n);
        rng.get(send.data(),n);rng.get(recv.data(),n);
        // Verify a nonempty intersection outside the timed region.
        for(u64 i=0;i<n;i+=1024) send[i]=recv[i];
        std::cout<<"backend,malicious,trial,n,columns,wall_both_ms,r_encode_ms,r_decode_ms,s_decode_ms,r_sent,s_sent\n";
        for(unsigned trial=0;trial<trials;++trial)
        {
            // Fresh objects and real base OTs on every trial.
            RsPsiSender sender;RsPsiReceiver receiver;
            Timer st,rt;
            sender.init(n,n,40,rng.get<block>(),malicious,1);
            receiver.init(n,n,40,rng.get<block>(),malicious,1);
            sender.setOkvsType(backend);receiver.setOkvsType(backend);
            sender.setTimer(st);receiver.setTimer(rt);
            auto sockets=coproto::LocalAsyncSocket::makePair();
            const auto begin=std::chrono::steady_clock::now();
            auto result=macoro::sync_wait(macoro::when_all_ready(sender.run(send,sockets[0]),receiver.run(recv,sockets[1])));
            std::get<0>(result).result();std::get<1>(result).result();
            const auto end=std::chrono::steady_clock::now();
            if(receiver.mIntersection.size()!=(n+1023)/1024) throw RTE_LOC;
            std::sort(receiver.mIntersection.begin(),receiver.mIntersection.end());
            for(u64 i=0;i<receiver.mIntersection.size();++i) if(receiver.mIntersection[i]!=i*1024) throw RTE_LOC;
            std::cout<<mode<<','<<malicious<<','<<trial<<','<<n<<','<<sender.mSender.okvsSize()<<','
                <<std::chrono::duration<double,std::milli>(end-begin).count()<<','
                <<interval(rt,"RsOprfReceiver::receive-alloc","RsOprfReceiver::receive-solve")<<','
                <<interval(rt,"RsOprfReceiver::decode-begin","RsOprfReceiver::receive-decode")<<','
                <<interval(st,"RsOprfSender::eval-begin","RsOprfSender::eval-decode")<<','
                <<sockets[1].bytesSent()<<','<<sockets[0].bytesSent()<<'\n'<<std::flush;
        }
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
