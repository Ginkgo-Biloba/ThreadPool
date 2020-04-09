#pragma once

#include <cstdarg>
#include <cstdio>

#if defined __ANDROID__
# include <android/log.h>
#endif

// printf and fprintf is not thread safe in GCC
void log_printf(bool stop, char const* fmt, ...)
{
	char buf[1 << 10];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	buf[sizeof(buf) - 1] = 0;
	if (stop)
	{
#	if defined __ANDROID__
		__android_log_print(ANDROID_LOG_ERROR, "gk::log_error", buf);
#	endif
		fflush(stdout); fflush(stderr);
		fputs(buf, stderr);
		fflush(stderr);
		char volatile* ptr = reinterpret_cast<char*>(0x2b);
		*ptr = 0x2b; // #11 SIGSEGV
	}
	else
	{
		fputs(buf, stdout);
#	if defined __ANDROID__
		__android_log_print(ANDROID_LOG_INFO, "gk::log_printf", buf);
#	endif
	}
}


#define log_info(...) // log_printf(false, __VA_ARGS__)

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

