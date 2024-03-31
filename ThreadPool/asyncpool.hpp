#pragma once
#include "refptr.hpp"

namespace gk {
namespace details {
class AsyncImpl;
class AsyncWorker;
}

class AsyncTask;

class AsyncPool {
	friend class AsyncTask;
	details::AsyncImpl* impl;
	uint8_t storage[192];

	AsyncPool(AsyncPool&&) = delete;
	AsyncPool(AsyncPool const&) = delete;
	AsyncPool& operator=(AsyncPool&&) = delete;
	AsyncPool& operator=(AsyncPool const&) = delete;

public:
	AsyncPool();
	~AsyncPool();

	// get the number of workers
	int get();
	// set the number of workers
	// -1/0 for default number
	int set(int size);

	void submit(RefPtr<AsyncTask> const& task);
	void submit(RefPtr<AsyncTask>* tasks, size_t len);
	void wait();
};

class AsyncTask : public RefObj {
	friend class details::AsyncImpl;
	friend class details::AsyncWorker;

	int num_submit, num_finish;
	SRWLOCK lock;
	CONDITION_VARIABLE cond;

public:
	// start from 0. use for debug
	int id;

	AsyncTask();
	virtual ~AsyncTask();
	virtual void call() = 0;
	void wait();
};

}
