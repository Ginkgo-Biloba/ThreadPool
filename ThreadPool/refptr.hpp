#pragma once

#include "atomic.hpp"

/*
IMPORTANT: always use the same order of defines
1. HAVE_PTHREADS_PF
	- POSIX Threads
2. HAVE_WIN32_THREAD
	- Windows CRT Thread (SRWLOCK + CONDITION_VARIABLE)
	- Available on Windows Vista and later
*/
#ifndef HAVE_PARALLEL_FRAMEWORK
#	if defined _MSC_VER || defined __MINGW32__
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

namespace gk {
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

namespace gk {
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

namespace gk {

struct RefObj {
	int refcount;

	RefObj() { refcount = 1; }
	virtual ~RefObj() { log_assert(refcount == 0); }

	int addref(int x) { return atomic_fetch_add(&refcount, x); }

	RefObj(RefObj const&) = delete;
	RefObj(RefObj&&) = delete;
	RefObj& operator=(RefObj const&) = delete;
	RefObj& operator=(RefObj&&) = delete;
};

#define GK_REFPTR_ENABLE_IF(X, Y) \
	typename std::enable_if<std::is_convertible<X*, Y*>::value, int>::type = 0

template <typename T>
class RefPtr final {
	static_assert(std::is_base_of<RefObj, T>::value, "");

	T* obj;

	void destroy()
	{
		if (obj && obj->addref(-1) == 1)
			delete obj;
		obj = nullptr;
	}

public:
	void swap(RefPtr& other) noexcept
	{
		T* tmp = obj;
		obj = other.obj;
		other.obj = tmp;
	}

	~RefPtr() { destroy(); }

	RefPtr() { obj = nullptr; }

	RefPtr(std::nullptr_t) { obj = nullptr; }

	RefPtr(T* ptr)
	{
		obj = ptr;
		if (obj) log_assert(obj->refcount == 1);
	}

	RefPtr(RefPtr const& other)
	{
		obj = other.obj;
		if (obj) obj->addref(1);
	}

	RefPtr(RefPtr&& other)
	{
		obj = other.obj;
		other.obj = nullptr;
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr(U* ptr)
	{
		obj = static_cast<T*>(ptr);
		if (obj) log_assert(obj->refcount == 1);
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr(RefPtr<U> const& other)
	{
		obj = static_cast<T*>(other.obj);
		if (obj) obj->addref(1);
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr(RefPtr<U>&& other)
	{
		obj = static_cast<T*>(other.obj);
		other.obj = nullptr;
	}

	RefPtr& operator=(std::nullptr_t) { destroy(); }

	RefPtr& operator=(T* ptr) { RefPtr(ptr).swap(*this); }

	RefPtr& operator=(RefPtr const& other)
	{
		if (obj != other.obj) RefPtr(other).swap(*this);
		return *this;
	}

	RefPtr& operator=(RefPtr&& other)
	{
		RefPtr(static_cast<RefPtr&&>(other)).swap(*this);
		return *this;
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr& operator=(U* ptr) { RefPtr(ptr).swap(*this); }

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr& operator=(RefPtr<U> const& other)
	{
		if (obj != static_cast<T*>(other)) RefPtr(other).swap(*this);
		return *this;
	}

	template <typename U, GK_REFPTR_ENABLE_IF(U, T)>
	RefPtr& operator=(RefPtr<U>&& other)
	{
		RefPtr(static_cast<RefPtr<U>&&>(other)).swap(*this);
		return *this;
	}

	T* get() const noexcept { return obj; }
	T* operator->() const noexcept { return obj; }
	explicit operator bool() const noexcept { return !!obj; }

	template <typename U>
	RefPtr<U> staticTo() noexcept
	{
		RefPtr<U> u;
		u.obj = static_cast<U*>(obj);
		if (obj) obj->addref(1);
		return u;
	}

	template <typename U>
	RefPtr<U> dynamicTo() noexcept
	{
		RefPtr<U> u;
		u.obj = dynamic_cast<U*>(obj);
		if (obj) obj->addref(1);
		return u;
	}
};

#define GK_REFPTR_OP(op)                                            \
	template <typename T, typename U>                                 \
	bool operator op(RefPtr<T> const& a, RefPtr<U> const& b) noexcept \
	{                                                                 \
		return a.get() op b.get();                                      \
	}

GK_REFPTR_OP(==)
GK_REFPTR_OP(!=)
GK_REFPTR_OP(<)
GK_REFPTR_OP(<=)
GK_REFPTR_OP(>)
GK_REFPTR_OP(>=)

#undef GK_REFPTR_OP
#undef GK_REFPTR_ENABLE_IF

}
