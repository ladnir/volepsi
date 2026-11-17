#include "Ribbon_Tests.h"
#include "Common.h"
#include "volePSI/RsPsi.h"
#include <cryptoTools/Network/Channel.h>
#include <array>
#include <cmath>

using namespace volePSI;
using namespace oc;

namespace {
    // Independent, scalar row construction and evaluation. No shared mask,
    // sorting, elimination, or SIMD decoder implementation.
    block reference(block key,oc::span<const block> table,const RibbonParams& params,block seed)
    {
        PRNG prng(seed);
        AES startHash(prng.get<block>());
        const u64 starts=params.columns-params.width+1;
        u64 x=startHash.hashBlock(key).get<u64>(0),attempt=0;
        while(x<(u64(0)-starts)%starts)
            x=startHash.hashBlock(key^toBlock(++attempt,0x72656a656374ULL)).get<u64>(0);
        const u64 start=x%starts;
        block result=ZeroBlock;
        for(unsigned j=0;j<params.width;j+=128)
        {
            auto coefficients=AES(prng.get<block>()).hashBlock(key);
            for(unsigned bit=0;bit<128 && j+bit<params.width;++bit)
                if((coefficients.get<u64>(bit/64)>>(bit%64))&1) result^=table[start+j+bit];
        }
        return result;
    }
}

void Ribbon_okvs_test(const CLP&)
{
    PRNG rng(toBlock(457,871));
    for(u64 ssp:{20,40,80,128})
    for(u64 n:{0,1,17,86,87,214,215,257,1001,4097,16385})
    {
        auto seed=rng.get<block>();
        RibbonOkvs code;
        code.init(n,ssp,seed);
        if(code.params().failureBits<ssp+2) throw RTE_LOC;
        std::vector<block> keys(n),vals(n),out(n),table(code.size()),a(code.size()),sum(code.size());
        rng.get(keys.data(),n);rng.get(vals.data(),n);rng.get(a.data(),a.size());
        for(bool randomized:{false,true})
        {
            if(!code.solve(keys,vals,table,randomized?&rng:nullptr)) throw RTE_LOC;
            code.decode(keys,out,table,3);
            if(vals!=out) throw RTE_LOC;
        }
        // Queries include nonmembers, duplicate queries, single-query/tail
        // calls and the real unpadded external-table contract.
        std::vector<block> queries(517),decoded(517),expected(517),other(517),combined(517);
        rng.get(queries.data(),queries.size());queries.back()=queries.front();
        code.decode(queries,decoded,a);
        for(u64 i=0;i<queries.size();++i) expected[i]=reference(queries[i],a,code.params(),seed);
        if(decoded!=expected) throw RTE_LOC;
        code.decode(queries,other,table);
        for(u64 i=0;i<a.size();++i) sum[i]=a[i]^table[i];
        code.decode(queries,combined,sum);
        for(u64 i=0;i<queries.size();++i) if(combined[i]!=(decoded[i]^other[i])) throw RTE_LOC;
        code.decode({queries.data(),1},{decoded.data(),1},a);
        if(decoded[0]!=expected[0]) throw RTE_LOC;
        if(n>=2)
        {
            keys[1]=keys[0];
            if(code.solve(keys,vals,table)) throw RTE_LOC;
        }
    }
    auto large=RibbonParams::select(1ull<<20,40);
    if(large.columns!=5*(1ull<<18) || large.width!=128 || large.failureBits<42) throw RTE_LOC;
}

void Ribbon_protocol_test(const CLP&)
{
    PRNG rng(toBlock(891,33));
    for(bool malicious:{false,true})
    for(bool reduced:{false,true})
    for(u64 nt:{1,3})
    {
        RsPsiSender sender;
        RsPsiReceiver receiver;
        sender.setOkvsType(OkvsType::Ribbon);receiver.setOkvsType(OkvsType::Ribbon);
        // Reconfigure the same objects across dense and banded shapes.
        for(u64 n:{1,37,257,4003})
        {
            const u64 ns=n+9;
            auto sockets=coproto::LocalAsyncSocket::makePair();
            std::vector<block> send(ns),recv(n);
            rng.get(send.data(),ns);rng.get(recv.data(),n);
            send.back()=recv.back();
            sender.init(ns,n,40,rng.get<block>(),malicious,nt,reduced);
            receiver.init(ns,n,40,rng.get<block>(),malicious,nt,reduced);
            auto s=sender.run(send,sockets[0]);auto r=receiver.run(recv,sockets[1]);
            eval(s,r);
            if(receiver.mIntersection!=std::vector<u64>{n-1}) throw RTE_LOC;
        }
    }
}

void Ribbon_failure_test(const CLP&)
{
    for(bool malicious:{false,true})
    {
        RsOprfSender sender;RsOprfReceiver receiver;
        sender.setOkvsType(OkvsType::Ribbon);receiver.setOkvsType(OkvsType::Ribbon);
        sender.mMalicious=receiver.mMalicious=malicious;
        auto sockets=coproto::LocalAsyncSocket::makePair();
        PRNG a(toBlock(11,22)),b(toBlock(33,44));
        std::vector<block> duplicateKeys(37,ZeroBlock),out(37);
        auto results=macoro::sync_wait(macoro::when_all_ready(
            sender.send(37,a,sockets[0]),receiver.receive(duplicateKeys,out,b,sockets[1])));
        unsigned failed=0;
        try {std::get<0>(results).result();} catch(const std::runtime_error& e) {
            if(std::string(e.what())!="Ribbon OKVS encoding failed") throw;
            ++failed;
        }
        try {std::get<1>(results).result();} catch(const std::runtime_error& e) {
            if(std::string(e.what())!="Ribbon OKVS encoding failed") throw;
            ++failed;
        }
        if(failed!=2) throw RTE_LOC;
    }
}
