#include "RsPsi_Tests.h"
#include "volePSI/RsPsi.h"
#include "volePSI/RsCpsi.h"
#include "cryptoTools/Network/Channel.h"
#include "cryptoTools/Network/Session.h"
#include "cryptoTools/Network/IOService.h"
#include "Common.h"
using namespace oc;
using namespace volePSI;
using coproto::LocalAsyncSocket;

namespace
{
    std::vector<u64> run(PRNG& prng, std::vector<block>& recvSet, std::vector<block> &sendSet, bool mal, u64 nt = 1, bool reduced = false)
    {
        auto sockets = LocalAsyncSocket::makePair();

        RsPsiReceiver recver;
        RsPsiSender sender;

        recver.init(sendSet.size(), recvSet.size(), 40, prng.get(), mal, nt, reduced);
        sender.init(sendSet.size(), recvSet.size(), 40, prng.get(), mal, nt, reduced);

        auto p0 = recver.run(recvSet, sockets[0]); 
        auto p1 = sender.run(sendSet, sockets[1]);

        eval(p0, p1);
        
        return recver.mIntersection;
    }


}

void Psi_Rs_empty_test(const CLP& cmd)
{
    u64 n = cmd.getOr("n", 13243);
    std::vector<block> recvSet(n), sendSet(n);
    PRNG prng(ZeroBlock);
    prng.get(recvSet.data(), recvSet.size());
    prng.get(sendSet.data(), sendSet.size());

    auto inter = run(prng, recvSet, sendSet, false);

    if (inter.size())
        throw RTE_LOC;
}

void Psi_Rs_reuse_test(const CLP&)
{
    PRNG prng(block(17, 83));
    RsPsiReceiver receiver;
    RsPsiSender sender;
    // Change size, hash width and worker count on the same objects. In
    // particular, put a match at the last serialized hash to exercise the tail.
    for (u64 nt : {1, 3, 1})
    for (u64 width : {5, 8, 10, 15, 16})
    for (u64 n : {257, 37, 513, 1})
    {
        auto sockets = LocalAsyncSocket::makePair();
        std::vector<block> recvSet(n), sendSet(n);
        prng.get(recvSet.data(), n);
        prng.get(sendSet.data(), n);
        sendSet.back() = recvSet.front();
        receiver.init(n, n, 40, prng.get(), false, nt);
        sender.init(n, n, 40, prng.get(), false, nt);
        // Exercise supported wire widths independently of the size-derived
        // default; the protocol's other statistical parameters stay unchanged.
        receiver.mMaskSize = sender.mMaskSize = width;
        receiver.mCompress = sender.mCompress = width != sizeof(block);
        auto r = receiver.run(recvSet, sockets[0]);
        auto s = sender.run(sendSet, sockets[1]);
        eval(r, s);
        if (receiver.mIntersection != std::vector<u64>{0}) throw RTE_LOC;

        prng.get(sendSet.data(), n);
        auto r2 = receiver.run(recvSet, sockets[0]);
        auto s2 = sender.run(sendSet, sockets[1]);
        eval(r2, s2);
        if (!receiver.mIntersection.empty()) throw RTE_LOC;
    }
    receiver.releaseHashBuffers();
    sender.releaseHashBuffers();
    auto sockets = LocalAsyncSocket::makePair();
    std::vector<block> values(37);
    prng.get(values.data(), values.size());
    receiver.init(37, 37, 40, prng.get(), true, 1);
    sender.init(37, 37, 40, prng.get(), true, 1);
    auto r = receiver.run(values, sockets[0]);
    auto s = sender.run(values, sockets[1]);
    eval(r, s);
    std::sort(receiver.mIntersection.begin(), receiver.mIntersection.end());
    if (receiver.mIntersection.size() != values.size()) throw RTE_LOC;
    for (u64 i = 0; i < values.size(); ++i)
        if (receiver.mIntersection[i] != i) throw RTE_LOC;
}


void Psi_Rs_partial_test(const CLP& cmd)
{
    u64 n = cmd.getOr("n", 13243);
    std::vector<block> recvSet(n), sendSet(n);
    PRNG prng(ZeroBlock);
    prng.get(recvSet.data(), recvSet.size());
    prng.get(sendSet.data(), sendSet.size());

    std::set<u64> exp;
    for (u64 i = 0; i < n; ++i)
    {
        if (prng.getBit())
        {
            recvSet[i] = sendSet[(i + 312) % n];
            exp.insert(i);
        }
    }

    auto inter = run(prng, recvSet, sendSet, false);
    std::set<u64> act(inter.begin(), inter.end());
    if (act != exp)
    {
        std::cout << "exp size " << exp.size() << std::endl;
        std::cout << "act size " << act.size() << std::endl;
        throw RTE_LOC;
    }
}


void Psi_Rs_full_test(const CLP& cmd)
{
    u64 n = cmd.getOr("n", 13243);
    std::vector<block> recvSet(n), sendSet(n);
    PRNG prng(ZeroBlock);
    prng.get(recvSet.data(), recvSet.size());
    sendSet = recvSet;

    std::set<u64> exp;
    for (u64 i = 0; i < n; ++i)
        exp.insert(i);

    auto inter = run(prng, recvSet, sendSet, false);
    std::set<u64> act(inter.begin(), inter.end());
    if (act != exp)
        throw RTE_LOC;
}



void Psi_Rs_reduced_test(const CLP& cmd)
{
    u64 n = cmd.getOr("n", 13243);
    std::vector<block> recvSet(n), sendSet(n);
    PRNG prng(ZeroBlock);
    prng.get(recvSet.data(), recvSet.size());
    sendSet = recvSet;

    std::set<u64> exp;
    for (u64 i = 0; i < n; ++i)
        exp.insert(i);

    auto inter = run(prng, recvSet, sendSet, false, 1, true);
    std::set<u64> act(inter.begin(), inter.end());
    if (act != exp)
        throw RTE_LOC;
}


void Psi_Rs_multiThrd_test(const CLP& cmd)
{
    u64 n = cmd.getOr("n", 13243);
    u64 nt = cmd.getOr("nt", 8);
    std::vector<block> recvSet(n), sendSet(n);
    PRNG prng(ZeroBlock);
    prng.get(recvSet.data(), recvSet.size());
    sendSet = recvSet;

    std::set<u64> exp;
    for (u64 i = 0; i < n; ++i)
        exp.insert(i);

    auto inter = run(prng, recvSet, sendSet, false, nt);
    std::set<u64> act(inter.begin(), inter.end());
    if (act != exp)
        throw RTE_LOC;
}


void Psi_Rs_mal_test(const CLP& cmd)
{
    u64 n = cmd.getOr("n", 13243);
    std::vector<block> recvSet(n), sendSet(n);
    PRNG prng(ZeroBlock);
    prng.get(recvSet.data(), recvSet.size());
    prng.get(sendSet.data(), sendSet.size());

    std::set<u64> exp;
    for (u64 i = 0; i < n; ++i)
    {
        if (prng.getBit())
        {
            recvSet[i] = sendSet[(i + 312) % n];
            exp.insert(i);
        }
    }

    auto inter = run(prng, recvSet, sendSet, true);
    std::set<u64> act(inter.begin(), inter.end());
    if (act != exp)
        throw RTE_LOC;
}
