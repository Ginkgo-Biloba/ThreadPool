﻿#pragma once

static_assert(true
	&& sizeof(char) == 1
	&& sizeof(short) == 2
	&& sizeof(int) == 4,
	"size_required");

// prefer to use int, because I have not used/tested char/short ...

#if defined _MSC_VER

// https://docs.microsoft.com/en-us/cpp/intrinsics/intrinsics-available-on-all-architectures?view=vs-2019
#include <intrin.h>
#include <atomic>

inline void atomic_thread_fence()
{
#if defined _M_IX86 || defined _M_X64
	_mm_mfence();
#elif defined  _M_ARM || defiend _M_ARM64
	__dmb(_ARM_BARRIER_SY);
#endif
}

#define GK_Atomic_Bin_Op(type, vctype, gnuop, vcop)                      \
inline type atomic_##gnuop(type* ptr, type val)                          \
{                                                                        \
	return static_cast<type>(vcop(reinterpret_cast<vctype*>(ptr), val));   \
}

#define GK_Atomic_CAS_Op(type, vctype, gnuop, vcop)                      \
inline type atomic_##gnuop(type* ptr, type comparand, type exchange)     \
{                                                                        \
	return static_cast<type>(                                              \
		vcop(reinterpret_cast<vctype*>(ptr), exchange, comparand));          \
}

namespace gk
{
GK_Atomic_Bin_Op(char, char, fetch_add,        _InterlockedExchangeAdd8)
GK_Atomic_Bin_Op(char, char, fetch_and,        _InterlockedAnd8)
GK_Atomic_Bin_Op(char, char, fetch_xor,        _InterlockedXor8)
GK_Atomic_Bin_Op(char, char, fetch_or,         _InterlockedOr8)
GK_Atomic_Bin_Op(char, char, exchange,         _InterlockedExchange8)
GK_Atomic_CAS_Op(char, char, compare_exchange, _InterlockedCompareExchange8);

GK_Atomic_Bin_Op(short, short, fetch_add,        _InterlockedExchangeAdd16)
GK_Atomic_Bin_Op(short, short, fetch_and,        _InterlockedAnd16)
GK_Atomic_Bin_Op(short, short, fetch_xor,        _InterlockedXor16)
GK_Atomic_Bin_Op(short, short, fetch_or,         _InterlockedOr16)
GK_Atomic_Bin_Op(short, short, exchange,         _InterlockedExchange16)
GK_Atomic_CAS_Op(short, short, compare_exchange, _InterlockedCompareExchange16);

GK_Atomic_Bin_Op(int, long, fetch_add,        _InterlockedExchangeAdd)
GK_Atomic_Bin_Op(int, long, fetch_and,        _InterlockedAnd)
GK_Atomic_Bin_Op(int, long, fetch_xor,        _InterlockedXor)
GK_Atomic_Bin_Op(int, long, fetch_or,         _InterlockedOr)
GK_Atomic_Bin_Op(int, long, exchange,         _InterlockedExchange)
GK_Atomic_CAS_Op(int, long, compare_exchange, _InterlockedCompareExchange);
}

#undef GK_Atomic_CAS_Op
#undef GK_Atomic_Bin_Op

#elif defined __GNUC__ && defined __ATOMIC_ACQ_REL

// gcc >= 4.7
// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

inline void atomic_thread_fence()
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

#define GK_Atomic_Bin_Op(type, gnuop)                   \
inline type atomic_##gnuop(type* ptr, type val)         \
{                                                       \
	return __atomic_##gnuop(ptr, val, __ATOMIC_ACQ_REL);  \
}

#define GK_Atomic_XCH_Op(type, gnuop)                        \
inline type atomic_##gnuop(type* ptr, type exchange)         \
{                                                            \
	(void)(ptr); /* -Wunused-but-set-parameter */              \
	type ret;                                                  \
	__atomic_##gnuop(ptr, &exchange, &ret, __ATOMIC_ACQ_REL);  \
	return ret;                                                \
}

#define GK_Atomic_CAS_Op(type, gnuop)                                 \
inline type atomic_##gnuop(type* ptr, type comparand, type exchange)  \
{                                                                     \
	(void)(ptr); /* -Wunused-but-set-parameter */                       \
	__atomic_##gnuop(ptr, &comparand, &exchange, false,                 \
		__ATOMIC_ACQ_REL, __ATOMIC_ACQ_REL);                              \
	return comparand;                                                   \
}

GK_Atomic_Bin_Op(char, fetch_add)
GK_Atomic_Bin_Op(char, fetch_and)
GK_Atomic_Bin_Op(char, fetch_xor)
GK_Atomic_Bin_Op(char, fetch_or)
GK_Atomic_XCH_Op(char, exchange)
GK_Atomic_CAS_Op(char, compare_exchange)

GK_Atomic_Bin_Op(short, fetch_add)
GK_Atomic_Bin_Op(short, fetch_and)
GK_Atomic_Bin_Op(short, fetch_xor)
GK_Atomic_Bin_Op(short, fetch_or)
GK_Atomic_XCH_Op(short, exchange)
GK_Atomic_CAS_Op(short, compare_exchange)

GK_Atomic_Bin_Op(int, fetch_add)
GK_Atomic_Bin_Op(int, fetch_and)
GK_Atomic_Bin_Op(int, fetch_xor)
GK_Atomic_Bin_Op(int, fetch_or)
GK_Atomic_XCH_Op(int, exchange)
GK_Atomic_CAS_Op(int, compare_exchange)

#undef GK_Atomic_CAS_Op
#undef GK_Atomic_XCH_Op
#undef GK_Atomic_Bin_Op

#elif defined __GNUC__

// gcc >= 4.1.2. otherwise maybe(?) raise compile error
// https://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Atomic-Builtins.html

inline void atomic_thread_fence()
{
	__sync_synchronize();
}

#define GK_Atomic_Bin_Op(type, gnuop, syncop)        \
inline type atomic_##gnuop(type* ptr, type val)      \
{                                                    \
	return __sync_##syncop(ptr, val);                  \
}

#define GK_Atomic_CAS_Op(type, gnuop, syncop)                         \
inline type atomic_##gnuop(type* ptr, type comparand, type exchange)  \
{                                                                     \
	return __sync_##syncop(ptr, comparand, exchange);                   \
}

#warning "no equivalent exchange operation is available, \
	__sync_lock_test_and_set is not a full barrier, use at your own risk"
	
GK_Atomic_Bin_Op(char, fetch_add, fetch_and_add)
GK_Atomic_Bin_Op(char, fetch_and, fetch_and_and)
GK_Atomic_Bin_Op(char, fetch_xor, fetch_and_xor)
GK_Atomic_Bin_Op(char, fetch_or, fetch_and_or)
GK_Atomic_Bin_Op(char, exchange, lock_test_and_set)
GK_Atomic_CAS_Op(char, compare_exchange, val_compare_and_swap)

GK_Atomic_Bin_Op(short, fetch_add, fetch_and_add)
GK_Atomic_Bin_Op(short, fetch_and, fetch_and_and)
GK_Atomic_Bin_Op(short, fetch_xor, fetch_and_xor)
GK_Atomic_Bin_Op(short, fetch_or, fetch_and_or)
GK_Atomic_Bin_Op(short, exchange, lock_test_and_set)
GK_Atomic_CAS_Op(short, compare_exchange, val_compare_and_swap)

GK_Atomic_Bin_Op(int, fetch_add, fetch_and_add)
GK_Atomic_Bin_Op(int, fetch_and, fetch_and_and)
GK_Atomic_Bin_Op(int, fetch_xor, fetch_and_xor)
GK_Atomic_Bin_Op(int, fetch_or, fetch_and_or)
GK_Atomic_Bin_Op(int, exchange, lock_test_and_set)
GK_Atomic_CAS_Op(int, compare_exchange, val_compare_and_swap)

#undef GK_Atomic_CAS_Op
#undef GK_Atomic_Bin_Op
	
#else 

# error no atomic operation support

#endif
