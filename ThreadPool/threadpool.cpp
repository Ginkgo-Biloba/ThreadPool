#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include "threadpool.hpp"
#include "atomicop.hpp"
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

#if defined _WIN32 || defined _WINCE
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

#if defined __ANDROID__
#  include <android/log.h>
#endif

#if defined __i386__ || defined __x86_64__ || defined _M_IX86 || defined _M_X64
#  include <emmintrin.h> // for _mm_pause
#endif


#if HAVE_PARALLEL_FRAMEWORK

//////////////////// TPImplement ////////////////////

namespace gk
{

// Spin lock's CPU-level yield (required for Hyper-Threading)
inline void nop_pause(int delay)
{
	for (; delay > 0; --delay)
	{
#ifdef GK_Pause
		GK_Pause(v)
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
#elif defined __GNUC__ && defined __PPC64__
		asm volatile("or 27,27,27" ::: "memory");
#elif defined _MSC_VER && (defined _M_IX86 || defined _M_X64)
		_mm_pause();
#elif defined _MSC_VER && (defined _M_ARM || defined M_ARM64)
		__nop();
#else
#  warning "can't detect `pause' (CPU-yield) instruction on the target platform, \
		specify GK_Pause(v) definition via compiler flags"
# endif
	}
}

// printf and fprintf is not thread safe in GCC
static void log_info(char const* fmt, ...)
{
#if 0
	char buf[1 << 16];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;
	fputs(buf, stdout);
	// fflush(stdout);
#	if defined __ANDROID__
	__android_log_print(ANDROID_LOG_INFO, "gk::log_info", buf);
#	endif
#else
	(void)(fmt);
#endif
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
		assert(2 <= n);
		start = range.start; end = range.end;
		chunk = (end - start + n - 1) / n;
		// make one task smaller
		chunk = (chunk + 7) / 8;
		// if assert failed, consider scale or offset the range
		assert(static_cast<int64_t>(end) - start < INT_MAX - n);
		assert(static_cast<int64_t>(end) < INT_MAX - n * chunk);
		finished = completed_count = active_count = 0;
	}

	~TPJob()
	{
		assert(atomic_fetch_add(&finished, 0));
		assert(atomic_fetch_add(&start, 0) >= end);
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
			log_info("xxxxx BUG xxxxxx\n\tjob %p %d, %d, %d\n",
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

#define TP_DEBUG_JOB 0

class TPImplement
{
	// numThread 是子线程的数量
	int numTrdMax, numThread;
	std::vector<TPWorker> workers; // 子线程

#if TP_DEBUG_JOB
	// 调试用，悬垂引用什么的都不是事
	std::vector<TPJob> jobs_done;
#endif

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
	int err = 0;
	assert(p && "must work with TPImplement");
	log_info("TPImplement: create worker %d\n", id);
	err = pthread_mutex_init(&mutex_job, NULL);
	if (err != 0)
	{
		log_info("worker %d can not init mutex, err = %d\n", id, err);
		assert(err == 0); // TODO
		return;
	}
	err = pthread_cond_init(&cond_job, NULL);
	if (err != 0)
	{
		log_info("worker %d can not init cond, err = %d\n", id, err);
		assert(err == 0); // TODO
		return;
	}
	err = pthread_create(&posix_thread, NULL, TPWorker_Func, this);
	if (err != 0)
	{
		log_info("worker %d can not create mutex, err = %d\n", id, err);
		return;
	}
	created = 1;
}


TPWorker::~TPWorker()
{
	log_info("TPImplement: destroy worker %d\n", id);
	if (created)
	{
		pthread_mutex_lock(&mutex_job);
		assert(!stoped);
		job = nullptr;
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
	if (created == 0)
		return;
	log_info("worker %d is assigned job %p\n", id, jptr);
	assert(!stoped && "has stoped");
	pthread_mutex_lock(&mutex_job);
	// job may has been finished before `I' wake and do it
	// assert(!job && "in working");
	// if use shared_ptr<TPJob>, here we can check if job is finished
	// otherwise this check is done in ~TPJob()
	// assert(!job || job->finished);
	job = jptr;
	pthread_mutex_unlock(&mutex_job);
	atomic_exchange(&wake_signal, 1);
	pthread_cond_signal(&cond_job);
}


void TPWorker::loop()
{
	// 立即执行？可能没有主线程安排任务速度快
	// assert(!job && "worker start just now");
	log_info("worker %d start now\n", id);
	int const active_wait = 1024;

	while (!stoped)
	{
		if (0 < active_wait)
		{
			log_info("worker %d pause\n", id);
			for (int i = 0; i < active_wait; ++i)
			{
				if (atomic_fetch_add(&wake_signal, 0))
					break;
				nop_pause(16);
			}
		}

		pthread_mutex_lock(&mutex_job);
		while (!wake_signal)
		{
			log_info("worker %d wait (sleep)...\n", id);
			pthread_cond_wait(&cond_job, &mutex_job);
		}
		TPJob* jptr = job;
		job = nullptr;
		wake_signal = 0;
		pthread_mutex_unlock(&mutex_job);
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
					pthread_mutex_lock(&(pool->mutex_work));
					pthread_mutex_unlock(&(pool->mutex_work));
					pthread_cond_signal(&(pool->cond_work));
				}
			}
		}
		else
		{
			log_info("worker %d no more jobs\n", id);
		}
	}
}


TPImplement::TPImplement(int n)
{
	log_info("TPImplement: create TPImplement with n = %d\n", n);
	int err = 0;
	err |= pthread_mutex_init(&mutex_pool, NULL);
	err |= pthread_mutex_init(&mutex_work, NULL);
	err |= pthread_cond_init(&cond_work, NULL);
	assert(!err && "failed to initialize TPImplement (pthreads)");
	numTrdMax = pthread_num_processors_np();
	numThread = 0;
	// see comment of `TPWorker& operator =(TPWorker&&)'
	workers.reserve(numTrdMax);
#if TP_DEBUG_JOB
	jobs_done.reserve(1024);
#endif // 
	set(n);
}


TPImplement::~TPImplement()
{
	int len = static_cast<int>(workers.size());
	log_info("TPImplement: destroy TPImplement with %d workers\n", len);
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
	if (get() <= 1)
	{
		body(range);
		return;
	}

	int const active_wait = 10240;
	pthread_mutex_lock(&mutex_pool);
	TPJob job(range, body, numThread + 1);
	assert(numThread == static_cast<int>(workers.size()));
	for (int i = 0; i < numThread; ++i)
		workers[i].assign(&job);
	job.execute(false);
	assert(atomic_fetch_add(&(job.start), 0) >= range.end);
	pthread_mutex_unlock(&mutex_pool);

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

	if (active_wait > 0)
	{
		log_info("TPImplement: prepare pause job %p\n", &job);
		// don't spin too much in any case (inaccurate getTickCount())
		for (int i = 0; i < active_wait; ++i)
		{
			finished = atomic_fetch_add(&(job.finished), 0);
			if (finished)
			{
				log_info("TPImplement: job %p is finished by others (pause)\n", &job);
				break;
			}
			nop_pause(16);
		}
	}
	if (!finished)
	{
		log_info("TPImplement: prepare wait job %p\n", &job);
		pthread_mutex_lock(&mutex_work);
		for (;;)
		{
			finished = atomic_fetch_add(&(job.finished), 0);
			if (finished)
			{
				log_info("TPImplement: job %p is finished by others (wait)\n", &job);
				break;
			}
			log_info("TPImplement: wait (sleep)...\n");
			pthread_cond_wait(&cond_work, &mutex_work);
			log_info("TPImplement: wake\n");
		}
		pthread_mutex_unlock(&mutex_work);
	}

#if TP_DEBUG_JOB
	pthread_mutex_lock(&mutex_pool);
	jobs_done.push_back(job);
	pthread_mutex_unlock(&mutex_pool);
#endif
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
	assert(depth == 0 && "do not change thread number when working");
	impl->set(n);
	depth = atomic_fetch_add(nestedptr, -1);
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
