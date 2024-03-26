
#include <cstdlib>
#include <cstdio>

#if defined __GNUC__ || defined __clang__
#	define GK_FUNC __PRETTY_FUNCTION__
#	define GK_TRAP __builtin_trap()
#	define GK_UNREACHABLE __builtin_unreachable()
#	define GK_GNU_INLINE extern __inline__ __attribute__((__gnu_inline__))
#	define GK_ALWAYS_INLINE GK_GNU_INLINE __attribute__((__always_inline__, __artificial__))
#	define GK_NEVER_INLINE __attribute__((__noinline__))
#	define GK_NEVER_RETURN __attribute__((__noreturn__))
#	define GK_ALIGNED(x) __attribute__((__aligned__(x)))
#elif defined _MSC_VER
#	include <intrin.h>
#	define GK_FUNC __FUNCSIG__
#	define GK_TRAP __debugbreak()
#	define GK_UNREACHABLE assume(0)
#	define GK_GNU_INLINE __inline
#	define GK_ALWAYS_INLINE __forceinline
#	define GK_NEVER_INLINE __declspec(noinline)
#	define GK_NEVER_RETURN __declspec(noreturn)
#	define GK_ALIGNED(x) __declspec(align(x))
#else
#	error "not support"
#endif

#if defined _M_X64 || defined _M_ARM64     \
	|| defined __x86_64__ || defined __amd64 \
	|| defined __ARM64 || defined __aarch64__
#	define GK_IS_64BIT 1
#else
#	define GK_IS_64BIT 0
#endif

#define GK_CONCAT_AUX(x, y) x##y
#define GK_CONCAT(x, y) GK_CONCAT_AUX(x, y)

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
			__FILE__, GK_FUNC, __LINE__);                            \
		snprintf(_buf + _idx, sizeof(_buf) - _idx, ##__VA_ARGS__); \
		fputs(_buf, stderr);                                       \
		fflush(stderr);                                            \
		GK_TRAP;                                                   \
	} while (0)

#define log_assert(expr)                   \
	do {                                     \
		if (!(expr)) log_error("%s\n", #expr); \
	} while (0)

namespace gk {
GK_ALWAYS_INLINE void yield_pause(int delay)
{
	// clang-format off
	while (delay--)
	{
#ifdef YIELD_PAUSE
		YIELD_PAUSE;
#elif defined __GNUC__ && (defined __i386__ || defined __x86_64__)
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
#	warning "can't detect `pause' (CPU-yield) instruction on the target platform, " \
	"specify YIELD_PAUSE definition via compiler flags"
#endif
		// clang-format on
	}
}
}
