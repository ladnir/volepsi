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
#include "coproto/Socket/BufferingSocket.h"

using coproto::LocalAsyncSocket;
using namespace volePSI;
using namespace oc;

namespace {
#ifdef _MSC_VER
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    void verifySaltCommitment(block salt, const std::array<u8, 32>& commitment)
    {
        std::array<u8, 32> expected;
        RandomOracle ro(32);
        ro.Update(salt);
        ro.Final(expected);
        if (expected != commitment) throw RTE_LOC;
    }

    // This peer deliberately waits for the salt without sending a correction.
    // All SIMD work stays inside the ordinary library helpers.
    Proto waitForSalt(RsOprfReceiver& receiver, Socket& sock, u64 columns,
        bool ribbon, bool reduced, PRNG& rng, block& salt)
    {
        auto seed = block(12, 34);
        auto wr = block(56, 78);
        auto commitment = std::array<u8, 32>{};
        auto fork = Socket{};
        auto success = u8{1};
        co_await sock.send(seed);
        co_await sock.recv(commitment);
        receiver.mVoleRecver.mSecurityType = SilentSecType::Malicious;
        fork = sock.fork();
        co_await receiver.genVole(columns, rng, fork, reduced);
        if (ribbon) co_await sock.send(success);
        co_await sock.send(wr);
        co_await sock.recv(salt);
        verifySaltCommitment(salt, commitment);
    }

    Proto sendCorrection(Socket& sock, volePSI::span<block> correction)
    {
        co_await sock.send(correction);
    }

    // Ordinary helpers keep field/SIMD temporaries outside the peer coroutine.
#ifdef _MSC_VER
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    std::vector<block> universalTable(Baxos& code)
    {
        std::vector<block> table(code.size(), ZeroBlock);
        for (u64 b = 0; b != code.mNumBins; ++b)
            table[b * code.mPaxosParam.size() + code.mPaxosParam.mSparseSize] = block(0, 1);
        return table;
    }

#ifdef _MSC_VER
    __declspec(noinline)
#else
    __attribute__((noinline))
#endif
    void maskCorrection(std::vector<block>& table, volePSI::span<const block> c)
    {
        for (u64 i = 0; i != table.size(); ++i) table[i] ^= c[i];
    }

    // Declare n inputs, but send the universal-table candidate instead of
    // encoding n labels. VOLE itself is followed honestly by this receiver.
    Proto chosenSeedPeer(RsOprfReceiver& receiver, Baxos& code,
        Socket& sock, PRNG& rng, bool reduced, block& w)
    {
        auto seed = code.mSeed;
        auto commitment = std::array<u8, 32>{};
        auto wr = block(56, 78);
        auto ws = block{};
        auto fork = Socket{};
        auto table = universalTable(code);
        co_await sock.send(seed);
        co_await sock.recv(commitment);
        receiver.mVoleRecver.mSecurityType = SilentSecType::Malicious;
        fork = sock.fork();
        co_await receiver.genVole(table.size(), rng, fork, reduced);
        maskCorrection(table, receiver.mVoleRecver.mC);
        co_await sock.send(wr);
        co_await sock.send(std::move(table));
        co_await sock.recv(ws);
        verifySaltCommitment(ws, commitment);
        w = ws ^ wr;
    }
}

void RsOprf_salt_order_test(const CLP&)
{
    for (auto backend : {OkvsType::Baxos, OkvsType::Ribbon})
    for (bool reduced : {false, true})
    {
        RsOprfSender sender;
        RsOprfReceiver receiver;
        sender.setOkvsType(backend);
        sender.mMalicious = true;
        PRNG sRng(toBlock(719, 2)), rRng(toBlock(719, 3));
        coproto::BufferingSocket sSock, rSock;
        auto s = sender.send(37, sRng, sSock, 1, reduced) | macoro::make_eager();
        block salt = ZeroBlock;
        auto r = waitForSalt(receiver, rSock, sender.okvsSize(),
            backend == OkvsType::Ribbon, reduced, rRng, salt) | macoro::make_eager();
        // Pump until quiescent, with a finite guard against a protocol loop.
        for (unsigned round = 0; ; ++round)
        {
            if (round == 1000) throw RTE_LOC;
            bool moved = false;
            if (auto msg = rSock.getOutbound(); msg && !msg->empty()) { sSock.processInbound(*msg); moved = true; }
            if (auto msg = sSock.getOutbound(); msg && !msg->empty()) { rSock.processInbound(*msg); moved = true; }
            if (!moved) break;
        }
        const bool early = r.is_ready();
        std::vector<block> correction(sender.okvsSize(), ZeroBlock);
        auto c = sendCorrection(rSock, correction) | macoro::make_eager();
        auto wire = rSock.getOutbound();
        if (!wire || wire->size() < 2) throw RTE_LOC;
        sSock.processInbound({wire->data(), wire->size() - 1});
        auto premature = sSock.getOutbound();
        if (premature) rSock.processInbound(*premature);
        const bool partial = r.is_ready();
        sSock.processInbound({wire->data() + wire->size() - 1, 1});
        for (unsigned round = 0; round != 1000 && (!s.is_ready() || !r.is_ready() || !c.is_ready()); ++round)
        {
            if (auto msg = rSock.getOutbound()) sSock.processInbound(*msg);
            if (auto msg = sSock.getOutbound()) rSock.processInbound(*msg);
        }
        if (!s.is_ready() || !r.is_ready() || !c.is_ready()) throw RTE_LOC;
        macoro::sync_wait(s); macoro::sync_wait(r); macoro::sync_wait(c);
        if (early || partial || (premature && !premature->empty())) throw RTE_LOC;
    }
}

void RsOprf_seed_rejection_test(const CLP&)
{
    const block labelKey = mAesFixedKey.getKey();
    for (u64 binSize : {128, 16384})
    for (bool reduced : {false, true})
    for (block seed : {labelKey, labelKey ^ block(0, 1)})
    {
        constexpr u64 declared = 257, probes = 2003;
        Baxos code;
        code.init(declared, binSize, 3, 40, PaxosParam::GF128, seed);
        PRNG rng(block(17, 91)), sRng(block(32, 19)), rRng(block(35, 21));
        std::vector<block> keys(probes), labels(probes), decoded(probes);
        rng.get(keys.data(), keys.size());
        mAesFixedKey.hashBlocks(keys, labels);
        auto witness = universalTable(code);
        code.decode<block>(keys, decoded, witness);
        const bool forbidden = seed == labelKey;
        if (forbidden && decoded != labels) throw RTE_LOC;

        RsOprfSender sender;
        RsOprfReceiver receiver;
        sender.mMalicious = true;
        sender.mBinSize = binSize;
        auto sockets = LocalAsyncSocket::makePair();
        block w = ZeroBlock;
        auto s = sender.send(declared, sRng, sockets[0], 1, reduced);
        auto r = chosenSeedPeer(receiver, code, sockets[1], rRng, reduced, w);
        auto done = macoro::sync_wait(macoro::when_all_ready(std::move(s), std::move(r)));
        bool rejected = false;
        try { std::get<0>(done).result(); }
        catch (const std::invalid_argument& e)
        {
            if (std::string(e.what()) != "Baxos seed equals the OPRF label AES key") throw;
            rejected = true;
        }
        bool peerFailed = false;
        try { std::get<1>(done).result(); }
        catch (const std::exception&) { if (!rejected) throw; peerFailed = true; }
        if (rejected)
        {
            if (!forbidden || !peerFailed || !sender.mVoleSender.mB.empty()) throw RTE_LOC;
            continue;
        }

        // If a mutation removes the check, complete the real attack before
        // failing: the receiver evaluates 2,003 keys after declaring only 257.
        std::vector<block> expected(probes), recovered(probes);
        sender.eval(keys, expected);
        code.decode<block>(keys, recovered, receiver.mVoleRecver.mA);
        u64 matches = 0;
        for (u64 i = 0; i != probes; ++i)
        {
            recovered[i] = AES(recovered[i] ^ w).hashBlock(keys[i]);
            matches += recovered[i] == expected[i];
        }
        if (forbidden)
        {
            if (matches != probes) throw std::runtime_error("equal-seed attack reproduction failed");
            throw std::runtime_error("equal-seed attack accepted: 2003 OPRF outputs after declaring 257 inputs");
        }
        // Control: a neighboring seed completes, but the same table no longer
        // gives the attacker these outputs. This is a regression, not a proof.
        if (matches != 0) throw RTE_LOC;
    }
}

void RsOprf_ribbon_seed_test(const CLP&)
{
    const block labelKey = mAesFixedKey.getKey();
    for(u64 n : {37, 257, 4097})
    {
        RibbonOkvs code;
        code.init(n, 40, ZeroBlock);
        if(code.labelKeyIndependent(labelKey, labelKey)) throw RTE_LOC;
        for(block seed : {ZeroBlock, labelKey ^ block(0, 1), block(17, 43)})
        {
            PRNG rng(seed);
            bool expected = seed != labelKey;
            for(unsigned i=0, count=1+(code.params().width+127)/128; i<count; ++i)
                expected &= rng.get<block>() != labelKey;
            if(code.labelKeyIndependent(seed, labelKey) != expected) throw RTE_LOC;
        }
    }
}

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
    for (bool malicious : {false, true})
    for (auto mult : {DefaultVoleMultType, MultType::ChosenBaa16x4, DefaultMultType, MultType::ExConv7x24})
    {
        RsOprfSender sender;
        RsOprfReceiver recver;

        sender.mMalicious = malicious;
        recver.mMalicious = malicious;
        sender.setMultType(mult);
        recver.setMultType(mult);

        auto sockets = LocalAsyncSocket::makePair();

        u64 n = 4003;
        PRNG prng0(block(0, 0));
        PRNG prng1(block(0, 1));

        std::vector<block> vals(n), recvOut(n);

        prng0.get(vals.data(), n);

        auto p0 = sender.send(n, prng0, sockets[0], 0, true);
        auto p1 = recver.receive(vals, recvOut, prng1, sockets[1], 0, true);

        eval(p0, p1);

        const auto security = malicious ? SilentSecType::Malicious : SilentSecType::SemiHonest;
        if (sender.mVoleSender.mSecurityType != security ||
            recver.mVoleRecver.mSecurityType != security ||
            sender.mVoleSender.mLpnMultType != mult || recver.mVoleRecver.mLpnMultType != mult)
            throw RTE_LOC;

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
}


