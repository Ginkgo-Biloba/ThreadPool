#pragma once

#include <cstdarg>
#include <cstdio>

#if defined __i386__ || defined __x86_64__ || defined _M_IX86 || defined _M_X64
#  include <emmintrin.h> // for _mm_pause
#endif


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
		asm volatile("yield" ::: "memory");
#elif defined __GNUC__ && defined __arm__
		asm volatile("" ::: "memory");
# elif defined __GNUC__ && defined __mips__ && __mips_isa_rev >= 2
		asm volatile("pause" ::: "memory");
#elif defined __GNUC__ && defined __PPC64__
		asm volatile("or 27,27,27" ::: "memory");
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


// printf and fprintf is not thread safe in GCC
void log_printf(bool segsev, char const* fmt, ...)
{
	char buf[1 << 10];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;
	if (segsev)
	{
		fputs(buf, stderr);
		fflush(stderr);
#	if defined __ANDROID__
		__android_log_print(ANDROID_LOG_ERROR, "gk::log_error", buf);
#	endif
#if _HAS_EXCEPTIONS
		throw buf;
#else
		char volatile* p = reinterpret_cast<char*>(0x2b);
		*p = 0x2b; // 引发异常
#endif
	}
	else
	{
		fputs(buf, stdout);
#	if defined __ANDROID__
		__android_log_print(ANDROID_LOG_INFO, "gk::log_printf", buf);
#	endif
	}
}

#define log_info(...) log_printf(false, __VA_ARGS__)

#define log_error(...) log_printf(true, __VA_ARGS__)

#ifdef _MSC_VER
#  define GK_Func __FUNCTION__
#elif defined __GNUC__
#  define GK_Func __PRETTY_FUNCTION__
#else 
#  define GK_Func "Can_Not_Get_Func_Name"
#endif

// expr in C assert evaluated exactly only once ?
#define log_assert(expr) do \
	{ \
		if (!(expr)) \
			log_printf(true, "log_assert failed: " #expr " in %s, file %s, line %d\n", \
				GK_Func, __FILE__, __LINE__); \
	} while(0)

