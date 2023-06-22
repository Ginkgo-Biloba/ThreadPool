#pragma once

#include <cstdint>
#include <type_traits>
#include "type.hpp"


#if defined _WIN32 && defined _MSC_VER

// https://docs.microsoft.com/en-us/cpp/intrinsics/intrinsics-available-on-all-architectures?view=vs-2019
#	include <intrin.h>

namespace gk
{
#	define GK_Atomic_EnableIf(sz) \
		typename std::enable_if<std::is_integral<T>::value && sizeof(T) == sz, T>::type

#	define GK_Atomic_DefineSize(sz, mstype, func, msfunc)       \
		template <typename T>                                      \
		GK_Atomic_EnableIf(sz) atomic_##func(T* ptr, mstype val)   \
		{                                                          \
			return static_cast<T>(                                   \
				msfunc(reinterpret_cast<volatile mstype*>(ptr), val)); \
		}

#	define GK_Atomic_Define(sz, mstype,                                  \
		opAdd, opAnd, opOr, opXor, opXch, opCas)                            \
		GK_Atomic_DefineSize(sz, mstype, fetch_add, opAdd);                 \
		GK_Atomic_DefineSize(sz, mstype, fetch_and, opAnd);                 \
		GK_Atomic_DefineSize(sz, mstype, fetch_or, opOr);                   \
		GK_Atomic_DefineSize(sz, mstype, fetch_xor, opXor)                  \
			GK_Atomic_DefineSize(sz, mstype, exchange, opXch);                \
		template <typename T>                                               \
		GK_Atomic_EnableIf(sz)                                              \
			atomic_compare_exchange(T* ptr, mstype expected, mstype val)      \
		{                                                                   \
			return static_cast<T>(                                            \
				opCas(reinterpret_cast<volatile mstype*>(ptr), val, expected)); \
		}                                                                   \
		template <typename T>                                               \
		GK_Atomic_EnableIf(sz) atomic_load(T* ptr)                          \
		{                                                                   \
			return static_cast<T>(                                            \
				opOr(reinterpret_cast<volatile mstype*>(ptr), 0));              \
		}                                                                   \
		template <typename T, GK_Atomic_EnableIf(sz) = 0>                   \
		void atomic_store(T* ptr, mstype val)                               \
		{                                                                   \
			opXch(reinterpret_cast<volatile mstype*>(ptr), val);              \
		}

#	if !defined __MINGW32__
GK_Atomic_Define(1, char,
	_InterlockedExchangeAdd8,
	_InterlockedAnd8,
	_InterlockedOr8,
	_InterlockedXor8,
	_InterlockedExchange8,
	_InterlockedCompareExchange8);

GK_Atomic_Define(2, short,
	_InterlockedExchangeAdd16,
	_InterlockedAnd16,
	_InterlockedOr16,
	_InterlockedXor16,
	_InterlockedExchange16,
	_InterlockedCompareExchange16);
#	endif

GK_Atomic_Define(4, long,
	_InterlockedExchangeAdd,
	_InterlockedAnd,
	_InterlockedOr,
	_InterlockedXor,
	_InterlockedExchange,
	_InterlockedCompareExchange);

#	if GK_M_64
GK_Atomic_Define(8, __int64,
	_InterlockedExchangeAdd64,
	_InterlockedAnd64,
	_InterlockedOr64,
	_InterlockedXor64,
	_InterlockedExchange64,
	_InterlockedCompareExchange64);
#	endif

#	undef GK_Atomic_Define
#	undef GK_Atomic_DefineSize
#	undef GK_Atomic_EnableIf
}

#elif defined __GNUC__ && defined __ATOMIC_ACQ_REL

// gcc >= 4.7
// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

namespace gk
{
// if get error: expected identifier before numeric constant
// un-include C++ header <atomic>, or change the functions' name
#	define atomic_fetch_add(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_ACQ_REL)
#	define atomic_fetch_and(ptr, val) __atomic_fetch_and(ptr, val, __ATOMIC_ACQ_REL)
#	define atomic_fetch_or(ptr, val) __atomic_fetch_or(ptr, val, __ATOMIC_ACQ_REL)
#	define atomic_fetch_xor(ptr, val) __atomic_fetch_xor(ptr, val, __ATOMIC_ACQ_REL)
#	define atomic_exchange(ptr, val) __atomic_exchange_n(ptr, val, __ATOMIC_ACQ_REL)
#	define atomic_load(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
#	define atomic_store(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELEASE)

template <typename T>
T atomic_compare_exchange(T* ptr, T comparand, T val)
{
	__atomic_compare_exchange_n(ptr, &comparand, val, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQ_REL);
	return comparand;
}
}

#elif defined __GNUC__

// gcc >= 4.1.2. otherwise maybe(?) raise compile error
// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fsync-Builtins.html

namespace gk
{
#	define atomic_fetch_add(ptr, val) __sync_fetch_and_add(ptr, val)
#	define atomic_fetch_and(ptr, val) __sync_fetch_and_and(ptr, val)
#	define atomic_fetch_or(ptr, val) __sync_fetch_and_or(ptr, val)
#	define atomic_fetch_xor(ptr, val) __sync_fetch_and_xor(ptr, val)
#	define atomic_exchange(ptr, val) __sync_lock_test_and_set(ptr, val)
#	define atomic_compare_exchange(ptr, comparand, val) \
		__sync_val_compare_and_swap(ptr, comparand, val)
#	define atomic_load(ptr) __sync_fetch_and_or(ptr, 0)
#	define atomic_store(ptr, val) __sync_lock_test_and_set(ptr, val)

#	warning "no equivalent exchange operation is available"              \
"__sync_lock_test_and_set is not a full barrier, use at your own risk"
}

#endif


namespace gk
{
/** use RefCount with T, only alloc once
 * replace stack allocate with new on heap and T*
 * replace operator= with addref
 * replace delete or de-constructor with subref
 */
template <typename T>
class RefCount
{
	RefCount(RefCount&&) = delete;
	RefCount(RefCount const&) = delete;
	RefCount& operator=(RefCount&&) = delete;
	RefCount& operator=(RefCount const&) = delete;

protected:
	int refcount;

	virtual ~RefCount() {};

public:
	RefCount()
		: refcount(1) { }

	T* addref()
	{
		static_assert(
			std::is_base_of<RefCount<T>, T>::value,
			"make T derived from RefCount<T>");
		atomic_fetch_add(&refcount, 1);
		return static_cast<T*>(this);
	}

	void subref()
	{
		int ref = atomic_fetch_add(&refcount, -1);
		log_assert(ref > 0);
		if (ref == 1)
			delete this;
	}
};
}
