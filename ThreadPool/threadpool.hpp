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


class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	// ʵ������ ((stop - start) < INT_MAX / 2) && (start <= stop)
	void run(TPLoopBody const& body, Range const& range, int nstripe = -1) const;
};


// ������ѡ�ת����ȫ�־�̬�̳߳ص���
void parallel_for(Range const& range, TPLoopBody const& body, int nstripe = -1);
}
