#pragma once

#include "atomic.hpp"
#include <cstdio>
#include <cstdlib>
#include <type_traits>

#if defined __i386__ || defined __x86_64__ || defined _M_IX86 || defined _M_X64
#	include <emmintrin.h> // for _mm_pause
#endif

#ifdef _MSC_VER
#	include <intrin.h>
#	define GK_Func __FUNCSIG__
#	define GK_Trap __debugbreak()
#elif defined __GNUC__
#	define GK_Func __PRETTY_FUNCTION__
#	define GK_Trap __builtin_trap()
#else
#	define GK_Func __FUNC__
#	define GK_Trap abort()
#endif

#define log_info(...)                            \
	do {                                           \
		char _buf[1024];                             \
		snprintf(_buf, sizeof(_buf), ##__VA_ARGS__); \
		fputs(_buf, stdout);                         \
	} while (0)

#define log_error(...)                                         \
	do {                                                         \
		char _buf[1024];                                           \
		size_t _idx = snprintf(_buf, sizeof(_buf) - 16,            \
			"ERROR on file %s, func %s, line %d: ",                  \
			__FILE__, GK_Func, __LINE__);                            \
		snprintf(_buf + _idx, sizeof(_buf) - _idx, ##__VA_ARGS__); \
		fputs(_buf, stderr);                                       \
		GK_Trap;                                                   \
	} while (0)

#define log_assert(expr)     \
	do {                       \
		if (!(expr))             \
			log_error(#expr "\n"); \
	} while (0)

/*
IMPORTANT: always use the same order of defines
1. HAVE_PTHREADS_PF
	- POSIX Threads
2. HAVE_WIN32_THREAD
	- Windows CRT Thread (SRWLOCK + CONDITION_VARIABLE)
	- Available on Windows Vista and later
*/
#ifndef HAVE_PARALLEL_FRAMEWORK
#	if defined _MSC_VER //|| defined __MINGW32__
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

#elif defined HAVE_WIN32_THREAD

#	define WIN32_LEAN_AND_MEAN
#	define NOCOMM
#	undef NOMINMAX // mingw-w64 redefined
#	define NOMINMAX
#	include <Windows.h>
#	include <process.h>

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
#elif defined _MSC_VER && (defined _M_ARM || defined _M_ARM64)
		__nop();
#else
		(void)(delay);
#	warning "can't detect `pause' (CPU-yield) instruction on the target platform, " \
	"specify YIELD_PAUSE definition via compiler flags"
#endif
		// clang-format on
	}
}
}

namespace gk
{
// https://github.com/microsoft/STL/blob/main/stl/inc/memory#L1125C1-L1200C3
class
#ifdef _MSC_VER
	__declspec(novtable)
#endif
		RefCount
{
	RefCount(RefCount const&) = delete;
	RefCount(RefCount&&) = delete;
	RefCount& operator=(RefCount const&) = delete;
	RefCount& operator=(RefCount&&) = delete;

public:
	int refcount;

	RefCount()
		: refcount(1) { }

	virtual ~RefCount()
	{
		log_assert(refcount == 0);
	}
};

// https://github.com/microsoft/STL/blob/main/stl/inc/memory#L2088C1-L2124C3
template <typename T>
class RefPtr final
{
	static_assert(std::is_base_of<RefCount, T>::value, "");

	template <typename U>
	friend class RefPtr;

	typename std::remove_cv<T>::type* obj;

	RefPtr(T* ptr, RefCount* ref) noexcept
		: obj(ptr)
	{
		if (obj)
		{
			log_assert(ref);
			atomic_fetch_add(&(ref->refcount), 1);
		}
	}

public:
#define GK_RefPtr_Sptr(X) \
	typename std::enable_if<std::is_convertible<X*, T*>::value, int>::type = 0

	~RefPtr() noexcept
	{
		if (obj && atomic_fetch_add(&(obj->refcount), -1) == 1)
			delete obj;
	}

	RefPtr() noexcept
		: obj(nullptr) { }

	RefPtr(std::nullptr_t) noexcept
		: obj(nullptr) { }

	RefPtr(T* ptr) noexcept
		: obj(ptr)
	{
		if (obj)
			log_assert(atomic_load(&(obj->refcount)) == 1);
	}

	template <typename U, GK_RefPtr_Sptr(U)>
	RefPtr(U* ptr) noexcept
		: RefPtr(static_cast<T*>(ptr)) { }

	RefPtr(RefPtr const& other) noexcept
		: RefPtr(other.obj, other.obj) { }

	RefPtr(RefPtr&& other)
		: obj(other.obj)
	{
		other.obj = nullptr;
	}

	template <typename U, GK_RefPtr_Sptr(U)>
	RefPtr(RefPtr<U> const& other) noexcept
		: RefPtr(other.obj, other.obj) { }

	template <typename U, GK_RefPtr_Sptr(U)>
	RefPtr(RefPtr<U>&& other) noexcept
		: obj(other.obj)
	{
		other.obj = nullptr;
	}

	void swap(RefPtr& other) noexcept
	{
		T* tmp = obj;
		obj = other.obj;
		other.obj = tmp;
	}

	void reset() noexcept
	{
		RefPtr().swap(*this);
	}

	template <typename U, GK_RefPtr_Sptr(U)>
	void reset(U* ptr) noexcept
	{
		RefPtr(ptr).swap(*this);
	}

	RefPtr& operator=(std::nullptr_t) noexcept
	{
		RefPtr(nullptr).swap(*this);
		return *this;
	}

	RefPtr& operator=(RefPtr const& right) noexcept
	{
		if (obj != right.obj)
			RefPtr(right).swap(*this);
		return *this;
	}

	RefPtr& operator=(RefPtr&& right) noexcept
	{
		RefPtr(static_cast<RefPtr&&>(right)).swap(*this);
		return *this;
	}

	template <typename U, GK_RefPtr_Sptr(U)>
	RefPtr& operator=(RefPtr<U> const& right) noexcept
	{
		RefPtr(right).swap(*this);
		return *this;
	}

	template <typename U, GK_RefPtr_Sptr(U)>
	RefPtr& operator=(RefPtr<U>&& other) noexcept
	{
		RefPtr(static_cast<RefPtr<U>&&>(other)).swap(*this);
		return *this;
	}

	T* get() const noexcept
	{
		return obj;
	}

	T* operator->() const noexcept
	{
		return obj;
	}

	T& operator*() const noexcept
	{
		return *obj;
	}

	explicit operator bool() const noexcept
	{
		return !!obj;
	}

	// T is derived from RefCount
	// no need to static_cast / dynamic_cast
	// can not reinterpret_cast
#if 0
	template <typename U>
	RefPtr<U> reinterpret_to()
	{
		return RefPtr<U>(reinterpret_cast<U*>(obj), obj);
	}

	template <typename U>
	RefPtr<U> static_to()
	{
		return RefPtr<U>(static_cast<U*>(obj), obj);
	}

	template <typename U>
	RefPtr<U> const_to()
	{
		return RefPtr<U>(const_cast<U*>(obj), obj);
	}
#endif

	template <typename U>
	RefPtr<U> dynamic_to()
	{
		return RefPtr<U>(dynamic_cast<U*>(obj), obj);
	}
};

#define GK_RefPtr_DefineOp(op)                       \
	template <typename T, typename U>                  \
	bool operator op(                                  \
		RefPtr<T> const& a, RefPtr<U> const& b) noexcept \
	{                                                  \
		return a.get() op b.get();                       \
	}

// clang-format off
GK_RefPtr_DefineOp(==)
GK_RefPtr_DefineOp(!=)
GK_RefPtr_DefineOp(<)
GK_RefPtr_DefineOp(<=)
GK_RefPtr_DefineOp(>)
GK_RefPtr_DefineOp(>=)
// clang-format on

#undef GK_RefPtr_DefineOp
#undef GK_RefPtr_Sptr

}
