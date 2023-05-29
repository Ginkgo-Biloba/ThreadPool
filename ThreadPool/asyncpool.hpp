#pragma once
#include "atomic.hpp"

namespace gk
{
namespace details
{
class AsyncImpl;
class AsyncWorker;
}

class AsyncTask;

class AsyncPool
{
	friend class AsyncTask;
	details::AsyncImpl* impl;

	AsyncPool(AsyncPool const&) = delete;
	AsyncPool& operator=(AsyncPool&&) = delete;
	AsyncPool& operator=(AsyncPool const&) = delete;

public:
	AsyncPool();
	AsyncPool(AsyncPool&&);
	~AsyncPool();

	// get the number of workers
	int get();
	// set the number of workers
	// -1/0 for default number
	int set(int size);

	void submit(AsyncTask* task);
	void submit(AsyncTask** tasks, size_t len);
	void wait();
};

class AsyncTask : public RefCount<AsyncTask>
{
	friend class details::AsyncImpl;
	friend class details::AsyncWorker;

	int num_submit, num_finish;
#if defined HAVE_PTHREADS_PF
	pthread_mutex_t lock;
	pthread_cond_t cond;
#elif defined HAVE_WIN32_THREAD
	SRWLOCK lock;
	CONDITION_VARIABLE cond;
#endif

protected:
	virtual ~AsyncTask();

public:
	// start from 0. use for debug
	int id;

	AsyncTask();
	virtual void call() = 0;
	void wait();
};

}
