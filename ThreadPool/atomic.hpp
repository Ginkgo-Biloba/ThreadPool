#pragma once

#include "fwd.hpp"

#include <type_traits>
#include <cstdint>

#define GK_ATOMIC_ENABLE_IF(sz) \
	typename std::enable_if<std::is_integral<T>::value && sizeof(T) == sz, int>::type = 0

#if defined _MSC_VER || defined __MINGW32__

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
		}                                                         \
		GK_ALWAYS_INLINE void atomic_##func##_##sz(               \
			void volatile* __restrict ptr,                          \
			void const* __restrict val, void* __restrict res)       \
		{                                                         \
			*reinterpret_cast<itype*>(res) = msfunc(                \
				reinterpret_cast<itype volatile*>(ptr),               \
				*reinterpret_cast<itype const*>(val));                \
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
		}                                                             \
		GK_ALWAYS_INLINE void atomic_store_##sz(                      \
			void volatile* __restrict ptr, void const* __restrict val)  \
		{                                                             \
			opXch(reinterpret_cast<itype volatile*>(ptr),               \
				*reinterpret_cast<itype const*>(val));                    \
		}                                                             \
		GK_ALWAYS_INLINE bool atomic_compare_exchange_##sz(           \
			void volatile* __restrict ptr,                              \
			void* __restrict cmp, void const* __restrict val)           \
		{                                                             \
			itype icmp = *reinterpret_cast<itype*>(cmp);                \
			itype iold = opCas(                                         \
				reinterpret_cast<itype volatile*>(ptr),                   \
				*reinterpret_cast<itype const*>(val),                     \
				*reinterpret_cast<itype const*>(cmp));                    \
			*reinterpret_cast<itype*>(cmp) = iold;                      \
			return iold == icmp;                                        \
		}                                                             \
		GK_ALWAYS_INLINE void atomic_load_##sz(                       \
			void volatile* __restrict ptr, void* __restrict res)        \
		{                                                             \
			*reinterpret_cast<itype*>(res) = opCas(                     \
				reinterpret_cast<itype volatile*>(ptr), 0, 0);            \
		}

#	if !defined(__MINGW32__)
GK_ATOMIC_DEFINE(1, char, _InterlockedExchangeAdd8,
	_InterlockedAnd8, _InterlockedOr8, _InterlockedXor8,
	_InterlockedExchange8, _InterlockedCompareExchange8)

GK_ATOMIC_DEFINE(2, short, _InterlockedExchangeAdd16,
	_InterlockedAnd16, _InterlockedOr16, _InterlockedXor16,
	_InterlockedExchange16, _InterlockedCompareExchange16)
#	endif

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
		}                                                             \
		GK_ALWAYS_INLINE bool atomic_##func##_##sz(                   \
			void volatile* __restrict ptr, itype bit)                   \
		{                                                             \
			return msfunc(reinterpret_cast<itype volatile*>(ptr), bit); \
		}

#	ifdef _MSC_VER
GK_ATOMIC_BTS(4, bts, long, _interlockedbittestandset)
GK_ATOMIC_BTS(4, btr, long, _interlockedbittestandreset)
GK_ATOMIC_BTS(8, bts, __int64, _interlockedbittestandset64)
GK_ATOMIC_BTS(8, btr, __int64, _interlockedbittestandreset64)
#	else
GK_ATOMIC_BTS(4, bts, long, InterlockedBitTestAndSet)
GK_ATOMIC_BTS(4, btr, long, InterlockedBitTestAndReset)
GK_ATOMIC_BTS(8, bts, __int64, InterlockedBitTestAndSet64)
GK_ATOMIC_BTS(8, btr, __int64, InterlockedBitTestAndReset64)
#	endif

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
		}                                                            \
		GK_ALWAYS_INLINE void atomic_##func##_##sz(                  \
			void volatile* __restrict ptr,                             \
			void const* __restrict val, void* __restrict res)          \
		{                                                            \
			*reinterpret_cast<itype*>(res) = __atomic_##gnu(           \
				reinterpret_cast<itype volatile*>(ptr),                  \
				*reinterpret_cast<itype const*>(val), __ATOMIC_ACQ_REL); \
		}

#	define GK_ATOMIC_DEFINE(sz, itype)                               \
		GK_ATOMIC_SIZE(sz, itype, fetch_add, fetch_add)                 \
		GK_ATOMIC_SIZE(sz, itype, fetch_and, fetch_and)                 \
		GK_ATOMIC_SIZE(sz, itype, fetch_or, fetch_or)                   \
		GK_ATOMIC_SIZE(sz, itype, fetch_xor, fetch_xor)                 \
		GK_ATOMIC_SIZE(sz, itype, exchange, exchange_n)                 \
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
		}                                                               \
		GK_ALWAYS_INLINE void atomic_store_##sz(                        \
			void volatile* __restrict ptr, void const* __restrict val)    \
		{                                                               \
			__atomic_store(reinterpret_cast<itype volatile*>(ptr),        \
				reinterpret_cast<itype const*>(val), __ATOMIC_RELEASE);     \
		}                                                               \
		GK_ALWAYS_INLINE bool atomic_compare_exchange_##sz(             \
			void volatile* __restrict ptr,                                \
			void* __restrict cmp, void const* __restrict val)             \
		{                                                               \
			return __atomic_compare_exchange(                             \
				reinterpret_cast<itype volatile*>(ptr),                     \
				reinterpret_cast<itype*>(cmp),                              \
				reinterpret_cast<itype const*>(val),                        \
				false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);                 \
		}                                                               \
		GK_ALWAYS_INLINE void atomic_load_##sz(                         \
			void volatile* __restrict ptr, void* __restrict res)          \
		{                                                               \
			__atomic_load(reinterpret_cast<itype volatile*>(ptr),         \
				reinterpret_cast<itype*>(res), __ATOMIC_ACQUIRE);           \
		}

GK_ATOMIC_DEFINE(1, uint8_t)
GK_ATOMIC_DEFINE(2, uint16_t)
GK_ATOMIC_DEFINE(4, uint32_t)
GK_ATOMIC_DEFINE(8, uint64_t)

template <typename T,
	typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
GK_ALWAYS_INLINE T atomic_load(T volatile* ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

#	define GK_ATOMIC_BTS(sz, itype, rev, func, orand)                         \
		template <typename T, GK_ATOMIC_ENABLE_IF(sz)>                           \
		GK_ALWAYS_INLINE bool atomic_##func(                                     \
			T volatile* __restrict ptr, itype bit)                                 \
		{                                                                        \
			itype mask = static_cast<itype>(1) << bit;                             \
			itype old = __atomic_fetch_##orand(ptr, rev mask, __ATOMIC_ACQ_REL);   \
			return old & mask;                                                     \
		}                                                                        \
		GK_ALWAYS_INLINE                                                         \
		bool atomic_##func##_##sz(void volatile* __restrict ptr, itype bit)      \
		{                                                                        \
			itype mask = static_cast<itype>(1) << bit;                             \
			itype old = __atomic_fetch_##orand(                                    \
				reinterpret_cast<itype volatile*>(ptr), rev mask, __ATOMIC_ACQ_REL); \
			return old & mask;                                                     \
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

namespace gk {

#if GK_IS_64BIT
#	define GK_ATOMIC_SIZE 8
#else
#	define GK_ATOMIC_SIZE 4
#endif

#define GK_ATOMIC_DEFINE(func)                                    \
	GK_ALWAYS_INLINE void atomic_##func##_ptr(                      \
		void volatile* __restrict ptr,                                \
		void const* __restrict val, void* __restrict res)             \
	{                                                               \
		(GK_CONCAT(atomic_##func##_, GK_ATOMIC_SIZE)(ptr, val, res)); \
	}

GK_ATOMIC_DEFINE(fetch_add)
GK_ATOMIC_DEFINE(fetch_and)
GK_ATOMIC_DEFINE(fetch_or)
GK_ATOMIC_DEFINE(fetch_xor)
GK_ATOMIC_DEFINE(exchange)

GK_ALWAYS_INLINE void atomic_store_ptr(
	void volatile* __restrict ptr, void const* __restrict val)
{
	(GK_CONCAT(atomic_store_, GK_ATOMIC_SIZE)(ptr, val));
}
GK_ALWAYS_INLINE bool atomic_compare_exchange_ptr(
	void volatile* ptr, void* __restrict cmp, void const* val)
{
	return GK_CONCAT(atomic_compare_exchange_, GK_ATOMIC_SIZE)(ptr, cmp, val);
}
GK_ALWAYS_INLINE
void atomic_load_ptr(void volatile* __restrict ptr, void* __restrict res)
{
	(GK_CONCAT(atomic_load_, GK_ATOMIC_SIZE)(ptr, res));
}
GK_ALWAYS_INLINE
bool atomic_bts_ptr(void volatile* __restrict ptr, uintptr_t bit)
{
	return atomic_bts(
		reinterpret_cast<uintptr_t volatile*>(ptr), bit);
}
GK_ALWAYS_INLINE
bool atomic_btr_ptr(void volatile* __restrict ptr, uintptr_t bit)
{
	return atomic_btr(
		reinterpret_cast<uintptr_t volatile*>(ptr), bit);
}

#undef GK_ATOMIC_DEFINE
#undef GK_ATOMIC_SIZE
}
