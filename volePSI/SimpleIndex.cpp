#include "SimpleIndex.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/Log.h"
#include "cryptoTools/Common/CuckooIndex.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace volePSI
{

    void SimpleIndex::print()
    {

        for (u64 i = 0; i < mBins.size(); ++i)
        //	for (u64 i = 0; i <1; ++i)
        {
            std::cout << "Bin #" << i << std::endl;

            std::cout << " contains " << mBinSizes[i] << " elements" << std::endl;

            for (u64 j = 0; j < mBinSizes[i]; ++j)
            {
                std::cout << "    idx=" << mBins(i, j).idx() << "  hIdx=" << mBins(i, j).hashIdx() << std::endl;
                //	std::cout << "    " << mBins[i].first[j] << "  " << mBins[i].second[j] << std::endl;
            }

            std::cout << std::endl;
        }

        std::cout << std::endl;
    }

    namespace
    {
        // (1+x) log(1+x) - x, without cancellation near zero.
        inline double logMoment(double x, double log1px)
        {
            if (std::abs(x) < 0.01)
                return x * x * (0.5 + x * (-1.0 / 6 + x * (1.0 / 12 +
                    x * (-1.0 / 20 + x * (1.0 / 30 + x * (-1.0 / 42 + x / 56))))));
            return (1 + x) * log1px - x;
        }
    }

    u64 SimpleIndex::get_bin_size(u64 numBins, u64 numBalls, u64 statSecParam,
        bool /*approx*/, u64 numBinSets)
    {
        if (!numBins || !numBinSets)
            throw std::invalid_argument("Bin sizing requires positive bin/set counts.");
        if (!numBalls || numBins == 1)
            return numBalls;

        constexpr double ln2 = 0.69314718055994530942;
        constexpr double eps = std::numeric_limits<double>::epsilon();

        // A fixed bin has X ~ Bin(numBalls, 1/numBins) under independent
        // uniform hashing. Chernoff gives Pr[X >= mean+d] <= exp(-rate(d)),
        // where rate(d) = n D((mean+d)/n || mean/n).
        // Union over all bins and sets; reserve one bit so Baxos can combine
        // the merged-bin and per-thread bounds within its requested budget.
        double target = (double(statSecParam) + 1) * ln2 +
            std::log(double(numBins)) + std::log(double(numBinSets));
        target += std::max(1e-8, 64 * eps * target);

        // Round the distribution parameters upward, including u64 values
        // beyond double's exact integer range. The target has a small extra
        // margin for arithmetic/libm rounding; integer capacity rounds up too.
        const double n = std::nextafter(double(numBalls), std::numeric_limits<double>::infinity());
        const double mean = (n / double(numBins)) * (1 + 8 * eps);
        const double rest = n - mean;
        const double b = rest / n;
        const double a = b * target / 3;

        // Bernstein supplies an upper starting point. Newton steps on the
        // increasing convex rate approach the root from above. Retain only
        // points whose rate meets the target, with n as the safe fallback.
        double d = std::min(a + std::sqrt(2 * mean * b * target + a * a),
            std::nextafter(rest, 0.0));
        double upper = rest;
        for (unsigned i = 0; i < 6; ++i)
        {
            const double x = d / mean;
            const double y = -d / rest;
            const double lx = std::log1p(x);
            const double ly = std::log1p(y);
            const double rate = mean * logMoment(x, lx) + rest * logMoment(y, ly);
            if (!(rate >= target))
                break;
            upper = d;
            const double step = (rate - target) / (lx - ly);
            if (step < 0.1)
                break;
            d -= step;
        }

        const double capacity = std::ceil(std::nextafter(mean + upper,
            std::numeric_limits<double>::infinity()));
        return capacity >= double(numBalls) ? numBalls : u64(capacity);
    }

    void SimpleIndex::init(u64 numBins, u64 numBalls, u64 statSecParam, u64 numHashFunction)
    {
        if (!numHashFunction || numHashFunction > numBins ||
            numBalls > std::numeric_limits<u64>::max() / numHashFunction)
            throw std::invalid_argument("Invalid SimpleIndex hash count or item count.");
        mNumHashFunctions = numHashFunction;
        mMaxBinSize = get_bin_size(numBins, numBalls * numHashFunction, statSecParam);
        mBins.resize(numBins, mMaxBinSize);
        mBinSizes.resize(numBins, 0);
        mItemToBinMap.resize(numBalls, numHashFunction);
        mNumBins = numBins;
    }

    void SimpleIndex::insertItems(span<block> items, block hashingSeed)
    {
        oc::CuckooIndex<> cuckoo;

        {
            //cuckoo.mLocations.resize(items.size(), mNumHashFunctions, oc::AllocType::Uninitialized);
            u64 binCount = mNumBins;

            // mBins.resize(binCount, mMaxBinSize);
            // mStash.resize(mParams.mStashSize);
            // mNumBins = binCount;
            // mNumBinMask = mParams.binMask();

            cuckoo.mMods.resize(mNumHashFunctions);
            for (u64 i = 0; i < cuckoo.mMods.size(); ++i)
            {
                cuckoo.mMods[i] = oc::Mod(binCount - i);
            }
        }

        // cuckoo.computeLocations()

        oc::Matrix<u32> locations(32, mNumHashFunctions);
        std::array<block, 32> hashs;
        oc::AES hasher(hashingSeed);
        for (u64 i = 0; i < items.size(); i += hashs.size())
        {
            auto min = std::min<u64>(items.size() - i, hashs.size());
            hasher.hashBlocks(items.data() + i, min, hashs.data());

            cuckoo.computeLocations(hashs, locations);

            for (u64 j = 0; j < mNumHashFunctions; ++j)
            {
                for(u64 k = 0; k < min; ++k)
                {
                    auto loc = locations(k,j);
                    if (mBinSizes[loc] >= mMaxBinSize)
                        throw std::runtime_error("SimpleIndex bin overflow" LOCATION);
                    mBins(loc, mBinSizes[loc]++).set(i+k, j);
                }
            }
        }
    }

}
