#pragma once
#include "volePSI/Defines.h"
#include <cryptoTools/Crypto/PRNG.h>
#include <string_view>
#include <stdexcept>

namespace volePSI
{
    enum class OkvsType { Baxos, Ribbon };
    inline OkvsType parseOkvsType(std::string_view name)
    {
        if(name=="baxos") return OkvsType::Baxos;
        if(name=="ribbon") return OkvsType::Ribbon;
        throw std::invalid_argument("OKVS must be baxos or ribbon");
    }

    // A concrete witness for the ideal random-band rank bound in
    // research/ribbon/proof.txt. Dense mode uses the elementary 2^(n-width) bound.
    struct RibbonParams
    {
        u64 items = 0, columns = 0;
        unsigned width = 0;
        double failureBits = 0, rho = 0, theta = 0;
        static RibbonParams select(u64 items, u64 ssp);
    };

    // Linear OKVS for block payloads. Keys must be distinct and fixed
    // independently of the public seed. No automatic seed retries.
    // All buffers have exactly size() logical cells; no padding is required.
    class RibbonOkvs
    {
    public:
        void init(u64 n, u64 ssp, block seed);
        u64 size() const { return mParams.columns; }
        const RibbonParams& params() const { return mParams; }
        void setSeed(block seed) { mSeed = seed; }
        // The label cipher must be absent from every row-key dependency.
        bool labelKeyIndependent(block seed, block labelKey) const;

        // False means rank deficiency; output is then unspecified. Zero free
        // cells when rng==nullptr, otherwise uniformly random free cells.
        bool solve(span<const block> keys, span<const block> values,
                   span<block> table, oc::PRNG* rng = nullptr) const;
        void decode(span<const block> keys, span<block> values,
                    span<const block> table, u64 numThreads = 1) const;

    private:
        RibbonParams mParams;
        block mSeed;
    };
}
