# 动机 

工作原因需要在 Android 上面写 C++，鉴于：

1. NDK Clang 的 `std::thread` 是个残废，并且官方[也不打算修复了](https://github.com/android/ndk/issues/789)。以及里面的 OpenMP 也[有点问题](https://github.com/android/ndk/issues/1028)。
2. 从 OpenCV 里面抄的 pthreads 线程池[有内存泄漏问题](https://github.com/opencv/opencv/issues/6203)。如果简单地把 `TYPE*` 改为 `unique_ptr<TYPE>` 则程序退出时线程会挂在那里 `join`不掉（个人遭遇）。

无奈，只能写这个了。

# 原则

1. 能正常使用。正常使用的时候，尽量最大化效率。
2. 异常安全去死，不要时间啊。50 毫秒延时甲方都能唠叨不停。
3. 对于不符合参数要求的调用，或者出各种问题的话，给出消息后，直接 `assert` 挂掉，省心。
4. 测试？封装？接口规范？代码复制过去能用就行了，要啥自行车。
5. 只在 Windows 上用 VC++ 和  GCC 测试，默认 NDK Clang 能用，出问题再说。Windows 上我不用 Clang ，也许会有其他人帮忙编译运行一下？
6. 一个线程池同时只运行一个并行任务，嵌套的当做单线程运行。毕竟图像处理算法的流程都是固定的。

# 引用库

通过宏定义选择实现。目前有三个，基本思路抄自 OpenCV 的 [`parallel.cpp`](https://github.com/opencv/opencv/blob/master/modules/core/src/parallel.cpp)。

可以编译时定义宏`HAVE_PARALLEL_FRAMEWORK`为下面的值（默认为 2）：

0. 没有多线程，所有调用都是单线程运行。
1. OpenMP。虽然 libc++ 的支持是个残废，但是其他的库还是能用的。据说 OpenMP 里面是个线程池。此时定义宏 `HAVE_OPENMP`。
2. Parallel Pattern Library。Windows 还是让人很省心的，据说 `Concurrency::Scheduler` 里面也是一个线程池。此时定义宏 `HAVE_CONCURRENCY`。运行的话需要 `concrtxxx.dll`，xxx 对应 VC++ 版本号。可惜[不支持 UWP 应用](https://docs.microsoft.com/en-us/cpp/parallel/concrt/task-scheduler-concurrency-runtime)。
3. POSIX Threads。最糟心的实现。此时定义宏 `HAVE_PTHREADS_PF`。

# 唉——

写到一半发现，其实只有 phreads 的实现才能叫做线程池。因为 OpenMP 和 PPL 的修改是全局的。

真需要的话，OpenMP 无解，PPL 需要按 pthreads 那样改成 WIN32 线程。算了，先不管了。