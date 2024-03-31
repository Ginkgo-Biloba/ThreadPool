#include "asyncpool.hpp"
#include <cstring>
#include <climits>
#include <vector>
#include <deque>
#undef small
#undef min
#undef max
#undef abs

#if defined __linux__
#elif defined _WIN32
#endif

namespace gk {
static int sTaskIndex = 0;
static int sWorkerIndex = 0;

//////////////////// AsyncTask ////////////////////

AsyncTask::AsyncTask()
	: num_submit(0), num_finish(0)
{
	id = atomic_fetch_add(&sTaskIndex, 1);
	lock = SRWLOCK_INIT;
	cond = CONDITION_VARIABLE_INIT;
	GK_LOG_INFO(
		"AsyncTask: task %d(%p) has been created\n",
		id, static_cast<void*>(this));
}

AsyncTask::~AsyncTask()
{
	AcquireSRWLockExclusive(&(lock));
	GK_ASSERT(num_finish == num_submit);
	ReleaseSRWLockExclusive(&(lock));
	GK_LOG_INFO(
		"AsyncTask: task %d(%p) has been deleted\n",
		id, static_cast<void*>(this));
}

void AsyncTask::wait()
{
	AcquireSRWLockExclusive(&(lock));
	while (num_finish != num_submit) {
		// GK_ASSERT(num_finish < num_submit);
		SleepConditionVariableSRW(&cond, &lock, INFINITE, 0);
	}
	ReleaseSRWLockExclusive(&(lock));
}

namespace details {
//////////////////// AsyncWorker ////////////////////

class AsyncWorker {
	AsyncWorker(AsyncWorker const&) = delete;
	AsyncWorker& operator=(AsyncWorker const&) = delete;
	AsyncWorker& operator=(AsyncWorker&&) = delete;

public:
	AsyncImpl* pool;
	int id, owned, stop;

#if defined __linux__
	pthread_t posix_thread;
#elif defined _WIN32
	unsigned win32_id;
	uintptr_t win32_thread;
#endif

	AsyncWorker(AsyncImpl* pool);
	// for vector
	AsyncWorker(AsyncWorker&&);
	~AsyncWorker();
	void loop();
};

//////////////////// AsyncImpl ////////////////////

class AsyncImpl {
	AsyncImpl(AsyncImpl&&) = delete;
	AsyncImpl(AsyncImpl const&) = delete;
	AsyncImpl& operator=(AsyncImpl&&) = delete;
	AsyncImpl& operator=(AsyncImpl const&) = delete;

public:
	int max_worker;
	int num_submit, num_finish;
	// do not use shared_ptr and unique_ptr
	std::deque<RefPtr<AsyncTask>> tasks;
	std::vector<AsyncWorker> workers;

	SRWLOCK lock_pool;
	SRWLOCK lock_wait;
	SRWLOCK lock_task;
	CONDITION_VARIABLE cond_wait;
	CONDITION_VARIABLE cond_task;

	AsyncImpl();
	~AsyncImpl();
	int set(int size);
	void submit(RefPtr<AsyncTask> const& task);
	void submit(RefPtr<AsyncTask>* task, int len);
	void wait();
};

//////////////////// AsyncWorker ////////////////////

#if defined __linux__

static void* Worker_Func(void* vp_worker)
{
	static_cast<AsyncWorker*>(vp_worker)->loop();
	return vp_worker; // just return a value
}

#elif defined _WIN32

static unsigned __stdcall Worker_Func(void* vp_worker)
{
	AsyncWorker* worker = static_cast<AsyncWorker*>(vp_worker);
	worker->loop();
	return worker->id; // just return a value
}

#endif

AsyncWorker::AsyncWorker(AsyncImpl* p)
	: pool(p), owned(1), stop(0)
{
	GK_ASSERT(pool);
	id = atomic_fetch_add(&sWorkerIndex, 1);
	GK_LOG_INFO(
		"AsyncWorker: worker %d(%p) is being created\n", id, static_cast<void*>(this));

	int err = 0;
#if defined __linux__
	err |= pthread_create(&posix_thread, NULL, Worker_Func, this);
	if (err != 0) {
		GK_LOG_ERROR(
			"worker %d(%p) can not create posix_thread, err = %d\n",
			id, static_cast<void*>(this), err);
		return;
	}
#elif defined _WIN32
	// for initialize CRT runtime, dot not use CreateThread
	win32_thread = _beginthreadex(NULL, 0, Worker_Func, this, 0, &win32_id);
	if (win32_thread == 0) {
		err = GetLastError();
		GK_LOG_ERROR(
			"worker %d(%p) can not create thread, handle = %zx, win32_id = %u, err = %x\n",
			id, static_cast<void*>(this), static_cast<size_t>(win32_thread), win32_id, err);
		return;
	}
#endif
}

AsyncWorker::AsyncWorker(AsyncWorker&& rhs)
{
	pool = rhs.pool;
	id = rhs.id;
	owned = rhs.owned;
	stop = rhs.stop;
#if defined __linux__
	posix_thread = rhs.posix_thread;
#elif defined _WIN32
	win32_id = rhs.win32_id;
	win32_thread = rhs.win32_thread;
#endif
	rhs.owned = 0;
}

AsyncWorker::~AsyncWorker()
{
	if (!owned)
		return;
	AcquireSRWLockExclusive(&(pool->lock_task));
	stop = 1;
	ReleaseSRWLockExclusive(&(pool->lock_task));
	WakeAllConditionVariable(&(pool->cond_task));
#if defined __linux__
	pthread_join(posix_thread, NULL);
#elif defined _WIN32
	WaitForSingleObject(reinterpret_cast<HANDLE>(win32_thread), INFINITE);
	CloseHandle(reinterpret_cast<HANDLE>(win32_thread));
#endif
	GK_LOG_INFO(
		"AsyncWorker: worker %d(%p) has been deleted\n",
		id, static_cast<void*>(this));
}

void AsyncWorker::loop()
{
	GK_LOG_INFO("AsyncWorker: worker %d start now\n", id);
	for (;;) {
		RefPtr<AsyncTask> task;
		AcquireSRWLockExclusive(&(pool->lock_task));
		while (!stop && pool->tasks.empty()) {
			GK_LOG_INFO("AsyncWorker: worker %d wait (sleep)...\n", id);
			SleepConditionVariableSRW(
				&(pool->cond_task), &(pool->lock_task), INFINITE, 0);
		}
		if (!stop) {
			task = pool->tasks.front();
			if (task)
				pool->tasks.pop_front();
		}
		ReleaseSRWLockExclusive(&(pool->lock_task));

		// 传递空指针表示结束
		if (stop || !task)
			break;

		task->call();
		AcquireSRWLockExclusive(&(task->lock));
		int finish = ++(task->num_finish);
		int submit = task->num_submit;
		ReleaseSRWLockExclusive(&(task->lock));
		// GK_ASSERT(finish <= submit);
		GK_LOG_INFO(
			"AsyncWorker: worker %d, task %d, finish %d, submit %d\n",
			id, task->id, finish, submit);
		if (finish == submit)
			WakeAllConditionVariable(&(task->cond));
		AcquireSRWLockExclusive(&(pool->lock_wait));
		++(pool->num_finish);
		ReleaseSRWLockExclusive(&(pool->lock_wait));
		WakeAllConditionVariable(&(pool->cond_wait));
	}
	GK_LOG_INFO("AsyncWorker: worker %d stop now\n", id);
}

//////////////////// AsyncImpl ////////////////////

AsyncImpl::AsyncImpl()
	: num_submit(0), num_finish(0)
{
	lock_pool = SRWLOCK_INIT;
	lock_wait = SRWLOCK_INIT;
	lock_task = SRWLOCK_INIT;
	cond_wait = CONDITION_VARIABLE_INIT;
	cond_task = CONDITION_VARIABLE_INIT;
	max_worker = getNumberOfCPU() * 2;
	workers.reserve(max_worker);
}

AsyncImpl::~AsyncImpl()
{
	// 明显起见，你需要手动等待所有任务完成
	// wait();
	AcquireSRWLockExclusive(&lock_wait);
	GK_ASSERT(num_finish == num_submit);
	ReleaseSRWLockExclusive(&lock_wait);
	AcquireSRWLockExclusive(&lock_pool);
	GK_ASSERT(tasks.empty());
	int len = static_cast<int>(workers.size());
	GK_LOG_INFO("AsyncImpl: destroy AsyncImpl with %d worker(s)\n", len);
	AcquireSRWLockExclusive(&lock_task);
	tasks.push_back(nullptr);
	ReleaseSRWLockExclusive(&lock_task);
	WakeAllConditionVariable(&cond_task);
	while (len--)
		workers.pop_back();
	ReleaseSRWLockExclusive(&(lock_pool));
}

int AsyncImpl::set(int size)
{
	AcquireSRWLockExclusive(&(lock_pool));
	int curr = static_cast<int>(workers.size());
	if (size != INT_MIN) {
		if (size <= 0)
			size = max_worker / 2;
		size = min(size, max_worker);
		for (int i = curr; i < size; ++i)
			workers.emplace_back(this);
		for (int i = curr; i > size; --i)
			workers.pop_back();
	}
	ReleaseSRWLockExclusive(&(lock_pool));
	return curr;
}

void AsyncImpl::submit(RefPtr<AsyncTask> const& task)
{
	if (!task)
		return;
	AcquireSRWLockExclusive(&lock_pool);
	GK_ASSERT(workers.size());
	ReleaseSRWLockExclusive(&lock_pool);
	AcquireSRWLockExclusive(&lock_wait);
	++num_submit;
	ReleaseSRWLockExclusive(&lock_wait);
	AcquireSRWLockExclusive(&(task->lock));
	++(task->num_submit);
	ReleaseSRWLockExclusive(&(task->lock));
	AcquireSRWLockExclusive(&(lock_task));
	tasks.push_back(task);
	ReleaseSRWLockExclusive(&(lock_task));
	WakeConditionVariable(&(cond_task));
}

void AsyncImpl::submit(RefPtr<AsyncTask>* task, int len)
{
	if (len < 1)
		return;
	AcquireSRWLockExclusive(&lock_pool);
	GK_ASSERT(workers.size());
	ReleaseSRWLockExclusive(&lock_pool);
	AcquireSRWLockExclusive(&lock_wait);
	num_submit += len;
	ReleaseSRWLockExclusive(&lock_wait);
	AcquireSRWLockExclusive(&(lock_task));
	int ok = 0;
	for (int i = 0; i < len; ++i) {
		if (!task[i])
			continue;
		++ok;
		AcquireSRWLockExclusive(&(task[i]->lock));
		++(task[i]->num_submit);
		ReleaseSRWLockExclusive(&(task[i]->lock));
		tasks.push_back(task[i]);
	}
	ReleaseSRWLockExclusive(&(lock_task));
	if (ok == 1)
		WakeConditionVariable(&cond_task);
	else if (ok > 1)
		WakeAllConditionVariable(&cond_task);
}

void AsyncImpl::wait()
{
	AcquireSRWLockExclusive(&lock_wait);
	while (num_finish != num_submit) {
		// GK_ASSERT(num_finish < num_submit);
		SleepConditionVariableSRW(&cond_wait, &lock_wait, INFINITE, 0);
	}
	ReleaseSRWLockExclusive(&lock_wait);
}
}

//////////////////// AsyncPool ////////////////////

AsyncPool::AsyncPool()
{
	using details::AsyncImpl;
	constexpr ptrdiff_t to = GK_ALIGNOF(AsyncImpl);
	static_assert(to + sizeof(AsyncImpl) <= sizeof(storage), "");
	impl = nullptr;
	size_t base = reinterpret_cast<size_t>(static_cast<void*>(storage));
	AsyncImpl* ptr = reinterpret_cast<AsyncImpl*>((base + to - 1) & -to);
	new (ptr) AsyncImpl;
	impl = ptr;
}

AsyncPool::~AsyncPool()
{
	if (impl)
		impl->~AsyncImpl();
	impl = nullptr;
}

int AsyncPool::get()
{
	return impl->set(INT_MIN);
}

int AsyncPool::set(int size)
{
	return impl->set(size);
}

void AsyncPool::submit(RefPtr<AsyncTask> const& task)
{
	impl->submit(task);
}

void AsyncPool::submit(RefPtr<AsyncTask>* tasks, size_t len)
{
	impl->submit(tasks, static_cast<int>(len));
}

void AsyncPool::wait()
{
	impl->wait();
}
}
