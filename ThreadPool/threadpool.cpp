#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <vector>
#include "threadpool.hpp"
#include "atomicop.hpp"
using std::vector;

/*
IMPORTANT: always use the same order of defines
0. HAVE_NONE
1. HAVE_PTHREADS_PF
	- POSIX Threads
2. HAVE_WIN32_THREAD
	- Windows CRT Thread (SRWLOCK + CONDITION_VARIABLE)
	- Available on Windows Vista and later
3. HAVE_WIN32_POOL
	- Windows Thread Pool
	- Available on Windows Vista and later
	- ref: https://stackoverflow.com/questions/8357955/windows-api-thread-pool-simple-example
	- ref: https://dorodnic.com/blog/2015/10/17/windows-threadpool/
*/
#ifndef HAVE_PARALLEL_FRAMEWORK
#  define HAVE_PARALLEL_FRAMEWORK 3
#endif

#if HAVE_PARALLEL_FRAMEWORK == 0
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_NONE")
#elif HAVE_PARALLEL_FRAMEWORK == 1
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_PTHREADS_PF")
#  define HAVE_PTHREADS_PF
#elif HAVE_PARALLEL_FRAMEWORK == 2
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_WIN32_THREAD")
#  define HAVE_WIN32_THREAD
#elif HAVE_PARALLEL_FRAMEWORK == 3
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_WIN32_POOL")
#  define HAVE_WIN32_POOL
#else
#  error must select one implementation
#endif

#if defined HAVE_PTHREADS_PF
#  include <pthread.h>
#elif defined HAVE_WIN32_THREAD || defined HAVE_WIN32_POOL
#  include <Windows.h>
#  ifdef HAVE_WIN32_THREAD
#    include <process.h>
#  endif
#  undef small
#  undef min
#  undef max
#  undef abs
#endif

#if defined __linux__ || defined __APPLE__ || defined __GLIBC__ || defined __HAIKU__
#  include <unistd.h>
#  include <stdio.h>
#  include <sys/types.h>
#  if defined __ANDROID__
#    include <sys/sysconf.h>
#    include <android/log.h>
#  elif defined __APPLE__
#    include <sys/sysctl.h>
#  endif
#endif

#if defined __i386__ || defined __x86_64__ || defined _M_IX86 || defined _M_X64
#  include <emmintrin.h> // for _mm_pause
#endif


#if HAVE_PARALLEL_FRAMEWORK

namespace gk
{

// printf and fprintf is not thread safe in GCC
void log_printf(bool segsev, char const* fmt, ...)
{
	char buf[1 << 14];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;
	if (segsev)
	{
		fputs(buf, stderr);
		fflush(stderr);
#	if defined __ANDROID__
		__android_log_print(ANDROID_LOG_ERROR, "gk::log_error", buf);
#	endif
#if _HAS_EXCEPTIONS
		throw buf;
#else
		char volatile* p = reinterpret_cast<char*>(0x2b);
		*p = 0x2b; // 引发异常
#endif
	}
	else
	{
		fputs(buf, stdout);
#	if defined __ANDROID__
		__android_log_print(ANDROID_LOG_INFO, "gk::log_printf", buf);
#	endif
	}
}

#define log_info(...) log_printf(false, __VA_ARGS__)

#define log_error(...) log_printf(true, __VA_ARGS__)

#ifdef _MSC_VER
#  define GK_Func __FUNCTION__
#elif defined __GNUC__
#  define GK_Func __PRETTY_FUNCTION__
#else 
#  define GK_Func "Can_Not_Get_Func_Name"
#endif

// expr in C assert evaluated exactly only once ?
#define log_assert(expr) do \
	{ \
		if (!(expr)) \
			log_printf(true, "log_assert failed: " #expr " in %s, file %s, line %d\n", \
				GK_Func, __FILE__, __LINE__); \
	} while(0)


// Spin lock's CPU-level yield (required for Hyper-Threading)
inline void yield_pause(int delay)
{
	for (; delay > 0; --delay)
	{
#ifdef YIELD_PAUSE
		YIELD_PAUSE;
#elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#  if !defined(__SSE2__)
		__asm__ __volatile__("rep; nop");
#  else
		_mm_pause();
#  endif
#elif defined __GNUC__ && defined __aarch64__
		asm volatile("yield" ::: "memory");
#elif defined __GNUC__ && defined __arm__
		asm volatile("" ::: "memory");
# elif defined __GNUC__ && defined __mips__ && __mips_isa_rev >= 2
		asm volatile("pause" ::: "memory");
#elif defined __GNUC__ && defined __PPC64__
		asm volatile("or 27,27,27" ::: "memory");
#elif defined _MSC_VER && (defined _M_IX86 || defined _M_X64)
		_mm_pause();
#elif defined _MSC_VER && (defined _M_ARM || defined M_ARM64)
		__nop();
#else
		#warning "can't detect `pause' (CPU-yield) instruction on the target platform, \
		specify YIELD_PAUSE definition via compiler flags"
# endif
	}
}


//////////////////// TPJob ////////////////////

class TPJob
{
	TPLoopBody const& body;

public:
	int start, end;
	int chunk;
	int64_t dummy0[8];

	// number of threads worked on this job
	int active_count;
	int64_t dummy1[8];

	// number of threads completed any activities on this job
	int completed_count;
	int64_t dummy2[8];

	int finished;

	TPJob(Range const& range, TPLoopBody const& b, int n)
		: body(b), start(range.start), end(range.end)
	{
		log_assert(2 <= n);
		start = range.start; end = range.end;
		chunk = (end - start + n - 1) / n;
		// make one task smaller
		chunk = (chunk + 7) / 8;
		// if log_assert failed, consider scale or offset the range
		log_assert(static_cast<int64_t>(end) - start < INT_MAX - n);
		log_assert(static_cast<int64_t>(end) < INT_MAX - n * chunk);
		finished = completed_count = active_count = 0;
		log_info("job %p has been created (%d, %d)\n", this, start, end);
	}

	~TPJob()
	{
		log_assert(atomic_fetch_add(&finished, 0));
		log_assert(atomic_fetch_add(&start, 0) >= end);
		log_info("job %p has been finished\n", this);
	}

	int execute(bool spawned)
	{
		int task = 0;
		Range R;
		for (;;)
		{
			R.start = atomic_fetch_add(&start, chunk);
			if (R.start >= end)
				break;
			R.end = std::min(R.start + chunk, end);
			task += (R.end - R.start);
			body(R);
		}
		int fin = atomic_fetch_add(&finished, 0);
		if (spawned && fin)
		{
			log_error("xxxxx BUG xxxxxx\n\tjob %p %d, %d, %d\n",
				this, R.start, active_count, completed_count);
		}
		return task;
	}
};


namespace details
{

//////////////////// TPWorker ////////////////////

#if defined HAVE_PTHREADS_PF || defined HAVE_WIN32_THREAD

class TPWorker
{
	enum { Active_Wait = 1 << 10 };

	TPWorker(TPWorker const&) = delete;
	TPWorker& operator =(TPWorker const&) = delete;

	/* 考虑实现这个函数
	否则 TPImplement::workers 里面只能放指针，不能放实体
	因为在 vector 扩容的时候，旧的结构体不会无效化
	从而导致互斥体、条件变量、线程被多次析构
	目前在创建子线程前 reserve 足够多，将就着用*/
	TPWorker& operator =(TPWorker&&) = delete;

	TPImplement* pool;
	TPJob* job;

public:
	// for emplace_back
	TPWorker(TPWorker&&) = default;

	// the index in the vector `TPImplement::workers'
	int const id;
	int created, stoped;
	int wake_signal;

#if defined HAVE_PTHREADS_PF
	pthread_mutex_t lock_job;
	pthread_cond_t cond_job;
	pthread_t posix_thread;
#elif defined HAVE_WIN32_THREAD
	SRWLOCK lock_job; // only use its exclusive mode
	CONDITION_VARIABLE cond_job;
	uintptr_t win32_thread;
	unsigned win32_id;
#endif

	TPWorker(TPImplement* pool, int id);
	~TPWorker();
	void assign(TPJob* job);
	void loop();
};

#endif

//////////////////// TPImplement ////////////////////

#define TP_DEBUG_JOB 0

class TPImplement
{
	TPImplement(TPImplement const&) = delete;
	TPImplement& operator =(TPImplement const&) = delete;
	TPImplement& operator =(TPImplement&&) = delete;

	enum { Active_Wait = 1 << 13 };

	// numThread 是子线程的数量
	int numTrdMax, numThread;
#if defined HAVE_PTHREADS_PF || defined HAVE_WIN32_THREAD
	std::vector<TPWorker> workers; // 子线程
#endif
#if TP_DEBUG_JOB
	// 调试用，悬垂引用什么的都不是事
	std::vector<TPJob> jobs_done;
#endif

public:
#if defined HAVE_PTHREADS_PF
	pthread_mutex_t lock_pool;
	pthread_mutex_t lock_work;
	pthread_cond_t cond_work;
#elif defined HAVE_WIN32_THREAD
	SRWLOCK lock_pool;
	SRWLOCK lock_work;
	CONDITION_VARIABLE cond_work;
#elif defined HAVE_WIN32_POOL
	TP_POOL* pool; // the actual Thread-Pool resource
	TP_CALLBACK_ENVIRON envir; // connect work-items to our custom Thread-Pool
	TP_CLEANUP_GROUP* clnup; // clean things up neatly when we are done
	SRWLOCK lock_pool;
#endif

	TPImplement(int n);
	~TPImplement();
	void set(int n);
	int get();
	void run(Range const& range, TPLoopBody const& body);
};

//////////////////// Implement ////////////////////

#if defined HAVE_PTHREADS_PF || defined HAVE_WIN32_THREAD

#if defined HAVE_PTHREADS_PF

static void* TPWorker_Func(void* vp_worker)
{
	static_cast<TPWorker*>(vp_worker)->loop();
	return vp_worker; // just return a value
}

inline void acquire_lock(pthread_mutex_t* pmtx)
{
	pthread_mutex_lock(pmtx);
}

inline void release_lock(pthread_mutex_t* pmtx)
{
	pthread_mutex_unlock(pmtx);
}

inline void wait_condvar(pthread_cond_t* cond, pthread_mutex_t* pmtx)
{
	pthread_cond_wait(cond, pmtx);
}

inline void wake_condvar(pthread_cond_t* cond)
{
	pthread_cond_signal(cond);
}

#elif defined HAVE_WIN32_THREAD

static unsigned _stdcall TPWorker_Func(void* vp_worker)
{
	TPWorker* worker = static_cast<TPWorker*>(vp_worker);
	worker->loop();
	return worker->id; // just return a value
}

inline void acquire_lock(SRWLOCK* srwl)
{
	AcquireSRWLockExclusive(srwl);
}

inline void release_lock(SRWLOCK* srwl)
{
	ReleaseSRWLockExclusive(srwl);
}

inline void wait_condvar(CONDITION_VARIABLE* cond, SRWLOCK* srwl)
{
	SleepConditionVariableSRW(cond, srwl, INFINITE, 0);
}

inline void wake_condvar(CONDITION_VARIABLE* cond)
{
	WakeConditionVariable(cond);
}

#endif


TPWorker::TPWorker(TPImplement* p, int i)
	: pool(p), job(nullptr), id(i), created(0), stoped(0), wake_signal(0)
{
	log_assert(p && "must work with TPImplement");
	log_info("TPWork: worker %d is being created\n", id);
	int err = 0;
#if defined HAVE_PTHREADS_PF
	err = pthread_mutex_init(&lock_job, NULL);
	if (err != 0)
	{
		log_error("worker %d can not init mutex, err = %d\n", id, err);
		return;
	}
	err = pthread_cond_init(&cond_job, NULL);
	if (err != 0)
	{
		log_error("worker %d can not init cond, err = %d\n", id, err);
		return;
	}
	err = pthread_create(&posix_thread, NULL, TPWorker_Func, this);
	if (err != 0)
	{
		log_info("worker %d can not create mutex, err = %d\n", id, err);
		return;
	}
#elif defined HAVE_WIN32_THREAD
	InitializeSRWLock(&lock_job);
	InitializeConditionVariable(&cond_job);
	// for initialize CRT runtime, not use CreateThread
	win32_thread = _beginthreadex(NULL, 0, TPWorker_Func, this, 0, &win32_id);
	err = GetLastError();
	if ((win32_thread == 0) || (err != ERROR_SUCCESS))
	{
		log_info("worker %d can not create mutex, "
			"handle = %zx, win32_id = %u, err = %x\n",
			id, static_cast<size_t>(win32_thread), win32_id, err);
		return;
	}
#endif
	created = 1;
}


TPWorker::~TPWorker()
{
	log_info("TPWork: worker %d is being destroyed\n", id);
	if (created)
	{
		acquire_lock(&lock_job);
		log_assert(!stoped && "repeate stop");
		stoped = 1;
		job = nullptr;
		wake_signal = 1;
		release_lock(&lock_job);
		wake_condvar(&cond_job);
#if defined HAVE_PTHREADS_PF
		pthread_join(posix_thread, NULL);
#elif defined HAVE_WIN32_THREAD
		WaitForSingleObject(reinterpret_cast<HANDLE>(win32_thread), INFINITE);
		CloseHandle(reinterpret_cast<HANDLE>(win32_thread));
#endif
	}
#if defined HAVE_PTHREADS_PF
	pthread_cond_destroy(&cond_job);
	pthread_mutex_destroy(&lock_job);
#elif defined HAVE_WIN32_THREAD
	// nothing to do with SRWLOCK and CONDITION_VARIABLE	
#endif
}


void TPWorker::assign(TPJob* jptr)
{
	if (created == 0)
		return;
	log_info("worker %d is assigned job %p\n", id, jptr);
	log_assert(!stoped && "has stoped");
	acquire_lock(&lock_job);
	// job may has been finished before `I' wake
	// log_assert(!job && "in working");
	// if use shared_ptr<TPJob>, here we can check if job is finished
	// otherwise this check is done in ~TPJob()
	// log_assert(!job || job->finished);
	job = jptr;
	wake_signal = 1;
	release_lock(&lock_job);
	wake_condvar(&cond_job);
}


void TPWorker::loop()
{
	// 立即执行？可能没有主线程安排任务速度快
	// log_assert(!job && "worker start just now");
	log_info("worker %d start now\n", id);

	while (!stoped)
	{
		log_info("worker %d loop (pause)...\n", id);
		for (int i = 0; i < Active_Wait; ++i)
		{
			if (atomic_fetch_add(&wake_signal, 0))
				break;
			yield_pause(16);
		}

		acquire_lock(&lock_job);
		while (!wake_signal)
		{
			log_info("worker %d wait (sleep)...\n", id);
			wait_condvar(&cond_job, &lock_job);
		}
		TPJob* jptr = job;
		job = nullptr;
		wake_signal = 0;
		release_lock(&lock_job);

		if (stoped)
			break;
		if (jptr && (atomic_fetch_add(&(jptr->start), 0) < jptr->end))
		{
			int active = atomic_fetch_add(&(jptr->active_count), 1);
			log_info("worker %d do job %p as %d\n", id, jptr, active);
			jptr->execute(true);

			int completed = atomic_fetch_add(&(jptr->completed_count), 1) + 1;
			active = atomic_fetch_add(&(jptr->active_count), 0);
			if (active == completed)
			{
				// finished (marked by others) before `I' mark it ?
				int finished = atomic_exchange(&(jptr->finished), 1);
				if (!finished)
				{
					log_info("worker %d mark job %p and notify the main thread\n", id, jptr);
					// to avoid signal miss due pre-check condition empty
					acquire_lock(&(pool->lock_work));
					release_lock(&(pool->lock_work));
					wake_condvar(&(pool->cond_work));
				}
			}
		}
		else
		{ log_info("worker %d no more jobs\n", id); }
	}
}


TPImplement::TPImplement(int n)
{
	log_info("TPImplement: create TPImplement with n = %d\n", n);
#if defined HAVE_PTHREADS_PF
	int err = 0;
	err |= pthread_mutex_init(&lock_pool, NULL);
	err |= pthread_mutex_init(&lock_work, NULL);
	err |= pthread_cond_init(&cond_work, NULL);
	log_assert(!err && "failed to initialize TPImplement (pthreads)");
	numTrdMax = pthread_num_processors_np() * 2; // not too much
#elif defined HAVE_WIN32_THREAD
	InitializeSRWLock(&lock_pool);
	InitializeSRWLock(&lock_work);
	InitializeConditionVariable(&cond_work);
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	numTrdMax = sysinfo.dwNumberOfProcessors * 2; // 2 for SMT/HT ?
#endif
	numThread = 0;
	// see comment of `TPWorker& operator =(TPWorker&&)'
	workers.reserve(numTrdMax);
#if TP_DEBUG_JOB
	jobs_done.reserve(1024);
#endif
	set(n);
}


TPImplement::~TPImplement()
{
	int len = static_cast<int>(workers.size());
	log_info("TPImplement: destroy TPImplement with %d workers\n", len);
	for (; len > 0; --len)
		workers.pop_back();
#if defined HAVE_PTHREADS_PF
	pthread_cond_destroy(&cond_work);
	pthread_mutex_destroy(&lock_work);
	pthread_mutex_destroy(&lock_pool);
#elif defined HAVE_WIN32_THREAD
	// nothing to do
#endif
}


void TPImplement::set(int n)
{
	acquire_lock(&lock_pool);
	numThread = n;
	if (numThread < 0)
		numThread = numTrdMax / 2;
	numThread = std::min(std::max(numThread - 1, 0), numTrdMax);
	if (numThread > 0)
	{
		int org = static_cast<int>(workers.size());
		if (org != numThread)
		{
			log_info("TPImplement: change the number of workers %d -> %d\n",
				org, numThread);
		}
		// decrease
		for (; org > numThread; --org)
			workers.pop_back();
		// increase
		for (; org < numThread; ++org)
			workers.emplace_back(this, org);
	}
	release_lock(&lock_pool);
}


int TPImplement::get()
{
	int n = 1;
	acquire_lock(&lock_pool);
	n = numThread + 1;
	release_lock(&lock_pool);
	return n;
}


void TPImplement::run(Range const& range, TPLoopBody const& body)
{
	if (get() <= 1)
	{
		body(range);
		return;
	}

	acquire_lock(&lock_pool);
	TPJob job(range, body, numThread + 1);
	log_assert(numThread == static_cast<int>(workers.size()));
	for (int i = 0; i < numThread; ++i)
		workers[i].assign(&job);
	job.execute(false);
	log_assert(atomic_fetch_add(&(job.start), 0) >= range.end);
	release_lock(&lock_pool);

	int finished = atomic_fetch_add(&(job.finished), 0);
	int active = atomic_fetch_add(&(job.active_count), 0);
	// have finished job, or just the main thread is working
	if (finished || (active == 0))
	{
		log_info("TPImplement: no WIP now for job %p active %d\n", &job, active);
		atomic_exchange(&(job.finished), 1);
#if TP_DEBUG_JOB
		jobs_done.push_back(job);
#endif
		return;
	}

	log_info("TPImplement: loop (pause) for job %p\n", &job);
	// don't spin too much in any case (inaccurate getTickCount())
	for (int i = 0; i < Active_Wait; ++i)
	{
		finished = atomic_fetch_add(&(job.finished), 0);
		if (finished)
		{
			log_info("TPImplement: job %p is finished by others (pause)\n", &job);
			break;
		}
		yield_pause(16);
	}
	if (!finished)
	{
		log_info("TPImplement: wait (sleep) for job %p\n", &job);
		acquire_lock(&lock_work);
		for (;;)
		{
			finished = atomic_fetch_add(&(job.finished), 0);
			if (finished)
			{
				log_info("TPImplement: job %p is finished by others (wait)\n", &job);
				break;
			}
			log_info("TPImplement: wait (sleep)...\n");
			wait_condvar(&cond_work, &lock_work);
			log_info("TPImplement: wake\n");
		}
		release_lock(&lock_work);
	}

#if TP_DEBUG_JOB
	acquire_lock(&lock_pool);
	jobs_done.push_back(job);
	release_lock(&lock_pool);
#endif
}


#elif defined HAVE_WIN32_POOL


// no need to introduce instance and worker
static VOID CALLBACK TPJob_Func(PTP_CALLBACK_INSTANCE, PVOID vp_job, PTP_WORK)
{
	TPJob* job = static_cast<TPJob*>(vp_job);
	if (atomic_fetch_add(&(job->start), 0) < job->end)
		job->execute(true);
}


TPImplement::TPImplement(int n)
{
	int err = 0;
	pool = CreateThreadpool(NULL);
	if (!pool)
	{
		err = GetLastError();
		log_error("TPImplement: CreateThreadpool failed, err = %d\n", err);
		return;
	}
	if (SetThreadpoolThreadMinimum(pool, 1) != TRUE)
	{
		err = GetLastError();
		log_error("TPImplement: SetThreadpoolThreadMinimum failed, err = %d\n", err);
		return;
	}

	InitializeThreadpoolEnvironment(&envir);
	SetThreadpoolCallbackPool(&envir, pool);

	clnup = CreateThreadpoolCleanupGroup();
	if (clnup == NULL)
	{
		err = GetLastError();
		log_error("TPImplement: CreateThreadpoolCleanupGroup failed, err = %d\n", err);
		return;
	}
	SetThreadpoolCallbackCleanupGroup(&envir, clnup, NULL);

	InitializeSRWLock(&lock_pool);
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	numTrdMax = sysinfo.dwNumberOfProcessors * 2;
	numThread = 0;
	set(n);
}


TPImplement::~TPImplement()
{
	// Wait for any previously scheduled tasks to complete, but stop accepting new ones
	CloseThreadpoolCleanupGroupMembers(clnup, FALSE, NULL);
	// Clean-up resources:
	CloseThreadpoolCleanupGroup(clnup);
	DestroyThreadpoolEnvironment(&envir);
	CloseThreadpool(pool);
}


void TPImplement::set(int n)
{
	AcquireSRWLockExclusive(&lock_pool);
	numThread = n;
	if (numThread < 0)
		numThread = numTrdMax / 2;
	numThread = std::min(std::max(numThread - 1, 0), numTrdMax);
	if (numThread > 0)
		SetThreadpoolThreadMaximum(pool, numThread);
	ReleaseSRWLockExclusive(&lock_pool);
}


int TPImplement::get()
{
	int n = 1;
	AcquireSRWLockExclusive(&lock_pool);
	n = numThread + 1;
	ReleaseSRWLockExclusive(&lock_pool);
	return n;
}


void TPImplement::run(Range const& range, TPLoopBody const& body)
{
	if (get() <= 1)
	{
		body(range);
		return;
	}

	AcquireSRWLockExclusive(&lock_pool);
	TPJob job(range, body, numThread + 1);
	do
	{
		TP_WORK* work = CreateThreadpoolWork(TPJob_Func, &job, &envir);
		if (work == NULL)
		{
			log_error("TPImplement: CreateThreadpoolWork failed, err = %d", GetLastError());
			// try or not?
			body(range);
			job.finished = 1;
			break;
		}
		for (int i = 0; i < numThread; ++i)
			SubmitThreadpoolWork(work);
		job.execute(false);
		log_assert(job.start >= job.end);
		log_info("TPImplement: WaitForThreadpoolWorkCallbacks for job %p\n", &job);
		WaitForThreadpoolWorkCallbacks(work, FALSE);
		CloseThreadpoolWork(work);
		job.finished = 1;
		log_info("TPImplement: done for job %p\n", &job);
	} while (0);
	ReleaseSRWLockExclusive(&lock_pool);
}


#endif

}
}
#endif


//////////////////// ThreadPool ////////////////////

namespace gk
{

ThreadPool::ThreadPool(int n)
{
#if HAVE_PARALLEL_FRAMEWORK
	impl = new details::TPImplement(n);
#else 
	impl = nullptr;
#endif
	nestedbuf = 0;
	nestedptr = &nestedbuf;
}


ThreadPool::~ThreadPool()
{
#if HAVE_PARALLEL_FRAMEWORK
	delete impl;
#endif
}


void ThreadPool::set(int n)
{
#if HAVE_PARALLEL_FRAMEWORK
	int depth = atomic_fetch_add(nestedptr, 1);
	log_assert(depth == 0 && "do not change thread number when working");
	impl->set(n);
	depth = atomic_fetch_add(nestedptr, -1);
	log_assert(depth == 1 && "do not work when changing thread number");
#else 
	(void)(n);
#endif
}


int ThreadPool::get() const
{
#if HAVE_PARALLEL_FRAMEWORK
	return impl->get();
#else 
	return 1;
#endif
}


void ThreadPool::run(Range const& range, TPLoopBody const& body, bool usepar) const
{
	if (range.end <= range.start)
		return;

	// 有时候想让外面的 parallel_for 单线程跑，而内部嵌套的并行
	usepar |= (range.end == range.start + 1);
	if (!usepar)
	{
		body(range);
		return;
	}

#if HAVE_PARALLEL_FRAMEWORK

	int depth = atomic_fetch_add(nestedptr, 1);
	do
	{
		if (depth != 0)
		{
			body(range);
			break;
		}
		impl->run(range, body);
	} while (0);
	atomic_fetch_add(nestedptr, -1);

#else 

	body(range);

#endif
}


static ThreadPool sgThreadPool;
}

//////////////////// 函数 ////////////////////

namespace gk
{
void set_num_thread(int n)
{
	sgThreadPool.set(n);
}

int get_num_thread()
{
	return sgThreadPool.get();
}

void parallel_for(Range const& range, TPLoopBody const& body, bool usepar)
{
	sgThreadPool.run(range, body, usepar);
}
}
