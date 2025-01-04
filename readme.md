# 简单的线程池

线程池有两种，按照[知乎网友的说法](https://www.zhihu.com/question/27908489/answer/44060803)：

> 第一种是适合把多线程当作异步使用的，比如 Windows API 或者 C# 里的那个。调用者直接扔一个 functor 过去就可以了，等到需要返回值的时候同步一下。
>
> 第二种是主线程需要多次 spawn 出很多子线程的情况。这经常需要详细控制线程个数，并且主线程会等待子线程都完成之后才继续。

代码里面，第一种是 `AsyncPool`，第二种是 `SyncPool`。不能跨进程使用。

`SyncPool` 的基本思路抄自 OpenCV 的 [`parallel_impl.cpp`](https://github.com/opencv/opencv/blob/4.1.0/modules/core/src/parallel_impl.cpp)。

- 能正常使用。不管异常安全。生命有限，不宜作死。
- 对于不符合参数要求的调用，或者出各种问题的话，给出消息后直接挂掉。
- 测试？封装？接口规范？复制过去能用就行了，要啥自行车。
- 在 Windows 8.1 / 10.0.19045 上用 VS2019 / MinGW-w64 TDM-GCC 5.1.0 测试过。
- Linux 系统在 x86_64 / aarch64 用 GCC 13.2 / 9.4 上测试过。
- 因为专注计算量大的数值或者图像任务，所以不过多关注线程池本身的同步性能。
- Windows 上使用 [beginthreadex](https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/beginthread-beginthreadex) / [Slim Reader/Writer (SRW) Locks](https://docs.microsoft.com/en-us/windows/win32/sync/slim-reader-writer--srw--locks) + [Condition Variables](https://docs.microsoft.com/en-us/windows/win32/sync/condition-variables) / [Keyed Event](http://locklessinc.com/articles/keyed_events)。Windows Vista 及之后可用。
- Linux 上使用 [pthreads](https://www.man7.org/linux/man-pages/man7/pthreads.7.html) / [futex](https://www.man7.org/linux/man-pages/man2/futex.2.html)。
