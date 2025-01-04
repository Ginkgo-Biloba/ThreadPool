#include "atomic.hpp"
#include <vector>
#include <memory>

namespace gk {

#if defined _WIN32

struct JobLock {
	SRWLOCK impl;

	JobLock() { InitializeSRWLock(&impl); }
	~JobLock() = default;
	void acquire() { AcquireSRWLockExclusive(&impl); }
	void release() { ReleaseSRWLockExclusive(&impl); }
};

struct JobCond {
	CONDITION_VARIABLE impl;

	JobCond() { InitializeConditionVariable(&impl); }
	~JobCond() = default;
	void wait(JobLock& lock) { GK_ASSERT(SleepConditionVariableSRW(&impl, &(lock.impl), INFINITE, 0)); }
	void signal() { WakeConditionVariable(&impl); }
	void broadcast() { WakeAllConditionVariable(&impl); }
};

/* WaitEvent

On Windows, submit and nsleep are limited to 65535.
*/
class JobEvent {
	enum {
		one_value = 1u,
		mask_value = (1u << 16) - 1u,
		one_sleep = 1u << 16,
		mask_sleep = ((1u << 16) - 1u) << 16
	};

	/* Bit field
	  - 15-0  submit: The real value, i.e. the number of submission
	  - 31-16 nsleep: The number of threads sleeping */
	uint32_t value;

public:
	JobEvent() { value = 0; }
	/* Should not on working or on sleeping */
	~JobEvent() { GK_ASSERT(value == 0); }

	void wait(uint32_t desired);
	void wake();
	uint32_t enter() { return atomic_fetch_add(&value, +one_value) & mask_value; }
	uint32_t leave() { return atomic_fetch_add(&value, -one_value) & mask_value; }
};

#elif defined __linux__

struct JobLock {
	pthread_mutex_t impl;

	JobLock() { GK_ASSERT(!pthread_mutex_init(&impl, NULL)); }
	~JobLock() { GK_ASSERT(!pthread_mutex_destroy(&impl)); }
	void acquire() { GK_ASSERT(!pthread_mutex_lock(&impl)); }
	void release() { GK_ASSERT(!pthread_mutex_unlock(&impl)); }
};

struct JobCond {
	pthread_cond_t impl;

	JobCond() { GK_ASSERT(!pthread_cond_init(&impl, NULL)); }
	~JobCond() { GK_ASSERT(!pthread_cond_destroy(&impl)); }
	void wait(JobLock& lock) { GK_ASSERT(!pthread_cond_wait(&impl, &(lock.impl))); }
	void signal() { GK_ASSERT(!pthread_cond_signal(&impl)); }
	void broadcast() { GK_ASSERT(!pthread_cond_broadcast(&impl)); }
};

/* WaitEvent

On Linux, it is just a wrapper on futex invocation.
*/
class JobEvent {
	uint32_t value;

public:
	JobEvent() { value = 0; }
	~JobEvent() { GK_ASSERT(value == 0); }

	void wait(uint32_t desired)
	{
		for (uint32_t submit; (submit = atomic_load(&value)) != desired;)
			sysfutex(&value, FUTEX_WAIT_PRIVATE, submit, NULL, NULL, 0);
	}
	void wake() { sysfutex(&value, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0); }
	uint32_t enter() { return atomic_fetch_add(&value, +1); }
	uint32_t leave() { return atomic_fetch_add(&value, -1); }
};

#endif

/* Asynchronous job

Before completed,
  it can be submitted to the thread pool multiple times (< 65536),
  to allowing multiple threads to do the job simultaneously.
Need to do job's partition and concurrency in `call`.

After completed, the variable can be reused.
*/
struct AsyncJob {
	/* Job priority.
	  Considered reasonably but limitedly internally,
	  to avoid other jobs waiting for long time */
	uint32_t priority;

	/* Indicates how many threads will work on it simultaneously.
	  Multiple threads can wait on a same job */
	JobEvent event;

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

	void submit(std::shared_ptr<AsyncJob> job);

	/* Waiting all submitted jobs completed

	  Jobs submitted during waiting are not be guaranteed completed.
	*/
	void wait();

private:
	struct IdJob {
		uint32_t id;
		std::shared_ptr<AsyncJob> job;
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
	// The number of jobs not completed
	JobEvent event;
	Worker workers[MAX_THREAD];
	JobLock pool_lock, work_lock;
	JobCond work_cond;
	std::vector<IdJob> waitlist;

#if defined _WIN32
	static unsigned __stdcall trdRoutine(void* void_args);
#elif defined __linux__
	static void* trdRoutine(void* void_args);
#endif
};

/* Synchronous job, same to cv::ParLoopBody */
struct SyncJob {
	/* The number of invorking `call` at most
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

	  @param tid   the index of thread doing this call, starting from 0
	  @param start the start of slice, inclusively
	  @param end   the end   of slice, exclusively
	*/
	virtual void call(uint32_t tid, uint32_t start, uint32_t end) = 0;
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
	struct JobRef {
		SyncPool* pool;
		SyncJob* job;
		uint32_t nstripe, maxcall;
		uint32_t allstart, allend;
		// Current index in range
		uint32_t index;
		// The number of threads on working
		JobEvent event;

		JobRef(SyncJob& job, uint32_t ntrd);
		~JobRef();
		void execute(uint32_t tid);
	};

	struct Worker {
		uint32_t index, stop;
		SyncPool* pool;
#if defined _WIN32
		unsigned win32_id;
		uintptr_t thread;
#elif defined __linux__
		pthread_t thread;
#endif
		JobLock lock;
		JobCond cond;
		std::shared_ptr<JobRef> ref;
	};

	uint32_t num_worker;
	Worker workers[MAX_THREAD - 1];
	JobLock pool_lock;
	JobCond pool_cond;

#if defined _WIN32
	static unsigned __stdcall trdRoutine(void* void_args);
#elif defined __linux__
	static void* trdRoutine(void* void_args);
#endif
};

}
