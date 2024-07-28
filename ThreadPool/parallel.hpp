#include "refptr.hpp"
#include <vector>

namespace gk {

/* Asynchronous job

Before completed, it can be submitted to the thread pool multiple times,
		to allowing multiple threads to do the job simultaneously.
Need to do job's partition and concurrency in `call`.

After completed, the variable can be reused.
*/
struct AsyncJob : RefObj {
	/* Job priority.
		Considered reasonably but limitedly internally,
		to avoid other jobs waiting for long time */
	uint32_t priority;

	/* The number of submit,
		indicates how many threads will work on it simultaneously */
	uint32_t working;

	/* The number of sleeping threads.
		Multiple threads can wait on a same job */
	uint32_t sleeping;

	AsyncJob();
	virtual ~AsyncJob();

	/* Implement this function

		The function will be called as many times as the job is submitted.
	*/
	virtual void call() = 0;

	/* Wating for job completed */
	virtual void wait();
};

/* Asynchronous thread pool */
struct AsyncPool {
	enum { MAX_THREAD = 32 };

	AsyncPool();
	~AsyncPool();

	/* The number of background threads */
	uint32_t getNumThread() const { return num_thread; }

	/* Set the number of background threads (<= MAX_THREAD)

		if 0, disable asynchrony, pool will do job during submit.
		Thus able to get a completed stack when debug, maybe useful.
	*/
	void setNumThread(uint32_t n);

	void submit(RefPtr<AsyncJob> job);

	/* Waiting all submitted jobs completed
	
		If jobs are submitted during waiting, they are not be guaranteed completed.
	*/
	void waitAllDone();

private:
	struct IdJob {
		uint32_t id;
		RefPtr<AsyncJob> job;
		bool operator<(IdJob const& rhs) const { return id > rhs.id; }
	};

	struct Worker {
		uint32_t index, stop;
		AsyncPool* pool;
#if defined _WIN32
		unsigned win32_id;
		uintptr_t thread;
#elif defined __linux__
		pthread_t thread;
#endif
	};

	uint32_t num_thread;
	uint32_t current_id;
	// Number of jobs not completed
	uint32_t working;
	// Number of sleeping threads. Usually 1 (the main thread)
	uint32_t sleeping;
	Worker workers[MAX_THREAD];
#if defined _WIN32
	SRWLOCK pool_lock, work_lock;
	CONDITION_VARIABLE work_cond;
#elif defined __linux__
	pthread_mutex_t pool_lock, work_lock;
	pthread_cond_t work_cond;
#endif
	std::vector<IdJob> waitlist;

#if defined _WIN32
	static unsigned __stdcall trdRoutine(void* void_args);
#elif defined __linux__
	static void* trdRoutine(void* void_args);
#endif
};

/* Synchronous job, same to cv::ParLoopBody */

struct SyncJob {
	/* Number of invorking `call` at most
		- 0 : dynamically decide internally. starting from 1/4 now
		- 1 : only the main thread do job
		- other : min(maxcall, allend - allstart) */
	uint32_t maxcall;

	/* start and end of job range
		(allend - allstart) * min(maxcall, num_thread) <= UINT_MAX */
	uint32_t allstart, allend;

	SyncJob()
		: maxcall(0), allstart(0), allend(0) { }
	virtual ~SyncJob() = default;

	/* Implement this function

		Will be invorked at most min(maxcall, num_thread)

		@param start the start of slice, inclusively
		@param end   the end   of slice, exclusively
	*/
	virtual void call(uint32_t start, uint32_t end) = 0;
};

/* Synchronous thread pool, same to cv::parallel_for_ */
struct SyncPool {
	enum { MAX_THREAD = 32 };

	SyncPool();
	~SyncPool();

	/* The number of working threads (background + the main) */
	uint32_t getNumThread() const { return num_worker + 1; }

	/* Set the number of working threads (<= MAX_THREAD), including the main

		if 0 or 1, disable concurrency, pool will do job during submit.
		Thus able to get a completed stack when debug, maybe useful.
	*/
	void setNumThread(uint32_t n);

	/* Do a job, and return */
	void submit(SyncJob& job);

private:
	struct JobRef : RefObj {
		SyncJob* const job;
		uint32_t const nstripe, maxcall;
		uint32_t const allstart, allend;
		// Current index in range
		uint32_t index;
		// The number of threads on working
		uint32_t working;
		// The number of threads sleeping. Only 1 here (the main)
		uint32_t sleeping;

		JobRef(SyncJob& job, uint32_t ntrd);
		~JobRef();
	};

	struct Worker {
		uint32_t index, stop;
		SyncPool* pool;
		RefPtr<JobRef> ref;
#if defined _WIN32
		unsigned win32_id;
		uintptr_t thread;
		SRWLOCK lock;
		CONDITION_VARIABLE cond;
#elif defined __linux__
		pthread_t thread;
		pthread_cond_t cond;
		pthread_mutex_t lock;
#endif
	};

	uint32_t num_worker;
	Worker workers[MAX_THREAD];
#if defined _WIN32
	SRWLOCK pool_lock;
	CONDITION_VARIABLE pool_cond;
#elif defined __linux__
	pthread_cond_t pool_cond;
	pthread_mutex_t pool_lock;
#endif

#if defined _WIN32
	static unsigned __stdcall trdRoutine(void* void_args);
#elif defined __linux__
	static void* trdRoutine(void* void_args);
#endif
};

}
