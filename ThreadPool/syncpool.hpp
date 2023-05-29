#pragma once
#include <climits>

namespace gk
{
namespace details
{
class SyncImpl;
}

class SyncTask
{
public:
	// max submit this times
	// i.e. max use such many threads
	int max_call;

	SyncTask()
		: max_call(INT_MAX) { }
	virtual void call(int start, int stop) = 0;
	virtual ~SyncTask() {};
};


class SyncPool
{
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

	void submit(int start, int stop, SyncTask& task);
};

}
