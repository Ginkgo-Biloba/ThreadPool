#pragma once

#include "fwd.hpp"

#include <type_traits>
#include <cstdint>

#define GK_ATOMIC_ENABLE_IF(sz) \
	typename std::enable_if<std::is_integral<T>::value && sizeof(T) == sz, int>::type = sz

#if defined _MSC_VER

// https://docs.microsoft.com/en-us/cpp/intrinsics/intrinsics-available-on-all-architectures?view=vs-2019
#	include <intrin.h>

namespace gk {

#	define GK_ATOMIC_SIZE(sz, itype, func, msfunc)             \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>            \
		GK_ALWAYS_INLINE T atomic_##func(                         \
			T volatile* __restrict ptr, itype val)                  \
		{                                                         \
			return static_cast<T>(                                  \
				msfunc(reinterpret_cast<itype volatile*>(ptr), val)); \
		}

#	define GK_ATOMIC_DEFINE(                                       \
		sz, itype, opAdd, opAnd, opOr, opXor, opXch, opCas)           \
		GK_ATOMIC_SIZE(sz, itype, fetch_add, opAdd)                   \
		GK_ATOMIC_SIZE(sz, itype, fetch_and, opAnd)                   \
		GK_ATOMIC_SIZE(sz, itype, fetch_or, opOr)                     \
		GK_ATOMIC_SIZE(sz, itype, fetch_xor, opXor)                   \
		GK_ATOMIC_SIZE(sz, itype, exchange, opXch)                    \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                \
		GK_ALWAYS_INLINE void atomic_store(                           \
			T volatile* __restrict ptr, itype val)                      \
		{                                                             \
			opXch(reinterpret_cast<itype volatile*>(ptr), val);         \
		}                                                             \
		/*template <typename T, GK_ATOMIC_ENABLE_IF(sz)>              \
		GK_ALWAYS_INLINE T atomic_compare_exchange(                   \
		  T volatile* __restrict ptr, itype cmp, itype val)           \
		{                                                             \
		  return static_cast<T>(                                      \
		    opCas(reinterpret_cast<itype volatile*>(ptr), val, cmp)); \
		}*/                                                           \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                \
		GK_ALWAYS_INLINE bool atomic_compare_exchange(                \
			T volatile* __restrict ptr, T* __restrict cmp, itype val)   \
		{                                                             \
			itype cur = *cmp;                                           \
			itype old = *cmp = opCas(                                   \
				reinterpret_cast<itype volatile*>(ptr), val, cur);        \
			return old == cur;                                          \
		}                                                             \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                \
		GK_ALWAYS_INLINE T atomic_load(T volatile* __restrict ptr)    \
		{                                                             \
			return static_cast<T>(                                      \
				opCas(reinterpret_cast<itype volatile*>(ptr), 0, 0));     \
		}

GK_ATOMIC_DEFINE(1, char, _InterlockedExchangeAdd8,
	_InterlockedAnd8, _InterlockedOr8, _InterlockedXor8,
	_InterlockedExchange8, _InterlockedCompareExchange8)

GK_ATOMIC_DEFINE(2, short, _InterlockedExchangeAdd16,
	_InterlockedAnd16, _InterlockedOr16, _InterlockedXor16,
	_InterlockedExchange16, _InterlockedCompareExchange16)

GK_ATOMIC_DEFINE(4, long, _InterlockedExchangeAdd,
	_InterlockedAnd, _InterlockedOr, _InterlockedXor,
	_InterlockedExchange, _InterlockedCompareExchange)

#	if defined _M_X64
GK_ATOMIC_DEFINE(8, __int64, _InterlockedExchangeAdd64,
	_InterlockedAnd64, _InterlockedOr64, _InterlockedXor64,
	_InterlockedExchange64, _InterlockedCompareExchange64)
#	endif

#	define GK_ATOMIC_BTS(sz, func, itype, msfunc)                  \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                \
		GK_ALWAYS_INLINE bool atomic_##func(                          \
			T volatile* __restrict ptr, itype bit)                      \
		{                                                             \
			return msfunc(reinterpret_cast<itype volatile*>(ptr), bit); \
		}

GK_ATOMIC_BTS(4, bts, long, _interlockedbittestandset)
GK_ATOMIC_BTS(4, btr, long, _interlockedbittestandreset)
GK_ATOMIC_BTS(8, bts, __int64, _interlockedbittestandset64)
GK_ATOMIC_BTS(8, btr, __int64, _interlockedbittestandreset64)

#	undef GK_ATOMIC_BTS
#	undef GK_ATOMIC_DEFINE
#	undef GK_ATOMIC_SIZE
}

#elif defined __GNUC__

// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

namespace gk {

#	define GK_ATOMIC_SIZE(sz, itype, func, gnu)                   \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>               \
		GK_ALWAYS_INLINE T atomic_##func(T volatile* ptr, itype val) \
		{                                                            \
			return __atomic_##gnu(ptr, val, __ATOMIC_ACQ_REL);         \
		}

#	define GK_ATOMIC_DEFINE(sz, itype)                               \
		GK_ATOMIC_SIZE(sz, itype, fetch_add, fetch_add)                 \
		GK_ATOMIC_SIZE(sz, itype, fetch_and, fetch_and)                 \
		GK_ATOMIC_SIZE(sz, itype, fetch_or, fetch_or)                   \
		GK_ATOMIC_SIZE(sz, itype, fetch_xor, fetch_xor)                 \
		GK_ATOMIC_SIZE(sz, itype, exchange, exchange_n)                 \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                  \
		GK_ALWAYS_INLINE T atomic_load(T volatile* ptr)                 \
		{                                                               \
			return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);                \
		}                                                               \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                  \
		GK_ALWAYS_INLINE void atomic_store(                             \
			T volatile* __restrict ptr, itype val)                        \
		{                                                               \
			__atomic_store_n(ptr, val, __ATOMIC_RELEASE);                 \
		}                                                               \
		/*template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                \
		GK_ALWAYS_INLINE T atomic_compare_exchange(                     \
		  T volatile* __restrict ptr, itype cmp, itype val)             \
		{                                                               \
		  __atomic_compare_exchange_n(                                  \
		    ptr, &cmp, val, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE); \
		  return cmp;                                                   \
		}*/                                                             \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                  \
		GK_ALWAYS_INLINE bool atomic_compare_exchange(                  \
			T volatile* __restrict ptr, T* __restrict cmp, itype val)     \
		{                                                               \
			bool res = __atomic_compare_exchange_n(                       \
				ptr, cmp, val, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);  \
			return res;                                                   \
		}

GK_ATOMIC_DEFINE(1, uint8_t)
GK_ATOMIC_DEFINE(2, uint16_t)
GK_ATOMIC_DEFINE(4, uint32_t)
GK_ATOMIC_DEFINE(8, uint64_t)

#	define GK_ATOMIC_BTS(sz, itype, rev, func, orand)                       \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                         \
		GK_ALWAYS_INLINE bool atomic_##func(                                   \
			T volatile* __restrict ptr, itype bit)                               \
		{                                                                      \
			itype mask = static_cast<itype>(1) << bit;                           \
			itype old = __atomic_fetch_##orand(ptr, rev mask, __ATOMIC_ACQ_REL); \
			return old & mask;                                                   \
		}

GK_ATOMIC_BTS(4, uint32_t, , bts, or)
GK_ATOMIC_BTS(8, uint64_t, , bts, or)
GK_ATOMIC_BTS(4, uint32_t, ~, btr, and)
GK_ATOMIC_BTS(8, uint64_t, ~, btr, and)

#	undef GK_ATOMIC_BTS
#	undef GK_ATOMIC_DEFINE
#	undef GK_ATOMIC_SIZE
}

#endif

#undef GK_ATOMIC_ENABLE_IF
