#include "RsOprf_Tests.h"
#include "volePSI/RsOprf.h"
#include "volePSI/RsOpprf.h"
#include "cryptoTools/Network/Channel.h"
#include "cryptoTools/Network/Session.h"
#include "cryptoTools/Network/IOService.h"
#include <iomanip>
#include "Common.h"
#include "cryptoTools/Crypto/MultiKeyAES.h"
#include <array>

using coproto::LocalAsyncSocket;
using namespace volePSI;
using namespace oc;

void RsOprf_multiKeyAES_test(const CLP&)
{
    PRNG prng(block(0, 17));
    std::array<block, 8> keys, inputs, ciphertexts, hashes;
    MultiKeyAES<8> batch;
    for (u64 trial = 0; trial < 4; ++trial)
    {
        prng.get(keys.data(), keys.size());
        prng.get(inputs.data(), inputs.size());
        batch.setKeys(keys);
        batch.ecbEncNBlocks(inputs.data(), ciphertexts.data());
        batch.hashNBlocks(inputs.data(), hashes.data());
        for (u64 i = 0; i < keys.size(); ++i)
        {
            AES single(keys[i]);
            if (ciphertexts[i] != single.ecbEncBlock(inputs[i]) ||
                hashes[i] != single.hashBlock(inputs[i]))
                throw RTE_LOC;
        }
        batch.hashNBlocks(inputs.data(), inputs.data());
        if (inputs != hashes)
            throw RTE_LOC;
    }
}

void RsOprf_eval_test(const CLP&)
{
    RsOprfSender sender;
    RsOprfReceiver recver;

    auto sockets = LocalAsyncSocket::makePair();
    u64 n = 4000;
    PRNG prng0(block(0, 0));
    PRNG prng1(block(0, 1));

    std::vector<block> vals(n), recvOut(n);

    prng0.get(vals.data(), n);

    auto p0 = sender.send(n, prng0, sockets[0]);
    auto p1 = recver.receive(vals, recvOut, prng1, sockets[1]);

    eval(p0, p1);
    
    std::vector<block> vv(n);
    sender.eval(vals, vv);

    u64 count = 0;
    for (u64 i = 0; i < n; ++i)
    {
        auto v = sender.eval(vals[i]);
        if (recvOut[i] != v || recvOut[i]  != vv[i])
        {
            if (count < 10)
                std::cout << i << " " << recvOut[i] << " " <<v <<" " << vv[i] << std::endl;
            else
                break;

            ++count;
        }
    }
    if (count)
        throw RTE_LOC;
}




void RsOprf_mal_test(const CLP&)
{
    RsOprfSender sender;
    RsOprfReceiver recver;

    auto sockets = LocalAsyncSocket::makePair();

    // Exercise both the eight-way batches and the single-key tail.
    u64 n = 4003;
    PRNG prng0(block(0, 0));
    PRNG prng1(block(0, 1));

    std::vector<block> vals(n), recvOut(n);

    prng0.get(vals.data(), n);

    sender.mMalicious = true;
    recver.mMalicious = true;

    auto p0 = sender.send(n, prng0, sockets[0]);
    auto p1 = recver.receive(vals, recvOut, prng1, sockets[1]);

    eval(p0, p1);
    
    std::vector<block> vv(n);
    sender.eval(vals, vv);

    u64 count = 0;
    for (u64 i = 0; i < n; ++i)
    {
        auto v = sender.eval(vals[i]);
        if (recvOut[i] != v || recvOut[i] != vv[i])
        {
            if (count < 10)
                std::cout << i << " " << recvOut[i] << " " << v << " " << vv[i] << std::endl;
            else
                break;

            ++count;
        }
    }
    if (count)
        throw RTE_LOC;
}


void RsOprf_reduced_test(const CLP&)
{
    RsOprfSender sender;
    RsOprfReceiver recver;

    auto sockets = LocalAsyncSocket::makePair();

    u64 n = 4000;
    PRNG prng0(block(0, 0));
    PRNG prng1(block(0, 1));

    std::vector<block> vals(n), recvOut(n);

    prng0.get(vals.data(), n);

    auto p0 = sender.send(n, prng0, sockets[0], 0, true);
    auto p1 = recver.receive(vals, recvOut, prng1, sockets[1], 0, true);

    eval(p0, p1);

    std::vector<block> vv(n);
    sender.eval(vals, vv);

    u64 count = 0;
    for (u64 i = 0; i < n; ++i)
    {
        auto v = sender.eval(vals[i]);
        if (recvOut[i] != v || recvOut[i] != vv[i])
        {
            if (count < 10)
                std::cout << i << " " << recvOut[i] << " " << v << " " << vv[i] << std::endl;
            else
                break;

            ++count;
        }
    }
    if (count)
        throw RTE_LOC;
}


