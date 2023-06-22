#pragma once

#include <cstdio>

#if defined _M_X64 || defined __x86_64__ || defined __aarch64__ || defined _M_ARM64
#	define GK_M_64 1
#else
#	define GK_M_64 0
#endif

#if defined __i386__ || defined __x86_64__ || defined _M_IX86 || defined _M_X64
#	include <emmintrin.h> // for _mm_pause
#endif

#ifdef _MSC_VER
#	define GK_Func __FUNCTION__
#	define GK_Trap abort()
#elif defined __GNUC__
#	define GK_Func __PRETTY_FUNCTION__
#	define GK_Trap __builtin_trap()
#else
#	define GK_Func "Can_Not_Get_Func_Name"
#endif

#define log_info(...)                            \
	do {                                           \
		char _buf[1024];                             \
		snprintf(_buf, sizeof(_buf), ##__VA_ARGS__); \
		fputs(_buf, stderr);                         \
	} while (0)

#define log_error(...)                                         \
	do {                                                         \
		char _buf[1024];                                           \
		size_t _idx = snprintf(_buf, sizeof(_buf) - 16,            \
			"ERROR on file %s, func %s, line %d\n",                  \
			__FILE__, GK_Func, __LINE__);                            \
		snprintf(_buf + _idx, sizeof(_buf) - _idx, ##__VA_ARGS__); \
		fputs(_buf, stderr);                                       \
		GK_Trap;                                                   \
	} while (0)

#define log_assert(expr) \
	do {                   \
		if (!(expr))         \
			log_error(#expr);  \
	} while (0)

/*
IMPORTANT: always use the same order of defines
0. HAVE_NONE
1. HAVE_PTHREADS_PF
	- POSIX Threads
2. HAVE_WIN32_THREAD
	- Windows CRT Thread (SRWLOCK + CONDITION_VARIABLE)
	- Available on Windows Vista and later
*/
#ifndef HAVE_PARALLEL_FRAMEWORK
#	if defined _MSC_VER
#		define HAVE_PARALLEL_FRAMEWORK 2
#	elif defined __GNUC__
#		define HAVE_PARALLEL_FRAMEWORK 1
#	else
#		define HAVE_PARALLEL_FRAMEWORK 0
#	endif
#endif

#if HAVE_PARALLEL_FRAMEWORK == 0
// #	pragma message("HAVE_PARALLEL_FRAMEWORK => HAVE_NONE")
#elif HAVE_PARALLEL_FRAMEWORK == 1
// #	pragma message("HAVE_PARALLEL_FRAMEWORK => HAVE_PTHREADS_PF")
#	define HAVE_PTHREADS_PF
#elif HAVE_PARALLEL_FRAMEWORK == 2
// #	pragma message("HAVE_PARALLEL_FRAMEWORK => HAVE_WIN32_THREAD")
#	define HAVE_WIN32_THREAD
#else
#	error must select one implementation
#endif

#if defined HAVE_PTHREADS_PF

#	include <pthread.h>

namespace gk
{
inline void acquire_lock(pthread_mutex_t* lock)
{
	log_assert(!pthread_mutex_lock(lock));
}

inline void release_lock(pthread_mutex_t* lock)
{
	log_assert(!pthread_mutex_unlock(lock));
}

inline void sleep_lock(pthread_cond_t* cond, pthread_mutex_t* lock)
{
	log_assert(!pthread_cond_wait(cond, lock));
}

inline void wake_cond(pthread_cond_t* cond)
{
	log_assert(!pthread_cond_signal(cond));
}

inline void wake_all_cond(pthread_cond_t* cond)
{
	log_assert(!pthread_cond_broadcast(cond));
}
}

#elif defined HAVE_WIN32_THREAD || defined HAVE_WIN32_POOL

#	define WIN32_LEAN_AND_MEAN
#	define NOCOMM
#	undef NOMINMAX // mingw-w64 redefined
#	define NOMINMAX
#	include <Windows.h>
#	ifdef HAVE_WIN32_THREAD
#		include <process.h>
#	endif

namespace gk
{
inline void acquire_lock(SRWLOCK* lock)
{
	AcquireSRWLockExclusive(lock);
}

inline void release_lock(SRWLOCK* lock)
{
	ReleaseSRWLockExclusive(lock);
}

inline void sleep_lock(CONDITION_VARIABLE* cond, SRWLOCK* lock)
{
	SleepConditionVariableSRW(cond, lock, INFINITE, 0);
}

inline void wake_cond(CONDITION_VARIABLE* cond)
{
	WakeConditionVariable(cond);
}

inline void wake_all_cond(CONDITION_VARIABLE* cond)
{
	WakeAllConditionVariable(cond);
}
}

#endif

namespace gk
{
// Spin lock's CPU-level yield (required for Hyper-Threading)
inline void yield_pause(int delay)
{
	// clang-format off
	for (; delay > 0; --delay)
	{
#ifdef YIELD_PAUSE
		YIELD_PAUSE;
#elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#	if !defined(__SSE2__)
		__asm__ __volatile__("rep; nop");
#	else
		_mm_pause();
#	endif
#elif defined __GNUC__ && defined __aarch64__
		__asm__ __volatile__("yield" ::: "memory");
#elif defined __GNUC__ && defined __arm__
		__asm__ __volatile__("" ::: "memory");
#elif defined __GNUC__ && defined __mips__ && __mips_isa_rev >= 2
		__asm__ __volatile__("pause" ::: "memory");
#elif defined __GNUC__ && defined __PPC64__
		__asm__ __volatile__("or 27,27,27" ::: "memory");
#elif defined _MSC_VER && (defined _M_IX86 || defined _M_X64)
		_mm_pause();
#elif defined _MSC_VER && (defined _M_ARM || defined M_ARM64)
		__nop();
#else
		(void)(delay);
#	warning "can't detect `pause' (CPU-yield) instruction on the target platform, \
		specify YIELD_PAUSE definition via compiler flags"
#endif
		// clang-format on
	}
}
}
