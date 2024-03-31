#pragma once
#include "refptr.hpp"

namespace gk {
namespace details {
class SyncWorker;
class SyncImpl;
}

class SyncJob : public RefObj {
	friend class details::SyncWorker;
	friend class details::SyncImpl;

	int id;
	// current work index
	int index, nstripe;
	// number of threads worked / completed on it
	int active, completed, finished;

	int call(bool spawned);
	int schedule(int n);

public:
	// max submit this times
	// i.e. max use such many threads
	int max_call;
	int start, stop;

	SyncJob();
	virtual ~SyncJob();
	virtual void call(int from, int to) = 0;
};

class SyncPool {
	uint8_t storage[128];
	details::SyncImpl* impl;

	SyncPool(SyncPool const&) = delete;
	SyncPool& operator=(SyncPool const&) = delete;

	SyncPool(SyncPool&&) = delete;
	SyncPool& operator=(SyncPool&&) = delete;

public:
	SyncPool();
	~SyncPool();

	// set the number of threads, including the main thread
	// -1/0 for default number
	int set(int size);
	// get the number of threads, including the main thread
	int get();

	void submit(RefPtr<SyncJob> const& job);
};

}
