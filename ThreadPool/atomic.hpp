#pragma once

#include <cstdint>
#include <type_traits>
#include "type.hpp"

#define GK_Atomic_Char_Short 0

#if defined _WIN32 // _MSC_VER

// https://docs.microsoft.com/en-us/cpp/intrinsics/intrinsics-available-on-all-architectures?view=vs-2019
#	include <intrin.h>

namespace gk
{
#	define GK_Atomic_Bin_Op(type, vctype, gnuop, vcop)                      \
		inline type atomic_##gnuop(type* ptr, type val)                        \
		{                                                                      \
			static_assert(sizeof(type) == sizeof(vctype), "wrong size");         \
			return static_cast<type>(vcop(reinterpret_cast<vctype*>(ptr), val)); \
		}

#	define GK_Atomic_CAS_Op(type, vctype, gnuop, vcop)                    \
		inline type atomic_##gnuop(type* ptr, type comparand, type exchange) \
		{                                                                    \
			return static_cast<type>(                                          \
				vcop(reinterpret_cast<vctype*>(ptr), exchange, comparand));      \
		}

#	if GK_Atomic_Char_Short
GK_Atomic_Bin_Op(signed char, char, fetch_add, _InterlockedExchangeAdd8);
GK_Atomic_Bin_Op(signed char, char, fetch_and, _InterlockedAnd8);
GK_Atomic_Bin_Op(signed char, char, fetch_xor, _InterlockedXor8);
GK_Atomic_Bin_Op(signed char, char, fetch_or, _InterlockedOr8);
GK_Atomic_Bin_Op(signed char, char, exchange, _InterlockedExchange8);
GK_Atomic_CAS_Op(unsigned char, char, compare_exchange, _InterlockedCompareExchange8);
GK_Atomic_Bin_Op(unsigned char, char, fetch_add, _InterlockedExchangeAdd8);
GK_Atomic_Bin_Op(unsigned char, char, fetch_and, _InterlockedAnd8);
GK_Atomic_Bin_Op(unsigned char, char, fetch_xor, _InterlockedXor8);
GK_Atomic_Bin_Op(unsigned char, char, fetch_or, _InterlockedOr8);
GK_Atomic_Bin_Op(unsigned char, char, exchange, _InterlockedExchange8);
GK_Atomic_CAS_Op(unsigned char, char, compare_exchange, _InterlockedCompareExchange8);

GK_Atomic_Bin_Op(short, short, fetch_add, _InterlockedExchangeAdd16);
GK_Atomic_Bin_Op(short, short, fetch_and, _InterlockedAnd16);
GK_Atomic_Bin_Op(short, short, fetch_xor, _InterlockedXor16);
GK_Atomic_Bin_Op(short, short, fetch_or, _InterlockedOr16);
GK_Atomic_Bin_Op(short, short, exchange, _InterlockedExchange16);
GK_Atomic_CAS_Op(short, short, compare_exchange, _InterlockedCompareExchange16);
GK_Atomic_Bin_Op(unsigned short, short, fetch_add, _InterlockedExchangeAdd16);
GK_Atomic_Bin_Op(unsigned short, short, fetch_and, _InterlockedAnd16);
GK_Atomic_Bin_Op(unsigned short, short, fetch_xor, _InterlockedXor16);
GK_Atomic_Bin_Op(unsigned short, short, fetch_or, _InterlockedOr16);
GK_Atomic_Bin_Op(unsigned short, short, exchange, _InterlockedExchange16);
GK_Atomic_CAS_Op(unsigned short, short, compare_exchange, _InterlockedCompareExchange16);
#	endif

GK_Atomic_Bin_Op(int, long, fetch_add, _InterlockedExchangeAdd);
GK_Atomic_Bin_Op(int, long, fetch_and, _InterlockedAnd);
GK_Atomic_Bin_Op(int, long, fetch_xor, _InterlockedXor);
GK_Atomic_Bin_Op(int, long, fetch_or, _InterlockedOr);
GK_Atomic_Bin_Op(int, long, exchange, _InterlockedExchange);
GK_Atomic_CAS_Op(int, long, compare_exchange, _InterlockedCompareExchange);
GK_Atomic_Bin_Op(unsigned int, long, fetch_add, _InterlockedExchangeAdd);
GK_Atomic_Bin_Op(unsigned int, long, fetch_and, _InterlockedAnd);
GK_Atomic_Bin_Op(unsigned int, long, fetch_xor, _InterlockedXor);
GK_Atomic_Bin_Op(unsigned int, long, fetch_or, _InterlockedOr);
GK_Atomic_Bin_Op(unsigned int, long, exchange, _InterlockedExchange);
GK_Atomic_CAS_Op(unsigned int, long, compare_exchange, _InterlockedCompareExchange);

#	if GK_M_X64
GK_Atomic_Bin_Op(int64_t, long long, fetch_add, _InterlockedExchangeAdd64);
GK_Atomic_Bin_Op(int64_t, long long, fetch_and, _InterlockedAnd64);
GK_Atomic_Bin_Op(int64_t, long long, fetch_xor, _InterlockedXor64);
GK_Atomic_Bin_Op(int64_t, long long, fetch_or, _InterlockedOr64);
GK_Atomic_Bin_Op(int64_t, long long, exchange, _InterlockedExchange64);
GK_Atomic_CAS_Op(int64_t, long long, compare_exchange, _InterlockedCompareExchange64);
GK_Atomic_Bin_Op(uint64_t, long long, fetch_add, _InterlockedExchangeAdd64);
GK_Atomic_Bin_Op(uint64_t, long long, fetch_and, _InterlockedAnd64);
GK_Atomic_Bin_Op(uint64_t, long long, fetch_xor, _InterlockedXor64);
GK_Atomic_Bin_Op(uint64_t, long long, fetch_or, _InterlockedOr64);
GK_Atomic_Bin_Op(uint64_t, long long, exchange, _InterlockedExchange64);
GK_Atomic_CAS_Op(uint64_t, long long, compare_exchange, _InterlockedCompareExchange64);
#	endif

#	undef GK_Atomic_CAS_Op
#	undef GK_Atomic_Bin_Op
}

#elif defined __GNUC__ && defined __ATOMIC_ACQ_REL1

// gcc >= 4.7
// https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

namespace gk
{
#	define GK_Atomic_Bin_Op(type, gnuop)                    \
		inline type atomic_##gnuop(type* ptr, type val)        \
		{                                                      \
			return __atomic_##gnuop(ptr, val, __ATOMIC_ACQ_REL); \
		}

#	define GK_Atomic_XCH_Op(type, gnuop)                         \
		inline type atomic_##gnuop(type* ptr, type exchange)        \
		{                                                           \
			(void)(ptr); /* -Wunused-but-set-parameter */             \
			type ret;                                                 \
			__atomic_##gnuop(ptr, &exchange, &ret, __ATOMIC_ACQ_REL); \
			return ret;                                               \
		}

#	define GK_Atomic_CAS_Op(type, gnuop)                                  \
		inline type atomic_##gnuop(type* ptr, type comparand, type exchange) \
		{                                                                    \
			(void)(ptr); /* -Wunused-but-set-parameter */                      \
			__atomic_##gnuop(ptr, &comparand, &exchange, false,                \
				__ATOMIC_ACQ_REL, __ATOMIC_ACQ_REL);                             \
			return comparand;                                                  \
		}

#	if GK_Atomic_Char_Short
GK_Atomic_Bin_Op(signed char, fetch_add);
GK_Atomic_Bin_Op(signed char, fetch_and);
GK_Atomic_Bin_Op(signed char, fetch_xor);
GK_Atomic_Bin_Op(signed char, fetch_or);
GK_Atomic_XCH_Op(signed char, exchange);
GK_Atomic_CAS_Op(signed char, compare_exchange);
GK_Atomic_Bin_Op(unsigned char, fetch_add);
GK_Atomic_Bin_Op(unsigned char, fetch_and);
GK_Atomic_Bin_Op(unsigned char, fetch_xor);
GK_Atomic_Bin_Op(unsigned char, fetch_or);
GK_Atomic_XCH_Op(unsigned char, exchange);
GK_Atomic_CAS_Op(unsigned char, compare_exchange);

GK_Atomic_Bin_Op(short, fetch_add);
GK_Atomic_Bin_Op(short, fetch_and);
GK_Atomic_Bin_Op(short, fetch_xor);
GK_Atomic_Bin_Op(short, fetch_or);
GK_Atomic_XCH_Op(short, exchange);
GK_Atomic_CAS_Op(short, compare_exchange);
GK_Atomic_Bin_Op(unsigned short, fetch_add);
GK_Atomic_Bin_Op(unsigned short, fetch_and);
GK_Atomic_Bin_Op(unsigned short, fetch_xor);
GK_Atomic_Bin_Op(unsigned short, fetch_or);
GK_Atomic_XCH_Op(unsigned short, exchange);
GK_Atomic_CAS_Op(unsigned short, compare_exchange);
#	endif

GK_Atomic_Bin_Op(int, fetch_add);
GK_Atomic_Bin_Op(int, fetch_and);
GK_Atomic_Bin_Op(int, fetch_xor);
GK_Atomic_Bin_Op(int, fetch_or);
GK_Atomic_XCH_Op(int, exchange);
GK_Atomic_CAS_Op(int, compare_exchange);
GK_Atomic_Bin_Op(unsigned int, fetch_add);
GK_Atomic_Bin_Op(unsigned int, fetch_and);
GK_Atomic_Bin_Op(unsigned int, fetch_xor);
GK_Atomic_Bin_Op(unsigned int, fetch_or);
GK_Atomic_XCH_Op(unsigned int, exchange);
GK_Atomic_CAS_Op(unsigned int, compare_exchange);

#	if GK_M_X64
GK_Atomic_Bin_Op(int64_t, fetch_add);
GK_Atomic_Bin_Op(int64_t, fetch_and);
GK_Atomic_Bin_Op(int64_t, fetch_xor);
GK_Atomic_Bin_Op(int64_t, fetch_or);
GK_Atomic_XCH_Op(int64_t, exchange);
GK_Atomic_CAS_Op(int64_t, compare_exchange);
GK_Atomic_Bin_Op(uint64_t, fetch_add);
GK_Atomic_Bin_Op(uint64_t, fetch_and);
GK_Atomic_Bin_Op(uint64_t, fetch_xor);
GK_Atomic_Bin_Op(uint64_t, fetch_or);
GK_Atomic_XCH_Op(uint64_t, exchange);
GK_Atomic_CAS_Op(uint64_t, compare_exchange);
#	endif

#	undef GK_Atomic_CAS_Op
#	undef GK_Atomic_XCH_Op
#	undef GK_Atomic_Bin_Op
}

#elif defined __GNUC__

// gcc >= 4.1.2. otherwise maybe(?) raise compile error
// https://gcc.gnu.org/onlinedocs/gcc-4.1.2/gcc/Atomic-Builtins.html

namespace gk
{
#	define GK_Atomic_Bin_Op(type, gnuop, syncop)     \
		inline type atomic_##gnuop(type* ptr, type val) \
		{                                               \
			return __sync_##syncop(ptr, val);             \
		}

#	define GK_Atomic_CAS_Op(type, gnuop, syncop)                          \
		inline type atomic_##gnuop(type* ptr, type comparand, type exchange) \
		{                                                                    \
			return __sync_##syncop(ptr, comparand, exchange);                  \
		}

#	warning "no equivalent exchange operation is available"              \
"__sync_lock_test_and_set is not a full barrier, use at your own risk"

#	if GK_Atomic_Char_Short
GK_Atomic_Bin_Op(signed char, fetch_add, fetch_and_add);
GK_Atomic_Bin_Op(signed char, fetch_and, fetch_and_and);
GK_Atomic_Bin_Op(signed char, fetch_xor, fetch_and_xor);
GK_Atomic_Bin_Op(signed char, fetch_or, fetch_and_or);
GK_Atomic_Bin_Op(signed char, exchange, lock_test_and_set);
GK_Atomic_CAS_Op(signed char, compare_exchange, val_compare_and_swap);
GK_Atomic_Bin_Op(unsigned char, fetch_add, fetch_and_add);
GK_Atomic_Bin_Op(unsigned char, fetch_and, fetch_and_and);
GK_Atomic_Bin_Op(unsigned char, fetch_xor, fetch_and_xor);
GK_Atomic_Bin_Op(unsigned char, fetch_or, fetch_and_or);
GK_Atomic_Bin_Op(unsigned char, exchange, lock_test_and_set);
GK_Atomic_CAS_Op(unsigned char, compare_exchange, val_compare_and_swap);

GK_Atomic_Bin_Op(short, fetch_add, fetch_and_add);
GK_Atomic_Bin_Op(short, fetch_and, fetch_and_and);
GK_Atomic_Bin_Op(short, fetch_xor, fetch_and_xor);
GK_Atomic_Bin_Op(short, fetch_or, fetch_and_or);
GK_Atomic_Bin_Op(short, exchange, lock_test_and_set);
GK_Atomic_CAS_Op(short, compare_exchange, val_compare_and_swap);
GK_Atomic_Bin_Op(unsigned short, fetch_add, fetch_and_add);
GK_Atomic_Bin_Op(unsigned short, fetch_and, fetch_and_and);
GK_Atomic_Bin_Op(unsigned short, fetch_xor, fetch_and_xor);
GK_Atomic_Bin_Op(unsigned short, fetch_or, fetch_and_or);
GK_Atomic_Bin_Op(unsigned short, exchange, lock_test_and_set);
GK_Atomic_CAS_Op(unsigned short, compare_exchange, val_compare_and_swap);
#	endif

GK_Atomic_Bin_Op(int, fetch_add, fetch_and_add);
GK_Atomic_Bin_Op(int, fetch_and, fetch_and_and);
GK_Atomic_Bin_Op(int, fetch_xor, fetch_and_xor);
GK_Atomic_Bin_Op(int, fetch_or, fetch_and_or);
GK_Atomic_Bin_Op(int, exchange, lock_test_and_set);
GK_Atomic_CAS_Op(int, compare_exchange, val_compare_and_swap);
GK_Atomic_Bin_Op(unsigned int, fetch_add, fetch_and_add);
GK_Atomic_Bin_Op(unsigned int, fetch_and, fetch_and_and);
GK_Atomic_Bin_Op(unsigned int, fetch_xor, fetch_and_xor);
GK_Atomic_Bin_Op(unsigned int, fetch_or, fetch_and_or);
GK_Atomic_Bin_Op(unsigned int, exchange, lock_test_and_set);
GK_Atomic_CAS_Op(unsigned int, compare_exchange, val_compare_and_swap);

#	if GK_M_X64
GK_Atomic_Bin_Op(int64_t, fetch_add, fetch_and_add);
GK_Atomic_Bin_Op(int64_t, fetch_and, fetch_and_and);
GK_Atomic_Bin_Op(int64_t, fetch_xor, fetch_and_xor);
GK_Atomic_Bin_Op(int64_t, fetch_or, fetch_and_or);
GK_Atomic_Bin_Op(int64_t, exchange, lock_test_and_set);
GK_Atomic_CAS_Op(int64_t, compare_exchange, val_compare_and_swap);
GK_Atomic_Bin_Op(uint64_t, fetch_add, fetch_and_add);
GK_Atomic_Bin_Op(uint64_t, fetch_and, fetch_and_and);
GK_Atomic_Bin_Op(uint64_t, fetch_xor, fetch_and_xor);
GK_Atomic_Bin_Op(uint64_t, fetch_or, fetch_and_or);
GK_Atomic_Bin_Op(uint64_t, exchange, lock_test_and_set);
GK_Atomic_CAS_Op(uint64_t, compare_exchange, val_compare_and_swap);
#	endif

#	undef GK_Atomic_CAS_Op
#	undef GK_Atomic_Bin_Op
}

#endif

namespace gk
{
// for convenience
#define GK_Atomic_Load(type)         \
	inline type atomic_load(type* ptr) \
	{                                  \
		return atomic_fetch_or(ptr, 0);  \
	}

#if GK_Atomic_Char_Short
GK_Atomic_Load(signed char);
GK_Atomic_Load(unsigned char);
GK_Atomic_Load(short);
GK_Atomic_Load(unsigned short);
#endif

GK_Atomic_Load(int);
GK_Atomic_Load(unsigned int);

#if GK_M_X64
GK_Atomic_Load(int64_t);
GK_Atomic_Load(uint64_t);
#endif

#undef GK_Atomic_Load
}

namespace gk
{
/** use RefCount with T, only alloc once
 * replace stack allocate with new on heap and T*
 * replace operator= with addref
 * replace delete or de-constructor with subref
 */
template <class T>
class RefCount
{
	RefCount(RefCount&&) = delete;
	RefCount(RefCount const&) = delete;
	RefCount& operator=(RefCount&&) = delete;
	RefCount& operator=(RefCount const&) = delete;

protected:
	int refcount;

	virtual ~RefCount()
	{
		log_assert(atomic_load(&refcount) == 0);
	};

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
		if (atomic_fetch_add(&refcount, -1) == 1)
			delete this;
	}
};
}
