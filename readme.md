# 动机 

工作原因需要在 Android 上面写 C++，鉴于：

1. NDK Clang 的 `std::thread` 是个残废，并且官方[也不打算修复了](https://github.com/android/ndk/issues/789)。以及里面的 OpenMP 也[有点问题](https://github.com/android/ndk/issues/1028)。
2. 从 OpenCV 里面抄的 pthreads 线程池[有内存泄漏问题](https://github.com/opencv/opencv/issues/6203)。如果简单地把 `TYPE*` 改为 `unique_ptr<TYPE>` 则程序退出时线程会挂在那里 `join` 不掉（个人遭遇）。

无奈，只能写这个了。

线程池有两种，按照[知乎网友的说法](https://www.zhihu.com/question/27908489/answer/44060803)：

> 第一种是适合把多线程当作异步使用的，比如Windows API或者C#里的那个。调用者直接扔一个functor过去就可以了，等到需要返回值的时候同步一下。
>
> 第二种是主线程需要多次spawn出很多子线程的情况。这经常需要详细控制线程个数，并且主线程会等待子线程都完成之后才继续。

因为我主要用来做图像处理或者数值计算，一般都是当前这一步运行出结果了才能下一步，所以这里的线程池属于第二种。据说 Windows Vista 之后[自带一个](https://docs.microsoft.com/en-us/windows/win32/procthread/thread-pools)第一种线程池？

# 原则

1. 能正常使用。正常使用的时候，尽量最大化效率。
2. 异常安全去死，不要时间啊。多 50 毫秒甲方都能唠叨不停。
3. 对于不符合参数要求的调用，或者出各种问题的话，给出消息后，直接 `assert` 挂掉，省心。
4. 测试？封装？接口规范？代码复制过去能用就行了，要啥自行车。
5. 只在 Windows 上用 VC++ 和  GCC 测试，姑且认为 NDK Clang 能用，出问题再说。Windows 上我不用 Clang ，也许会有其他人帮忙编译运行一下？
6. 一个线程池同时只运行一个并行任务，嵌套的当做单线程运行。

# 引用库

通过宏定义选择实现。目前只有 pthreads，基本思路抄自 OpenCV 的 [`parallel.cpp`](https://github.com/opencv/opencv/blob/master/modules/core/src/parallel.cpp)。

可以编译时定义宏`HAVE_PARALLEL_FRAMEWORK`为下面的值：

0. 没有多线程，所有调用都是单线程运行。
1. POSIX Threads。此时定义宏 `HAVE_PTHREADS_PF`。Windows 上运行需要对应的 pthreadXXX.dll。

# 唉——

反正 libc++ 里面的线程库我是~~这辈子都~~不会去用了。

