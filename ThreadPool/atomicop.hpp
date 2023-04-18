#pragma once

#if defined _M_X64 || defined __x86_64__ || defined __aarch64__ || defined _M_ARM64
#  define GK_M_X64 1
#else 
#  define GK_M_X64 0
#endif

#if defined __i386__ || defined __x86_64__ || defined _M_IX86 || defined _M_X64
#  include <emmintrin.h> // for _mm_pause
#endif

#include <cstdint>

#define GK_Atomic_Char_Short 0

#if defined _WIN32 // _MSC_VER

// https://docs.microsoft.com/en-us/cpp/intrinsics/intrinsics-available-on-all-architectures?view=vs-2019
#include <intrin.h>

namespace gk
{
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

#if GK_Atomic_Char_Short
GK_Atomic_Bin_Op(int8_t, char, fetch_add, _InterlockedExchangeAdd8)
GK_Atomic_Bin_Op(int8_t, char, fetch_and, _InterlockedAnd8)
GK_Atomic_Bin_Op(int8_t, char, fetch_xor, _InterlockedXor8)
GK_Atomic_Bin_Op(int8_t, char, fetch_or, _InterlockedOr8)
GK_Atomic_Bin_Op(int8_t, char, exchange, _InterlockedExchange8)
GK_Atomic_CAS_Op(int8_t, char, compare_exchange, _InterlockedCompareExchange8);

GK_Atomic_Bin_Op(int16_t, short, fetch_add, _InterlockedExchangeAdd16)
GK_Atomic_Bin_Op(int16_t, short, fetch_and, _InterlockedAnd16)
GK_Atomic_Bin_Op(int16_t, short, fetch_xor, _InterlockedXor16)
GK_Atomic_Bin_Op(int16_t, short, fetch_or, _InterlockedOr16)
GK_Atomic_Bin_Op(int16_t, short, exchange, _InterlockedExchange16)
GK_Atomic_CAS_Op(int16_t, short, compare_exchange, _InterlockedCompareExchange16);
#endif

GK_Atomic_Bin_Op(int32_t, long, fetch_add, _InterlockedExchangeAdd)
GK_Atomic_Bin_Op(int32_t, long, fetch_and, _InterlockedAnd)
GK_Atomic_Bin_Op(int32_t, long, fetch_xor, _InterlockedXor)
GK_Atomic_Bin_Op(int32_t, long, fetch_or, _InterlockedOr)
GK_Atomic_Bin_Op(int32_t, long, exchange, _InterlockedExchange)
GK_Atomic_CAS_Op(int32_t, long, compare_exchange, _InterlockedCompareExchange)

#if GK_M_X64
GK_Atomic_Bin_Op(int64_t, long long, fetch_add, _InterlockedExchangeAdd64)
GK_Atomic_Bin_Op(int64_t, long long, fetch_and, _InterlockedAnd64)
GK_Atomic_Bin_Op(int64_t, long long, fetch_xor, _InterlockedXor64)
GK_Atomic_Bin_Op(int64_t, long long, fetch_or, _InterlockedOr64)
GK_Atomic_Bin_Op(int64_t, long long, exchange, _InterlockedExchange64)
GK_Atomic_CAS_Op(int64_t, long long, compare_exchange, _InterlockedCompareExchange64)
#endif

#undef GK_Atomic_CAS_Op
#undef GK_Atomic_Bin_Op
}

#elif defined __GNUC__ && defined __ATOMIC_ACQ_REL

// gcc >= 4.7
// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

namespace gk
{
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

#if GK_Atomic_Char_Short
GK_Atomic_Bin_Op(int8_t, fetch_add)
GK_Atomic_Bin_Op(int8_t, fetch_and)
GK_Atomic_Bin_Op(int8_t, fetch_xor)
GK_Atomic_Bin_Op(int8_t, fetch_or)
GK_Atomic_XCH_Op(int8_t, exchange)
GK_Atomic_CAS_Op(int8_t, compare_exchange)

GK_Atomic_Bin_Op(int16_t, fetch_add)
GK_Atomic_Bin_Op(int16_t, fetch_and)
GK_Atomic_Bin_Op(int16_t, fetch_xor)
GK_Atomic_Bin_Op(int16_t, fetch_or)
GK_Atomic_XCH_Op(int16_t, exchange)
GK_Atomic_CAS_Op(int16_t, compare_exchange)
#endif

GK_Atomic_Bin_Op(int32_t, fetch_add)
GK_Atomic_Bin_Op(int32_t, fetch_and)
GK_Atomic_Bin_Op(int32_t, fetch_xor)
GK_Atomic_Bin_Op(int32_t, fetch_or)
GK_Atomic_XCH_Op(int32_t, exchange)
GK_Atomic_CAS_Op(int32_t, compare_exchange)

#if GK_M_X64
GK_Atomic_Bin_Op(int64_t, fetch_add)
GK_Atomic_Bin_Op(int64_t, fetch_and)
GK_Atomic_Bin_Op(int64_t, fetch_xor)
GK_Atomic_Bin_Op(int64_t, fetch_or)
GK_Atomic_XCH_Op(int64_t, exchange)
GK_Atomic_CAS_Op(int64_t, compare_exchange)
#endif

#undef GK_Atomic_CAS_Op
#undef GK_Atomic_XCH_Op
#undef GK_Atomic_Bin_Op
}

#elif defined __GNUC__

// gcc >= 4.1.2. otherwise maybe(?) raise compile error
// https://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Atomic-Builtins.html

namespace gk
{
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

#warning "no equivalent exchange operation is available"              \
"__sync_lock_test_and_set is not a full barrier, use at your own risk"

#if GK_Atomic_Char_Short
GK_Atomic_Bin_Op(int8_t, fetch_add, fetch_and_add)
GK_Atomic_Bin_Op(int8_t, fetch_and, fetch_and_and)
GK_Atomic_Bin_Op(int8_t, fetch_xor, fetch_and_xor)
GK_Atomic_Bin_Op(int8_t, fetch_or, fetch_and_or)
GK_Atomic_Bin_Op(int8_t, exchange, lock_test_and_set)
GK_Atomic_CAS_Op(int8_t, compare_exchange, val_compare_and_swap)

GK_Atomic_Bin_Op(int16_t, fetch_add, fetch_and_add)
GK_Atomic_Bin_Op(int16_t, fetch_and, fetch_and_and)
GK_Atomic_Bin_Op(int16_t, fetch_xor, fetch_and_xor)
GK_Atomic_Bin_Op(int16_t, fetch_or, fetch_and_or)
GK_Atomic_Bin_Op(int16_t, exchange, lock_test_and_set)
GK_Atomic_CAS_Op(int16_t, compare_exchange, val_compare_and_swap)
#endif

GK_Atomic_Bin_Op(int32_t, fetch_add, fetch_and_add)
GK_Atomic_Bin_Op(int32_t, fetch_and, fetch_and_and)
GK_Atomic_Bin_Op(int32_t, fetch_xor, fetch_and_xor)
GK_Atomic_Bin_Op(int32_t, fetch_or, fetch_and_or)
GK_Atomic_Bin_Op(int32_t, exchange, lock_test_and_set)
GK_Atomic_CAS_Op(int32_t, compare_exchange, val_compare_and_swap)

#if GK_M_X64
GK_Atomic_Bin_Op(int64_t, fetch_add, fetch_and_add)
GK_Atomic_Bin_Op(int64_t, fetch_and, fetch_and_and)
GK_Atomic_Bin_Op(int64_t, fetch_xor, fetch_and_xor)
GK_Atomic_Bin_Op(int64_t, fetch_or, fetch_and_or)
GK_Atomic_Bin_Op(int64_t, exchange, lock_test_and_set)
GK_Atomic_CAS_Op(int64_t, compare_exchange, val_compare_and_swap)
#endif

#undef GK_Atomic_CAS_Op
#undef GK_Atomic_Bin_Op
}

#else 

# error no atomic operation support

#endif


namespace gk
{
#define GK_Unsiged2Sigend_Bin(op, utype, stype)                 \
inline utype atomic_##op(utype* ptr, utype val)                 \
{                                                               \
	return static_cast<utype>(atomic_##op(                        \
		reinterpret_cast<stype*>(ptr), static_cast<stype>(val)));   \
}

#define GK_Unsigned2Signed(utype, stype)                        \
GK_Unsiged2Sigend_Bin(fetch_add, utype, stype)                  \
GK_Unsiged2Sigend_Bin(fetch_and, utype, stype)                  \
GK_Unsiged2Sigend_Bin(fetch_xor, utype, stype)                  \
GK_Unsiged2Sigend_Bin(fetch_or, utype, stype)                   \
GK_Unsiged2Sigend_Bin(exchange, utype, stype)                   \
inline utype atomic_compare_exchange(utype* ptr,                \
	utype comparand, utype exchange)                              \
{                                                               \
	return static_cast<utype>(                                    \
		atomic_compare_exchange(                                    \
			reinterpret_cast<stype*>(ptr),                            \
			static_cast<stype>(comparand),                            \
			static_cast<stype>(exchange)                              \
		));                                                         \
}

#if GK_Atomic_Char_Short
GK_Unsigned2Signed(uint8_t, int8_t)
GK_Unsigned2Signed(uint16_t, int16_t)
#endif

GK_Unsigned2Signed(uint32_t, int32_t)

#if GK_M_X64
GK_Unsigned2Signed(uint64_t, int64_t)
#endif
}


#undef GK_Unsigned2Signed
#undef GK_Unsiged2Sigend_Bin


namespace gk
{
// for convenience
#define GK_Atomic_Load(type)          \
inline type atomic_load(type* ptr)    \
{                                     \
	return atomic_fetch_or(ptr, 0);     \
}

#if GK_Atomic_Char_Short
GK_Atomic_Load(int8_t)
GK_Atomic_Load(uint8_t)
GK_Atomic_Load(int16_t)
GK_Atomic_Load(uint16_t)
#endif

GK_Atomic_Load(int32_t)
GK_Atomic_Load(uint32_t)

#if GK_M_X64
GK_Atomic_Load(int64_t)
GK_Atomic_Load(uint64_t)
#endif

#undef GK_Atomic_Load


// Spin lock's CPU-level yield (required for Hyper-Threading)
inline void yield_pause(int delay)
{
	for (; delay > 0; --delay)
	{
#ifdef YIELD_PAUSE
		YIELD_PAUSE;
#elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#  if !defined(__SSE2__)
		__asm__ __volatile__("rep; nop");
#  else
		_mm_pause();
#  endif
#elif defined __GNUC__ && defined __aarch64__
		__asm__ __volatile__("yield" ::: "memory");
#elif defined __GNUC__ && defined __arm__
		__asm__ __volatile__("" ::: "memory");
# elif defined __GNUC__ && defined __mips__ && __mips_isa_rev >= 2
		__asm__ __volatile__("pause" ::: "memory");
#elif defined __GNUC__ && defined __PPC64__
		__asm__ __volatile__("or 27,27,27" ::: "memory");
#elif defined _MSC_VER && (defined _M_IX86 || defined _M_X64)
		_mm_pause();
#elif defined _MSC_VER && (defined _M_ARM || defined M_ARM64)
		__nop();
#else
		(void)(delay);
		#warning "can't detect `pause' (CPU-yield) instruction on the target platform, \
		specify YIELD_PAUSE definition via compiler flags"
# endif
}
}

}
