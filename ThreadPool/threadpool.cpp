#include "ThreadPool.hpp"

#include <algorithm>
#include <vector>
using std::vector;

/*
* 0. None
* 1. OpenMP
* 2. Parallel Patterns Library
* 3. POSIX Threads
*/
#ifndef HAVE_PARALLEL_FRAMEWORK
#  define HAVE_PARALLEL_FRAMEWORK 2
#endif

#if HAVE_PARALLEL_FRAMEWORK == 1
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_OPENMP")
#  define HAVE_OPENMP
#elif HAVE_PARALLEL_FRAMEWORK == 2
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_CONCURRENCY")
#  define HAVE_CONCURRENCY
#elif HAVE_PARALLEL_FRAMEWORK == 3
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_PTHREADS_PF")
#  define HAVE_PTHREADS_PF
#else
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => 0")
#  undef HAVE_PARALLEL_FRAMEWORK
#  define HAVE_PARALLEL_FRAMEWORK 0
#endif

/* IMPORTANT: always use the same order of defines
- HAVE_OPENMP      - integrated to compiler, should be explicitly enabled
- HAVE_CONCURRENCY - part of runtime, used automatically (Windows only, MSVS 11)
- HAVE_PTHREADS_PF - pthreads if available
*/

#if defined HAVE_OPENMP
#include <omp.h>
#elif defined HAVE_CONCURRENCY
#include <ppl.h>
#elif defined HAVE_PTHREADS_PF
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

#if defined __GNUC__ || defined __clang__
#  define GK_Func __PRETTY_FUNCTION__
#elif defined _MSC_VER
#  define GK_Func __FUNCTION__
#else
#  define GK_Func ""
#endif


static void gk_error(char const* info, char const* func, char const* file, int line)
{
	char msg[1 << 14];
	snprintf(msg, sizeof(msg),
		"%s in %s, file %s, line %d",
		info, func, file, line);
	fprintf(stderr, "gk::error(): %s\n", msg);
	fflush(stderr);
#if defined __ANDROID__
	__android_log_print(ANDROID_LOG_ERROR, "gk::error()", "%s", msg);
#endif

	// line 总为真，这样可以抑制警告
	if (line)
	{
#if _HAS_EXCEPTIONS
		throw msg;
#else
		static char volatile* p = reinterpret_cast<char*>(0x2b);
		*p = 0x2b; // 引发异常
#endif
	}
}


/* 表达式为假，则调用 error */
#define GK_Assert(expr) if (!!(expr)); \
		else gk_error("GK_Assert failed: " #expr, GK_Func, __FILE__, __LINE__)

#ifdef _DEBUG
#  define GK_DbgAssert(expr) GK_Assert(expr)
#else
#  define GK_DbgAssert(expr)
#endif


//////////////////// TPImplement ////////////////////

namespace gk
{
#if HAVE_PARALLEL_FRAMEWORK

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

//////////////////// 辅助函数 ////////////////////

inline bool is_sequential(int n)
{
	return 0 <= n && n <= 1;
}


class TPJob
{
	TPLoopBody const& body;
	int64_t start, end;

public:
	int64_t stripe, slen;

	TPJob(Range const& range, TPLoopBody const& b, int n)
		: body(b)
	{
		start = range.start; end = range.end;
		stripe = (end - start + n - 1) / n;
		// 各个任务碎一点？同时为了让 stripe slen 可以为 int
		stripe = (stripe + 7) / 8;
		slen = (stripe - 1 + range.end - range.start) / stripe;
		// +1 为了考虑 for(;;++i)
		GK_Assert(stripe + n < INT_MAX);
		GK_Assert(slen + n < INT_MAX);
	}

	void operator ()(int i) const
	{
		int64_t L = start + stripe * i;
		int64_t R = std::min(L + stripe, end);
		body(Range(static_cast<int>(L), static_cast<int>(R)));
	}
};


namespace details
{

#if defined HAVE_OPENMP

//////////////////// OpenMP ////////////////////

class TPImplement
{
	int numTrdMax, numThread;

public:
	TPImplement(int n = -1)
	{
		numTrdMax = omp_get_max_threads();
		set(n);
	}

	~TPImplement() {}

	void set(int n)
	{
		numThread = n;
		if (numThread < 0)
			numThread = numTrdMax;
	}

	int get()
	{
		return std::max(numThread, 1);
	};

	void run(Range const& range, TPLoopBody const& body) const
	{
		if (is_sequential(numThread))
		{
			body(range);
			return;
		}

		TPJob job(range, body, numThread);
		int slen = static_cast<int>(job.slen);
		// 只能动态调整，因为 omp_set_num_thread 是全局的
#pragma omp parallel for schedule(dynamic) num_threads(numThread)
		for (int i = 0; i < slen; ++i)
			job(i);
	}
};


#elif defined HAVE_CONCURRENCY

//////////////////// PPL ////////////////////


class TPImplement
{
	int numTrdMax, numThread;
	Concurrency::Scheduler* schd;
	Range range;

	void releaseSchd()
	{
		if (schd)
			schd->Release();
		schd = nullptr;
	}

public:
	TPImplement(int n = -1)
		: schd(nullptr)
	{
		unsigned trdmax = 64; // TODO ?
		numTrdMax = std::min<unsigned>(trdmax, INT_MAX);
		set(n);
	}

	~TPImplement()
	{
		GK_DbgAssert(schd);
		releaseSchd();
	}

	void set(int n)
	{
		numThread = n;
		if (is_sequential(numThread))
			return;
		releaseSchd();
		if (numThread > 1)
		{
			numThread = std::min(n, numTrdMax);
			schd = Concurrency::Scheduler::Create(Concurrency::SchedulerPolicy(2,
				Concurrency::MinConcurrency, numThread,
				Concurrency::MaxConcurrency, numThread));
		}
		else
		{
			// https://docs.microsoft.com/en-us/cpp/parallel/concrt/reference/currentscheduler-class?view=vs-2019#get
			// Get 会创建默认的 Scheduler，所以这里自己创建一个
			schd = Concurrency::Scheduler::Create(Concurrency::SchedulerPolicy());
			numThread = schd->GetNumberOfVirtualProcessors();
		}
	}

	int get()
	{
		GK_DbgAssert(schd);
		return std::max(numThread, 1);
	};

	void run(Range const& range, TPLoopBody const& body) const
	{
		if (is_sequential(numThread))
		{
			body(range);
			return;
		}

		GK_DbgAssert(schd);
		TPJob job(range, body, numThread);
		int slen = static_cast<int>(job.slen);
		schd->Attach();
		Concurrency::parallel_for(0, slen, job);
		Concurrency::CurrentScheduler::Detach();
	}
};


#elif defined HAVE_PTHREADS_PF

//////////////////// PThreads ////////////////////

class TPWorker
{
	TPWorker(TPWorker const&) = delete;
public:
};


class TPImplement
{
	int numTrdMax, numThread;
	std::vector<TPWorker*> threads;

public:
	TPImplement(int n)
	{
		numTrdMax = pthread_num_processors_np();
		numThread = 0;
		set(n);
	}
	~TPImplement() {}

	void set(int n)
	{
		if (n < 0)
			n = p;
	}
	int get();
	void run(Range const& range, TPLoopBody const& body, int nstripe) const;
};

#endif

}

#else // HAVE_PARALLEL_FRAMEWORK == 0


namespace details
{
class TPImplement
{
public:
	TPImplement(int) {}
	void set(int) {}
	int get() { return 1; };
	void run(Range const& range, TPLoopBody const& body, int) const
	{
		body(range);
	}
};
}

#endif
}


//////////////////// ThreadPool ////////////////////

namespace gk
{
ThreadPool::ThreadPool(int n)
{
	impl = new details::TPImplement(n);
	nestedbuf = 0;
	nestedptr = &nestedbuf;
}


ThreadPool::~ThreadPool()
{
	delete impl;
}

void ThreadPool::set(int n)
{
	impl->set(n);

}

int ThreadPool::get()
{
	return impl->get();
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
	if (depth != 0)
	{
		fetch_add(nestedptr, -1);
		body(range);
		return;
	}
	impl->run(range, body);
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
int get_thread_id()
{
#if HAVE_PARALLEL_FRAMEWORK
#if defined HAVE_OPENMP

	return omp_get_thread_num();

#elif defined HAVE_CONCURRENCY

	// zero for master thread, unique number for others but not necessary 1,2,3,...
	int id = Concurrency::Context::VirtualProcessorId();
	return std::max(0, id);

#elif defined HAVE_PTHREADS_PF

	

#endif
#else 

	return 0;

#endif
}

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
