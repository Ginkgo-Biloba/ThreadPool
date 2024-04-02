# 简单的线程池

## 动机

> N 年前的，现在已经没有这个需求了。

因为需要在 Android 上面写 C++，鉴于：

1. NDK Clang 的 `std::thread` 是个残废，并且官方[也不打算修复了](https://github.com/android/ndk/issues/789)。以及里面的 OpenMP 也[有点问题](https://github.com/android/ndk/issues/1028)。
2. 从 OpenCV 4.2.0 里面抄的 pthreads 线程池[有内存泄漏问题](https://github.com/opencv/opencv/issues/6203)，官方说「[It is by design](https://github.com/opencv/opencv/commit/53fc5440d78fd9ebe727c51243528e1eac3b5e35#r15671484)」。如果简单地把 `TYPE*` 改为 `unique_ptr<TYPE>`，那么程序退出线程池自动析构时，子线程会挂 (hang, not crash) 在条件变量 destroy 那里。目前发现的解决方案是程序退出前，手动设置线程数为 0 或 1，就是[单线程的意思](https://docs.opencv.org/4.1.0/db/de0/group__core__utils.html#gae78625c3c2aa9e0b83ed31b73c6549c0)（我也不知道为啥文档里面不提 pthreads）。Windows 上的个人遭遇，至今不知道为什么，但是在 Android 上没有这个问题，可能使用的 pthreads-win32 有问题，仅仅是怀疑。

所以，写了这个。

线程池有两种，按照[知乎网友的说法](https://www.zhihu.com/question/27908489/answer/44060803)：

> 第一种是适合把多线程当作异步使用的，比如 Windows API 或者 C# 里的那个。调用者直接扔一个 functor 过去就可以了，等到需要返回值的时候同步一下。
>
> 第二种是主线程需要多次 spawn 出很多子线程的情况。这经常需要详细控制线程个数，并且主线程会等待子线程都完成之后才继续。

代码里面，第一种是 `AsyncPool`，第二种是 `SyncPool`。

## 原则

1. 能正常使用。不管异常安全。生命有限，不宜作死。
1. 对于不符合参数要求的调用，或者出各种问题的话，给出消息后直接挂掉。
1. 测试？封装？接口规范？复制过去能用就行了，要啥自行车。
1. 在 Windows 上用 VS2019 和 MinGW-w64 TDM-GCC 5.1.0 测试过。
1. Linux 系统在 x86_64 和 aarch64 的 GCC 上测试过。
1. `SyncPool` 同时只运行一个并行任务，嵌套的当做单线程运行。
1. 因为专注计算量大的数值或者图像任务，所以不过多关注线程池本身的同步性能。

## 参考/抄袭

`SyncPool` 的基本思路抄自 OpenCV 的 [`parallel_impl.cpp`](https://github.com/opencv/opencv/blob/4.1.0/modules/core/src/parallel_impl.cpp)。

不能跨进程使用。

1. Windows 上使用 [beginthread](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/beginthread-beginthreadex) 和 [Slim Reader/Writer (SRW) Locks](https://docs.microsoft.com/en-us/windows/win32/sync/slim-reader-writer--srw--locks) `SRWLOCK` + [Condition Variables](https://docs.microsoft.com/en-us/windows/win32/sync/condition-variables) `CONDITION_VARIABLE` 。Windows Vista 及之后可用。
1. Linux 上使用 pthread 和 [winehq](https://github.com/wine-mirror/wine/blob/87164ee3332c95f0cd9a1f3e4598056689cdfadc/dlls/ntdll/unix/sync.c) 里面用 futex 实现的 SRWLOCK 和 CONDITION_VARIABLE，差不多是 winehq 2014 年的代码了。
1. [Windows Thread Pool](https://docs.microsoft.com/en-us/windows/win32/procthread/thread-pools)。同步线程池使用这个测试了一下。此时定义 `HAVE_WIN32_POOL`。基本思想是把 Windows 自带的异步（第一种）线程池当做第二种使用。Windows Vista 及之后可用 (ref: The original thread pool has been completely rearchitected in Windows Vista)。
