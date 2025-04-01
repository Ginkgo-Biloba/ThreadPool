#pragma once
#include <climits>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

#if defined __GNUC__ || defined __clang__
#	define GK_FUNC          __PRETTY_FUNCTION__
#	define GK_TRAP          __builtin_trap()
#	define GK_UNREACHABLE   __builtin_unreachable()
#	define GK_INLINE        extern __inline__ __attribute__((__artificial__))
#	define GK_ALWAYS_INLINE extern __inline__ __attribute__((__artificial__, __always_inline__))
#	define GK_NO_INLINE     __attribute__((__noinline__))
#	define GK_NO_RETURN     __attribute__((__noreturn__))
#	define GK_ALIGNED(x)    __attribute__((__aligned__(x)))
#	define GK_ALIGNOF(x)    __alignof__(x)
#elif defined _MSC_VER
#	include <intrin.h>
#	define GK_FUNC          __FUNCSIG__
#	define GK_TRAP          __debugbreak()
#	define GK_UNREACHABLE   assume(0)
#	define GK_INLINE        __inline
#	define GK_ALWAYS_INLINE __forceinline
#	define GK_NO_INLINE     __declspec(noinline)
#	define GK_NO_RETURN     __declspec(noreturn)
#	define GK_ALIGNED(x)    __declspec(align(x))
#	define GK_ALIGNOF(x)    __alignof(x)
#else
#	error "not support"
#endif

#if defined _M_X64 \
	|| defined __x86_64__ || defined __amd64__ || defined __aarch64__
#	define GK_IS_64BIT 1
#else
#	define GK_IS_64BIT 0
#endif

#if defined _M_IX86 || (defined _M_AMD64 && !defined _M_ARM64) \
	|| defined __i386__ || defined __x86_64__ || defined __amd64__
#	define GK_IS_X86 1
#else
#	define GK_IS_X86 0
#endif

#if defined _M_ARM || defined _M_ARM64 || defined _M_ARM64EC \
	|| defined __arm__ || defined __aarch64__
#	define GK_IS_ARM 1
#else
#	define GK_IS_ARM 0
#endif

#define GK_CONCAT_AUX(x, y) x##y
#define GK_CONCAT(x, y)     GK_CONCAT_AUX(x, y)

#if defined _WIN32
#	define NOCOMM
#	undef NOMINMAX // mingw-w64 redefined
#	define NOMINMAX
#	include <Windows.h>
#	include <process.h>

#	define NT_SUCCESS(x) ((NTSTATUS)(x) >= 0)

EXTERN_C NTSYSAPI NTSTATUS NTAPI NtCreateKeyedEvent(
	OUT PHANDLE KeyedEventHandle, IN ACCESS_MASK DesiredAccess,
	IN PVOID /*POBJECT_ATTRIBUTES*/ ObjectAttributes, IN ULONG Reserved);

EXTERN_C NTSYSAPI NTSTATUS NTAPI NtWaitForKeyedEvent(
	IN HANDLE KeyedEventHandle, IN PVOID Key,
	IN BOOLEAN Alertable, IN PLARGE_INTEGER Timeout OPTIONAL);

EXTERN_C NTSYSAPI NTSTATUS NTAPI NtReleaseKeyedEvent(
	IN HANDLE KeyedEventHandle, IN PVOID Key,
	IN BOOLEAN Alertable, IN PLARGE_INTEGER Timeout OPTIONAL);

#elif defined __linux__
#	include <pthread.h>
#	include <unistd.h>
#	include <syscall.h>
#	include <linux/futex.h>

GK_ALWAYS_INLINE void Sleep(uint32_t dwMilliseconds)
{
	timespec ts;
	ts.tv_sec = dwMilliseconds / 1000;
	ts.tv_nsec = (dwMilliseconds - ts.tv_sec * 1000) * 1000000;
	nanosleep(&ts, NULL);
}
#endif

namespace gk {

#if defined _WIN32

HANDLE GlobalKeyedEventHandle();

#elif defined __linux__

GK_ALWAYS_INLINE long sysfutex(
	uint32_t* uaddr, int futex_op, uint32_t val, const struct timespec* timeout,
	uint32_t* uaddr2 /* or: uint32_t val2 */, uint32_t val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}

#endif

// The smaller of a and b. If equivalent, returns a
template <typename T>
GK_INLINE constexpr T const& min(T const& a, T const& b)
{
	return b < a ? b : a;
}

// The greater of a and b. If equivalent, returns a.
template <typename T>
GK_INLINE constexpr T const& max(T const& a, T const& b)
{
	return a < b ? b : a;
}

// lo if v < lo, hi if hi < v, otherwise v
template <typename T>
GK_INLINE constexpr T const& clamp(T const& v, T const& lo, T const& hi)
{
	return v < lo ? lo : (hi < v ? hi : v);
}

//////////////////// from OpenCV ////////////////////

GK_ALWAYS_INLINE void yield(uint32_t delay)
{
	// clang-format off
	while (delay--)
	{
#if defined __GNUC__ && (defined __i386__ || defined __x86_64__)
#	if !defined(__SSE2__)
		__asm__ __volatile__("rep; nop");
#	else
		__builtin_ia32_pause();
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
		__yield();
#else
#	warning "can't detect `pause' (CPU-yield) instruction on the target platform"
#endif
	}
	// clang-format on
}

GK_INLINE int64_t getTickCount()
{
#if defined _WIN32 || defined WINCE
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);
	return static_cast<int64_t>(counter.QuadPart);
#elif defined __linux__
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return static_cast<int64_t>(tp.tv_nsec)
		+ static_cast<int64_t>(tp.tv_sec) * 1000000000;
#else
	return 0;
#endif
}

GK_INLINE double getTickFrequency(void)
{
#if defined _WIN32 || defined WINCE
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	return static_cast<double>(freq.QuadPart);
#elif defined __linux__
	return 1e9;
#else
	return 1;
#endif
}

GK_NO_RETURN void logError(char const* file, char const* func, int line,
#if defined _MSC_VER
	_Printf_format_string_ char const* fmt, ...);
#elif defined __MINGW32__
	char const* fmt, ...) __attribute__((format(__MINGW_PRINTF_FORMAT, 4, 5)));
#elif defined __GNUC__
	char const* fmt, ...) __attribute__((format(printf, 4, 5)));
#endif

}

#define GK_LOG_INFO(...)                       \
	do {                                         \
		char _buf[1024];                           \
		snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
		fputs(_buf, stdout);                       \
	} while (0)

#define GK_LOG_ERROR(...) \
	gk::logError(__FILE__, GK_FUNC, __LINE__, __VA_ARGS__)

#define GK_ASSERT(expr)                       \
	do {                                        \
		if (!(expr)) GK_LOG_ERROR("%s\n", #expr); \
	} while (0)
