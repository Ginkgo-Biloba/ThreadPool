#include "ThreadPool.hpp"

#include <algorithm>
#include <vector>
using std::vector;

/*
* 0. None
* 1. OpenMP
* 2. Parallel Patterns Library
* 3. POSIX Threads
*/
#ifndef HAVE_PARALLEL_FRAMEWORK
#  define HAVE_PARALLEL_FRAMEWORK 3
#endif

#if HAVE_PARALLEL_FRAMEWORK == 1
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_OPENMP")
#  define HAVE_OPENMP
#elif HAVE_PARALLEL_FRAMEWORK == 2
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_CONCURRENCY")
#  define HAVE_CONCURRENCY
#elif HAVE_PARALLEL_FRAMEWORK == 3
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => HAVE_PTHREADS_PF")
#  define HAVE_PTHREADS_PF
#else
#  pragma message ("HAVE_PARALLEL_FRAMEWORK => 0")
#  undef HAVE_PARALLEL_FRAMEWORK
#  define HAVE_PARALLEL_FRAMEWORK 0
#endif


namespace gk
{
ThreadPool::ThreadPool()
{}


ThreadPool::~ThreadPool()
{}

}
