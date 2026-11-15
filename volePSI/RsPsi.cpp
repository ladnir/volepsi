#include "RsPsi.h"
#include <array>
#include <future>
//#include "thirdparty/parallel-hashmap/parallel_hashmap/phmap.h"
namespace volePSI
{



    namespace
    {
        // Keep SIMD scratch on an ordinary stack, outside the coroutine frame.
        OC_NOINLINE void insertHashes(details::PsiHashTable& map,
            span<const block> hashes, block mask)
        {
            constexpr u64 batchSize = 128;
            std::array<std::pair<block, u64>, batchSize> batch;
            u64 i = 0;
            for (; i + batchSize <= hashes.size(); i += batchSize)
            {
                for (u64 j = 0; j < batchSize; ++j)
                    batch[j] = { hashes[i + j] & mask, i + j };
                map.insert(batch.begin(), batch.end());
            }
            for (; i < hashes.size(); ++i) map.insert({ hashes[i] & mask, i });
        }

        template<typename Visitor>
        OC_NOINLINE void visitHashes(span<const u8> bytes, u64 width, block mask, Visitor&& visit)
        {
            if (!width || width > sizeof(block) || bytes.size() % width)
                throw std::invalid_argument("Invalid PSI hash width.");
            auto ptr = bytes.data();
            auto remaining = bytes.size();
            // Only load a full block when all of its bytes are in the received
            // buffer. Mask away bytes belonging to the following hash.
            for (; remaining >= sizeof(block); remaining -= width, ptr += width)
            {
                block h;
                std::memcpy(&h, ptr, sizeof(h));
                visit(h & mask);
            }
            for (; remaining; remaining -= width, ptr += width)
            {
                block h = oc::ZeroBlock;
                std::memcpy(&h, ptr, width);
                visit(h);
            }
        }
    }

	void details::RsPsiBase::init(
		u64 senderSize,
		u64 recverSize,
		u64 statSecParam,
		block seed,
		bool malicious,
		u64 numThreads,
		bool useReducedRounds)
	{
		mSenderSize = senderSize;
		mRecverSize = recverSize;
		mSsp = statSecParam;
		mPrng.SetSeed(seed);
		mMalicious = malicious;

		mMaskSize = malicious ?
			sizeof(block) :
			std::min<u64>(oc::divCeil(mSsp + oc::log2ceil(mSenderSize * mRecverSize), 8), sizeof(block));
		mCompress = mMaskSize != sizeof(block);

		mNumThreads = numThreads;
		mUseReducedRounds = useReducedRounds;
	}

	Proto RsPsiSender::run(span<block> inputs, Socket& chl)
	{

		auto hashes = span<u8>{};
		setTimePoint("RsPsiSender::run-begin");

		if (mTimer)
			mSender.setTimer(getTimer());

		mSender.mMalicious = mMalicious;
		mSender.mSsp = mSsp;
		mSender.mDebug = mDebug;

		co_await mSender.send(mRecverSize, mPrng, chl, mNumThreads, mUseReducedRounds);

		setTimePoint("RsPsiSender::run-opprf");

		mHashes.resize(inputs.size());
        hashes = span<u8>(reinterpret_cast<u8*>(mHashes.data()), inputs.size() * sizeof(block));
		mSender.eval(inputs, span<block>((block*)hashes.data(), inputs.size()), mNumThreads);

		setTimePoint("RsPsiSender::run-eval");
		if (mCompress)
		{
			auto src = (block*)hashes.data();
			auto dest = (u8*)hashes.data();
			u64 i = 0;

			for (; i < std::min<u64>(mSenderSize, 100); ++i)
			{
				memmove(dest, src, mMaskSize);
				dest += mMaskSize;
				src += 1;
			}
			for (; i < mSenderSize; ++i)
			{
				memcpy(dest, src, mMaskSize);
				dest += mMaskSize;
				src += 1;
			}
			static_cast<span<u8>&>(hashes) = span<u8>((u8*)hashes.data(), dest);
		}

		// Await completion before this reusable storage can be overwritten.
        co_await chl.send(hashes);
		setTimePoint("RsPsiSender::run-sendHash");

	}

	Proto RsPsiReceiver::run(span<block> inputs, Socket& chl)
	{
		setTimePoint("RsPsiReceiver::run-enter");
		static const u64 batchSize = 128;

		struct MultiThread
		{
			std::promise<void> prom;
			std::shared_future<void> fu;
			std::vector<std::thread> thrds;
			std::function<void(u64)>routine;
			std::atomic<u64> numDone;
			std::promise<void> hashingDoneProm;
			std::shared_future<void> hashingDoneFu;
			std::mutex mMergeMtx;

			u64 numThreads;
			u64 binSize;
			libdivide::libdivide_u32_t divider;
		};

		auto myHashes = span<block>{};
		auto theirHashes = oc::MatrixView<u8>{};
		auto i = u64{};
		auto mt = std::unique_ptr<MultiThread>{};
		auto mask = block{};

		setTimePoint("RsPsiReceiver::run-begin");
		mIntersection.clear();

        mHashStorage.resize(mRecverSize + oc::divCeil(mSenderSize * mMaskSize, sizeof(block)));
        myHashes = span<block>(mHashStorage.data(), mRecverSize);
        theirHashes = oc::MatrixView<u8>(reinterpret_cast<u8*>(mHashStorage.data() + mRecverSize), mSenderSize, mMaskSize);
        while (mTables.size() < std::max<u64>(1, mNumThreads))
            mTables.emplace_back(std::make_unique<details::PsiHashTable>());

		setTimePoint("RsPsiReceiver::run-alloc");

		if (mTimer)
			mRecver.setTimer(getTimer());

		mRecver.mMalicious = mMalicious;
		mRecver.mSsp = mSsp;
		mRecver.mDebug = mDebug;

		// todo, parallelize these two
		co_await(mRecver.receive(inputs, myHashes, mPrng, chl, mNumThreads, mUseReducedRounds));
		setTimePoint("RsPsiReceiver::run-opprf");

		mask = oc::ZeroBlock;
		for (i = 0; i < mMaskSize; ++i)
			mask.set<u8>(i, ~0);

		if (mNumThreads < 2)
		{

            auto& map = *mTables[0];
            map.clear_no_resize();
            map.resize(myHashes.size());
            setTimePoint("RsPsiReceiver::run-reserve");
            setTimePoint("RsPsiReceiver::run-set_empty_key");
            insertHashes(map, myHashes, mask);

			setTimePoint("RsPsiReceiver::run-insert");

			co_await(chl.recv(theirHashes));

			setTimePoint("RsPsiReceiver::run-recv");

            visitHashes({ theirHashes.data(), mSenderSize * mMaskSize }, mMaskSize, mask,
                [&](block h) {
                    auto found = map.find(h);
                    if (found != map.end()) mIntersection.push_back(found->second);
                });

			setTimePoint("RsPsiReceiver::run-find");
		}
		else
		{
			mt.reset(new MultiThread);

			mt->fu = mt->prom.get_future().share();

			setTimePoint("RsPsiReceiver::run-reserve");

			mt->numDone = 0;
			mt->hashingDoneFu = mt->hashingDoneProm.get_future().share();

			mt->numThreads = std::max<u64>(1, mNumThreads);
			mt->binSize = Baxos::getBinSize(mNumThreads, mRecverSize, mSsp);
			mt->divider = libdivide::libdivide_u32_gen(mt->numThreads);

			mt->routine = [&](u64 thrdIdx)
				{
					if (!thrdIdx)
						setTimePoint("RsPsiReceiver::run-threadBegin");

					auto& divider = mt->divider;
					auto& map = *mTables[thrdIdx];
                    map.clear_no_resize();
                    map.resize(mt->binSize);

					if (!thrdIdx)
						setTimePoint("RsPsiReceiver::run-set_empty_key_par");

					u64 i = 0;
					std::array<std::pair<block, u64>, batchSize> hh;
					for (; i < myHashes.size();)
					{
						u64 j = 0;
						while (j != batchSize && i < myHashes.size())
						{
							auto v = myHashes[i].get<u32>(0);
							auto k = libdivide::libdivide_u32_do(v, &divider);
							v -= k * mNumThreads;
							if (v == thrdIdx)
							{
								hh[j] = { myHashes[i] & mask, i };
								++j;
							}
							++i;
						}
						map.insert(hh.begin(), hh.begin() + j);
					}

					if (++mt->numDone == mt->numThreads)
						mt->hashingDoneProm.set_value();
					else
						mt->hashingDoneFu.get();

					if (!thrdIdx)
						setTimePoint("RsPsiReceiver::run-insert_par");

					mt->fu.get();
					if (!thrdIdx)
						setTimePoint("RsPsiReceiver::run-recv_par");

					auto begin = thrdIdx * myHashes.size() / mNumThreads;
					u64 intersectionSize = 0;
					u64* intersection = (u64*)&myHashes[begin];

                    visitHashes({ theirHashes.data(), mSenderSize * mMaskSize }, mMaskSize, mask,
                        [&](block h) {
                            auto v = h.get<u32>(0);
                            auto k = libdivide::libdivide_u32_do(v, &divider);
                            if (v - k * mNumThreads == thrdIdx)
                            {
                                auto found = map.find(h);
                                if (found != map.end()) intersection[intersectionSize++] = found->second;
                            }
                        });

					if (!thrdIdx)
						setTimePoint("RsPsiReceiver::run-find_par");
					if (intersectionSize)
					{
						std::lock_guard<std::mutex> lock(mt->mMergeMtx);
						mIntersection.insert(mIntersection.end(), intersection, intersection + intersectionSize);
					}
				};


			mt->thrds.resize(mt->numThreads);
			for (i = 0; i < mt->thrds.size(); ++i)
				mt->thrds[i] = std::thread(mt->routine, i);
			co_await(chl.recv(theirHashes));
			mt->prom.set_value();

			for (i = 0; i < mt->thrds.size(); ++i)
				mt->thrds[i].join();

			setTimePoint("RsPsiReceiver::run-done");

		}
	}

}