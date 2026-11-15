#pragma once
// © 2022 Visa.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#include "volePSI/Defines.h"
#include "volePSI/RsOprf.h"
#include "sparsehash/dense_hash_map"
#include "cryptoTools/Common/Timer.h"

namespace volePSI
{
    namespace details
    {
        struct PsiHash
        {
            size_t operator()(const block& value) const { return value.get<size_t>(0); }
        };
        struct PsiHashTable : google::dense_hash_map<block, u64, PsiHash>
        {
            PsiHashTable() { set_empty_key(oc::ZeroBlock); }
        };
        struct RsPsiBase
        {

            u64 mSenderSize = 0;
            u64 mRecverSize = 0;
            u64 mSsp = 0;
            PRNG mPrng;
            bool mMalicious = false;
            bool mCompress = true;
            u64 mNumThreads = 0;
            u64 mMaskSize = 0;
            bool mUseReducedRounds = false;
            bool mDebug = false;

            void init(u64 senderSize, u64 recverSize, u64 statSecParam, block seed, bool malicious, u64 numThreads, bool useReducedRounds = false);

        };
    }

    class RsPsiSender : public details::RsPsiBase, public oc::TimerAdapter
    {
    public:

        RsOprfSender mSender;
        void setMultType(oc::MultType type) { mSender.setMultType(type); };


        Proto run(span<block> inputs, Socket& chl);

        // Call only between runs. Does not reset the OPRF/VOLE state.
        void releaseHashBuffers() { mHashes.clear(); }
    private:
        oc::AlignedUnVector<block> mHashes;
    };


    class RsPsiReceiver : public details::RsPsiBase, public oc::TimerAdapter
    {
    public:
        RsOprfReceiver mRecver;
        void setMultType(oc::MultType type) { mRecver.setMultType(type); };

        std::vector<u64> mIntersection;

        Proto run(span<block> inputs, Socket& chl);

        // Call only between runs. Does not reset the OPRF/VOLE state.
        void releaseHashBuffers() { mHashStorage.clear(); mTables.clear(); }
    private:
        oc::AlignedUnVector<block> mHashStorage;
        std::vector<std::unique_ptr<details::PsiHashTable>> mTables;
    };
}
