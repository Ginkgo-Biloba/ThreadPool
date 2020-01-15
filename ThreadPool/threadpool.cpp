#include <algorithm>
#include <vector>
#include "ThreadPool.hpp"
using std::vector;

#undef NDEBUG
#include <cassert>

/*
* 0. None
* 1. Parallel Patterns Library
*/
#ifndef HAVE_PARALLEL_FRAMEWORK
#  define HAVE_PARALLEL_FRAMEWORK 1
#endif

#if HAVE_PARALLEL_FRAMEWORK == 0
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_NONE")
#elif HAVE_PARALLEL_FRAMEWORK == 1
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_PTHREADS_PF")
#  define HAVE_PTHREADS_PF
#else
#  error must select one implementation
#endif

/* IMPORTANT: always use the same order of defines
- HAVE_PTHREADS_PF - pthreads if available
*/

#if defined HAVE_PTHREADS_PF
#include <pthread.h>
#endif

#if defined _WIN32 || defined WINCE
#include <Windows.h>
#undef small
#undef min
#undef max
#undef abs
#endif

#if defined __linux__ || defined __APPLE__ || defined __GLIBC__ || defined __HAIKU__
#  include <unistd.h>
#  include <stdio.h>
#  include <sys/types.h>
#  if defined __ANDROID__
#    include <sys/sysconf.h>
#  elif defined __APPLE__
#    include <sys/sysctl.h>
#  endif
#endif

#if 0
#   define  GK_Printf(...)
#elif defined __ANDROID__
#  include <android/log.h>
#  define GK_Printf(...) __android_log_print(ANDROID_LOG_INFO, "GK_Printf", __VA_ARGS__)
#else
#  include <cstdio>
#  define GK_Printf(...) printf(__VA_ARGS__)
#endif

//////////////////// TPImplement ////////////////////

#if HAVE_PARALLEL_FRAMEWORK

// Spin lock's CPU-level yield (required for Hyper-Threading)
#ifndef CV_PAUSE
# if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#   if !defined(__SSE__)
static inline void cv_non_sse_mm_pause() { __asm__ __volatile__("rep; nop"); }
#     define _mm_pause cv_non_sse_mm_pause
#   endif
#   define CV_PAUSE(v) do { for (int __delay = (v); __delay > 0; --__delay) { _mm_pause(); } } while (0)
# elif defined __GNUC__ && defined __aarch64__
#   define CV_PAUSE(v) do { for (int __delay = (v); __delay > 0; --__delay) { asm volatile("yield" ::: "memory"); } } while (0)
# elif defined __GNUC__ && defined __arm__
#   define CV_PAUSE(v) do { for (int __delay = (v); __delay > 0; --__delay) { asm volatile("" ::: "memory"); } } while (0)
# elif defined __GNUC__ && defined __PPC64__
#   define CV_PAUSE(v) do { for (int __delay = (v); __delay > 0; --__delay) { asm volatile("or 27,27,27" ::: "memory"); } } while (0)
# elif defined _WIN32 && (defined _M_IX86 || defined _M_X64)
#   define CV_PAUSE(v) do { for (int __delay = (v); __delay > 0; --__delay) { _mm_pause(); } } while (0)
# else
#   warning "Can't detect 'pause' (CPU-yield) instruction on the target platform. Specify CV_PAUSE(v) definition via compiler flags."
#   define CV_PAUSE(...) do { /* no-op: works, but not effective */ } while (0)
# endif
#endif // CV_PAUSE


namespace gk
{

//////////////////// exchange-add (fetch and add) ////////////////////

#if defined __GNUC__ || defined __clang__
inline int fetch_add(int* addr, int delta)
{
#  if defined __clang__ && __clang_major__ >= 3 && !defined __ANDROID__ && !defined __EMSCRIPTEN__ && !defined(__CUDACC__)
#    ifdef __ATOMIC_ACQ_REL
	return __c11_atomic_fetch_add((_Atomic(int)*)(addr), delta, __ATOMIC_ACQ_REL);
#    else
	return __atomic_fetch_add((_Atomic(int)*)(addr), delta, 4);
#    endif
#  else
#    if defined __ATOMIC_ACQ_REL && !defined __clang__
	// version for gcc >= 4.7
	return __atomic_fetch_add(addr, delta, __ATOMIC_ACQ_REL);
#    else
	return __sync_fetch_and_add(addr, delta);
#    endif
#  endif
}
#elif defined _MSC_VER && !defined RC_INVOKED
#include <intrin.h>
inline int fetch_add(int* addr, int delta)
{
	return static_cast<int>(
		_InterlockedExchangeAdd(reinterpret_cast<long volatile*>(addr), delta));
}
#else
// 这里不支持原子操作，引发编译错误
inline int fetch_add(int* addr, int delta)
{
	int tmp = *addr; *addr += delta; // return "error";
}
#endif

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
		assert(2 <= n);
		start = range.start; end = range.end;
		chunk = (end - start + n - 1) / n;
		// make one task smaller
		chunk = (chunk + 7) / 8;
		// if assert failed, consider scale or offset the range
		assert(static_cast<int64_t>(end) - start < INT_MAX - chunk);
		assert(static_cast<int64_t>(end) < INT_MAX - n * chunk);
		finished = completed_count = active_count = 0;
	}

	~TPJob()
	{
		assert(fetch_add(&finished, 0));
	}

	int execute(bool spawned)
	{
		int task = 0;
		Range R;
		for (;;)
		{
			R.start = fetch_add(&start, chunk);
			if (R.start >= end)
				break;
			R.end = std::min(R.start + chunk, end);
			task += (R.end - R.start);
			body(R);
		}
		int fin = fetch_add(&finished, 0);
		if (spawned && fin)
		{
			GK_Printf("xxxxx BUG xxxxxx\n\tjob %p %d, %d, %d\n",
				this, R.start, active_count, completed_count);
			assert(!fin);
		}
		return task;
	}
};


namespace details
{

//////////////////// TPWorker ////////////////////

class TPWorker
{
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
	// the index in the vector `TPImplement::workers'
	int const id;
	int created, stoped;
	int wake_signal;

#if defined HAVE_PTHREADS_PF
	pthread_mutex_t mutex_job;
	pthread_cond_t cond_job;
	pthread_t posix_thread;
#endif

	TPWorker(TPWorker&&) = default;
	TPWorker(TPImplement* pppl, int id);
	~TPWorker();
	void assign(TPJob* job);
	void loop();
};


//////////////////// TPImplement ////////////////////

class TPImplement
{
	// numThread 是子线程的数量
	int numTrdMax, numThread;
	std::vector<TPWorker> workers;

public:
#if defined HAVE_PTHREADS_PF
	pthread_mutex_t mutex_pool;
	pthread_mutex_t mutex_work;
	pthread_cond_t cond_work;
#endif

	TPImplement(int n);
	~TPImplement();
	void set(int n);
	int get();
	void run(Range const& range, TPLoopBody const& body);
};


//////////////////// PThreads ////////////////////

#if defined HAVE_PTHREADS_PF

static void* TPWorker_Func(void* worker)
{
	static_cast<TPWorker*>(worker)->loop();
	return worker;
}


TPWorker::TPWorker(TPImplement* p, int i)
	: pool(p), job(nullptr), id(i), created(0), stoped(0), wake_signal(0)
{
	assert(p && "must work with TPImplement");
	GK_Printf("MainThread: create worker %d\n", id);
	int err = 0;
	err = pthread_mutex_init(&mutex_job, NULL);
	if (err != 0)
	{
		GK_Printf("worker %d can not init mutex, err = %d\n", id, err);
		assert(err == 0); // TODO
		return;
	}
	err = pthread_cond_init(&cond_job, NULL);
	if (err != 0)
	{
		GK_Printf("worker %d can not init cond, err = %d\n", id, err);
		assert(err == 0); // TODO
		return;
	}
	err = pthread_create(&posix_thread, NULL, TPWorker_Func, this);
	if (err != 0)
	{
		GK_Printf("worker %d can not create mutex, err = %d\n", id, err);
		return;
	}
	created = 1;
}


TPWorker::~TPWorker()
{
	GK_Printf("MainThread: destroy worker %d\n", id);
	if (created)
	{
		pthread_mutex_lock(&mutex_job);
		assert(!job && !stoped);
		stoped = 1;
		wake_signal = 1;
		pthread_mutex_unlock(&mutex_job);
		pthread_cond_signal(&cond_job);
		pthread_join(posix_thread, NULL);
	}
	pthread_cond_destroy(&cond_job);
	pthread_mutex_destroy(&mutex_job);
}


void TPWorker::assign(TPJob* jptr)
{
	if (created == 0 || stoped == 1)
		return;

	assert(!job && "in working");
	pthread_mutex_lock(&mutex_job);
	job = jptr;
	wake_signal = 1;
	pthread_mutex_unlock(&mutex_job);
	pthread_cond_signal(&cond_job);
}


void TPWorker::loop()
{
	// assert(!job);
	int const active_wait = 1024;
	bool allow_active_wait = true;

	while (!stoped)
	{
		if (allow_active_wait)
		{
			GK_Printf("worker %d pause\n", id);
			allow_active_wait = false;
			for (int i = 0; i < active_wait; ++i)
			{
				if (fetch_add(&wake_signal, 0))
					break;
				CV_PAUSE(16);
			}
		}

		pthread_mutex_lock(&mutex_job);
		while (!wake_signal)
		{
			GK_Printf("worker %d wait (sleep)...\n", id);
			pthread_cond_wait(&cond_job, &mutex_job);
		}
		allow_active_wait = true;
		TPJob* jptr = job;
		job = nullptr;
		wake_signal = 0;
		pthread_mutex_unlock(&mutex_job);
		if (stoped)
			break;

		if (jptr && (fetch_add(&(jptr->start), 0) < jptr->end))
		{
			int active = fetch_add(&(jptr->active_count), 1);
			GK_Printf("worker %d do job %p as %d\n", id, jptr, active);
			jptr->execute(true);

			int completed = fetch_add(&(jptr->completed_count), 1) + 1;
			active = fetch_add(&(jptr->active_count), 0);
			if (active == completed)
			{
				bool send_signal = !(fetch_add(&(jptr->finished), 1));
				if (send_signal)
				{
					GK_Printf("worker %d mark job %p and notify the main thread\n", id, jptr);
					// to avoid signal miss due pre-check condition empty
					pthread_mutex_lock(&(pool->mutex_work));
					pthread_mutex_unlock(&(pool->mutex_work));
					pthread_cond_signal(&(pool->cond_work));
				}
			}
		}
		else
		{
			GK_Printf("worker %d no more jobs\n", id);
		}
	}
}


TPImplement::TPImplement(int n)
{
	GK_Printf("MainThread: create TPImplement with n = %d\n", n);
	int err = 0;
	err |= pthread_mutex_init(&mutex_pool, NULL);
	err |= pthread_mutex_init(&mutex_work, NULL);
	err |= pthread_cond_init(&cond_work, NULL);
	assert(!err && "failed to initialize TPImplement (pthreads)");
	numTrdMax = pthread_num_processors_np();
	numThread = 0;
	// see comment of `TPWorker& operator =(TPWorker&&)'
	workers.reserve(numTrdMax);
	set(n);
}


TPImplement::~TPImplement()
{
	int len = static_cast<int>(workers.size());
	GK_Printf("MainThread: destroy TPImplement with %d workers\n", len);
	for (; len > 0; --len)
		workers.pop_back();
	pthread_cond_destroy(&cond_work);
	pthread_mutex_destroy(&mutex_work);
	pthread_mutex_destroy(&mutex_pool);
}


void TPImplement::set(int n)
{
	pthread_mutex_lock(&mutex_pool);
	numThread = n;
	if (numThread < 0)
		numThread = numTrdMax;
	numThread = std::max(numThread - 1, 0);
	if (numThread > 0)
	{
		int org = static_cast<int>(workers.size());
		// decrease
		for (; org > numThread; --org)
			workers.pop_back();
		// increase
		for (; org < numThread; ++org)
			workers.emplace_back(this, org);
	}
	pthread_mutex_unlock(&mutex_pool);
}


int TPImplement::get()
{
	int n = 1;
	pthread_mutex_lock(&mutex_pool);
	n = numThread + 1;
	pthread_mutex_unlock(&mutex_pool);
	return n;
}


void TPImplement::run(Range const& range, TPLoopBody const& body)
{
	if (numThread == 0)
	{
		body(range);
		return;
	}

	int const active_wait = 10240;
	pthread_mutex_lock(&mutex_pool);
	TPJob job(range, body, numThread);
	assert(numThread == static_cast<int>(workers.size()));
	pthread_mutex_unlock(&mutex_pool);

	for (int i = 0; i < numThread; ++i)
		workers[i].assign(&job);
	job.execute(false);
	assert(fetch_add(&(job.start), 0) >= range.end);

	int finished = fetch_add(&(job.finished), 0);
	int active = fetch_add(&(job.active_count), 0);
	// have finished job, or just main thread is working
	if (finished || active == 0)
	{
		// due to the nested check in ThreadPool::run, so should not run in here
		assert(active && "MainThread: no WIP worker threads\n");
		fetch_add(&(job.finished), 1);
		return;
	}

	if (active_wait > 0)
	{
		GK_Printf("MainThread: prepare pause job %p\n", &job);
		// don't spin too much in any case (inaccurate getTickCount())
		for (int i = 0; i < active_wait; ++i)
		{
			finished = fetch_add(&(job.finished), 0);
			if (finished)
			{
				GK_Printf("MainThread: job %p is finished by others (pause)\n", &job);
				break;
			}
			CV_PAUSE(16);
		}
	}
	if (!finished)
	{
		GK_Printf("MainThread: prepare wait job %p\n", &job);
		pthread_mutex_lock(&mutex_work);
		for(;;)
		{
			finished = fetch_add(&(job.finished), 0);
			if (finished)
			{
				GK_Printf("MainThread: job %p is finished by others (wait)\n", &job);
				break;
			}
			GK_Printf("MainThread: wait (sleep)...\n");
			pthread_cond_wait(&cond_work, &mutex_work);
			GK_Printf("MainThread: wake\n");
		}
		pthread_mutex_unlock(&mutex_work);
	}
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
	int depth = fetch_add(nestedptr, 1);
	assert(depth == 0 && "do not change thread number when working");
	impl->set(n);
	depth = fetch_add(nestedptr, -1);
	assert(depth == 1 && "do not work when changing thread number");
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
	if (range.start >= range.end)
		return;

	// 有时候想让外面的 parallel_for 单线程跑，而内部嵌套的并行
	if (!usepar)
	{
		body(range);
		return;
	}

#if HAVE_PARALLEL_FRAMEWORK

	int depth = fetch_add(nestedptr, 1);
	do
	{
		if (depth != 0)
		{
			body(range);
			break;
		}
		impl->run(range, body);
	} while (0);
	fetch_add(nestedptr, -1);

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
