#pragma once

namespace gk
{
struct Range
{
	int start, end;
	Range(int s = 0, int e = 0) : start(s), end(e) {}
	Range(Range const& r) = default;
	Range& operator =(Range const& r) = default;
};

class TPLoopBody
{
public:
	virtual void operator ()(Range const& range) const = 0;
	virtual ~TPLoopBody() {};
};

namespace details
{
// do not use this type directly.
class TPImplement;
}

class ThreadPool
{
	// why naked pointer? <memory> include too many things
	details::TPImplement* impl;

	/* 与 OpenCV 一样，简单起见，不支持超嵌套，同时只运行一项任务
	指针是为了能在 run 函数里面改变值 */
	int nestedbuf; int* nestedptr;

	ThreadPool(ThreadPool const&) = delete;
	ThreadPool& operator =(ThreadPool const&) = delete;

public:
	ThreadPool(ThreadPool&&) = default;
	ThreadPool& operator =(ThreadPool&&) = default;

	ThreadPool(int num_thread = -1);
	~ThreadPool();

	/* set the number of threads, including the main thread.
	do not call it when working */
	void set(int num_thread);

	/* get the number of threads, including the main thread */
	int get() const;

	/* need `start < end'
	consider scale range if it is too long
	negative in TPLoopBody::operator() if need start > end
	usepar 可以用来运行时切换实际使用多线程的嵌套等级 */
	void run(Range const& range, TPLoopBody const& body, bool usepar = true) const;
};

/* 方便而已。转发到全局静态线程池调用 */

void set_num_thread(int n);

int get_num_thread();

void parallel_for(Range const& range, TPLoopBody const& body, bool usepar = true);
}

