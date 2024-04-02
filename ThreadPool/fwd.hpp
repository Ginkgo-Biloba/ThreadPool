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
#	define GK_INLINE        extern __inline__ __attribute__((__gnu_inline__))
#	define GK_ALWAYS_INLINE extern __inline__ __attribute__((__always_inline__, __artificial__))
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
#	define WIN32_LEAN_AND_MEAN
#	define NOCOMM
#	undef NOMINMAX // mingw-w64 redefined
#	define NOMINMAX
#	include <Windows.h>
#	include <process.h>
#elif defined __linux__
#	include <pthread.h>
#	include <sys/sysinfo.h>
#	include <unistd.h>
#	include <syscall.h>
#	include <linux/futex.h>

GK_ALWAYS_INLINE long sysfutex(
	uint32_t* uaddr, int futex_op, uint32_t val, const struct timespec* timeout,
	uint32_t* uaddr2 /* or: uint32_t val2 */, uint32_t val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3);
}
#endif

namespace gk {

// a 与 b 的较大者。若等价则返回 a
template <typename T>
GK_ALWAYS_INLINE constexpr T const& min(T const& a, T const& b)
{
	return b < a ? b : a;
}

// a 与 b 的较小者。若等价则返回 a
template <typename T>
GK_ALWAYS_INLINE constexpr T const& max(T const& a, T const& b)
{
	return a < b ? b : a;
}

// 若 v 小于 lo 则为 lo，若 hi 小于 v 则为 hi，否则为 v
template <typename T>
GK_ALWAYS_INLINE constexpr T const& clamp(T const& v, T const& lo, T const& hi)
{
	return v < lo ? lo : (hi < v ? hi : v);
}

//////////////////// from wine / reactos ////////////////////

#if defined __linux__

#	define WINE_LOCK 1

// https://learn.microsoft.com/zh-cn/windows/win32/sync/one-time-initialization
typedef union _RTL_RUN_ONCE {
	// 使结构体大小和 Windows 上的相同
	uintptr_t padding;
	uint32_t value[1];
	struct {
		uint32_t ready   : 8; // 是否完成初始化
		uint32_t pending : 1; // 是否有线程正在初始化
		uint32_t waiting : 1; // 是否有线程在等待
		uintptr_t nsleep : sizeof(uintptr_t) * 8 - 1;
	} bs;
} INIT_ONCE, *PINIT_ONCE, *LPINIT_ONCE;

// https://learn.microsoft.com/zh-cn/windows/win32/sync/slim-reader-writer--srw--locks
typedef union _RTL_SRWLOCK {
	uintptr_t padding;
	uint32_t value[1];
	struct {
		uint32_t rd_hold : 15; // 获取到读锁的线程数
		uint32_t rd_wait : 1;  // 是否包含读等待
		uint32_t ex_wait : 15; // 等待写锁的线程数
		uint32_t ex_lock : 1;  // 是否写锁定
	} bs;                    // bit filed
	struct {
		uint32_t locked   : 1; // 是否锁定
		uint32_t spining  : 1; // 是否有线程在自旋等待
		uint32_t waiting  : 1; // 是否包含等待链表
		uint32_t multiple : 1; // 是否是获取了多个读锁
		// 获得读锁的线程数，或者指向链表头节点的指针
		uintptr_t rd_hold : sizeof(uintptr_t) * 8 - 4;
	} wl; // waiting list
} SRWLOCK, *PSRWLOCK;

// https://learn.microsoft.com/zh-cn/windows/win32/sync/condition-variables
typedef union _RTL_CONDITION_VARIABLE {
	uintptr_t padding;
	uint32_t value[1];
} CONDITION_VARIABLE, *PCONDITION_VARIABLE;

// clang-format off
#	define INIT_ONCE_STATIC_INIT   { 0 }
#	define SRWLOCK_INIT            { 0 }
#	define CONDITION_VARIABLE_INIT { 0 }
#	define INFINITE 0xffffffff
#	define CONDITION_VARIABLE_LOCKMODE_SHARED 1
// clang-format on

/* 如果返回非 0 值，当前线程应该完成初始化
 * 如果返回 0，表示初始化已完成
 * 不支持 dwFlags fPending lpContext 三个参数，只是为了接口一致
 */
int InitOnceBeginInitialize(
	LPINIT_ONCE lpInitOnce, uint32_t dwFlags, int* fPending, void** lpContext);
int InitOnceComplete(LPINIT_ONCE lpInitOnce, uint32_t dwFlags, void* lpContext);

/* 如果成功获取锁，则返回值为非零值
 * 如果当前线程无法获取锁，则返回值为零
 */
int TryAcquireSRWLockExclusive(PSRWLOCK SRWLock);
/* 如果成功获取锁，则返回值为非零值
 * 如果当前线程无法获取锁，则返回值为零
 */
int TryAcquireSRWLockShared(PSRWLOCK SRWLock);
void AcquireSRWLockExclusive(PSRWLOCK SRWLock);
void AcquireSRWLockShared(PSRWLOCK SRWLock);
void ReleaseSRWLockExclusive(PSRWLOCK SRWLock);
void ReleaseSRWLockShared(PSRWLOCK SRWLock);

/* 如果该函数成功，则返回值为非零值
 * 如果超时过期，函数将返回 0
 * 
 * dwMilliseconds
 * - 如果 dwMilliseconds 为零，该函数将测试指定对象的状态并立即返回
 * - 如果 dwMilliseconds 为 INFINITE，则函数的超时间隔永远不会过期
 * 
 * Flags
 * - 如果为 CONDITION_VARIABLE_LOCKMODE_SHARED，则 SRW 锁处于共享模式
 * - 否则，锁处于独占模式
 */
int SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable,
	PSRWLOCK SRWLock, uint32_t dwMilliseconds, uint32_t Flags);
void WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable);
void WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable);

GK_ALWAYS_INLINE void Sleep(uint32_t dwMilliseconds)
{
	timespec ts;
	ts.tv_sec = dwMilliseconds / 1000;
	ts.tv_nsec = (dwMilliseconds - ts.tv_sec * 1000) * 1000000;
	nanosleep(&ts, NULL);
}

#elif defined _WIN32

using ::INIT_ONCE;
using ::InitOnceBeginInitialize;
using ::InitOnceComplete;

using ::SRWLOCK;
using ::TryAcquireSRWLockExclusive;
using ::TryAcquireSRWLockShared;
using ::AcquireSRWLockExclusive;
using ::AcquireSRWLockShared;
using ::ReleaseSRWLockExclusive;
using ::ReleaseSRWLockShared;

using ::CONDITION_VARIABLE;
using ::SleepConditionVariableSRW;
using ::WakeAllConditionVariable;
using ::WakeConditionVariable;

using ::Sleep;

#endif

//////////////////// from opencv ////////////////////

GK_ALWAYS_INLINE void yield(int delay)
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
		(void)(delay);
#	warning "can't detect `pause' (CPU-yield) instruction on the target platform"
#endif
		// clang-format on
	}
}

/* 返回的是逻辑核数量，带超线程 */
GK_ALWAYS_INLINE int getNumberOfCPU()
{
#if defined _WIN32
	SYSTEM_INFO sysinfo;
	GetNativeSystemInfo(&sysinfo);
	return sysinfo.dwNumberOfProcessors;
#elif defined __linux__
	return get_nprocs();
#else
	return 1;
#endif
}

GK_ALWAYS_INLINE int64_t getTickCount()
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

GK_ALWAYS_INLINE double getTickFrequency(void)
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

#define GK_LOG_INFO(...)                         \
	do {                                           \
		char _buf[1024];                             \
		snprintf(_buf, sizeof(_buf), ##__VA_ARGS__); \
		fputs(_buf, stdout);                         \
	} while (0)

#define GK_LOG_ERROR(...) \
	gk::logError(__FILE__, GK_FUNC, __LINE__, ##__VA_ARGS__)

#define GK_ASSERT(expr)                       \
	do {                                        \
		if (!(expr)) GK_LOG_ERROR("%s\n", #expr); \
	} while (0)
