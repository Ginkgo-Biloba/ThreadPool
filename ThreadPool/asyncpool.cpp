#include "asyncpool.hpp"
#include <cstring>
#include <vector>
#include <deque>
#undef small
#undef min
#undef max
#undef abs
using std::deque;
using std::max;
using std::min;
using std::vector;

#if HAVE_PARALLEL_FRAMEWORK

#	if defined HAVE_PTHREADS_PF
#	elif defined HAVE_WIN32_THREAD
#	endif

namespace gk
{
static int sTaskIndex = 0;
static int sWorkerIndex = 0;

//////////////////// AsyncTask ////////////////////

AsyncTask::AsyncTask()
	: num_submit(0), num_finish(0)
{
	id = atomic_fetch_add(&sTaskIndex, 1);
#	if defined HAVE_PTHREADS_PF
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
#	elif defined HAVE_WIN32_THREAD
	InitializeSRWLock(&lock);
	InitializeConditionVariable(&cond);
#	endif
	log_info("AsyncTask: task %d(%p) has been created\n", id, this);
}

AsyncTask::~AsyncTask()
{
	acquire_lock(&(lock));
	log_assert(num_finish == num_submit);
	release_lock(&(lock));
#	if defined HAVE_PTHREADS_PF
	pthread_cond_destroy(&cond);
	pthread_mutex_destroy(&lock);
#	elif defined HAVE_WIN32_THREAD
	// nothing to do
#	endif
	log_info("AsyncTask: task %d(%p) has been deleted\n", id, this);
}

void AsyncTask::wait()
{
	acquire_lock(&(lock));
	while (num_finish != num_submit)
	{
		// log_assert(num_finish < num_submit);
		sleep_lock(&cond, &lock);
	}
	release_lock(&(lock));
}

namespace details
{
//////////////////// AsyncWorker ////////////////////

class AsyncWorker
{
	AsyncWorker(AsyncWorker const&) = delete;
	AsyncWorker& operator=(AsyncWorker const&) = delete;
	AsyncWorker& operator=(AsyncWorker&&) = delete;

public:
	AsyncImpl* pool;
	int id, owned, stop;

#	if defined HAVE_PTHREADS_PF
	pthread_t posix_thread;
#	elif defined HAVE_WIN32_THREAD
	unsigned win32_id;
	uintptr_t win32_thread;
#	endif

	AsyncWorker(AsyncImpl* pool);
	// for vector
	AsyncWorker(AsyncWorker&&);
	~AsyncWorker();
	void loop();
};

//////////////////// AsyncImpl ////////////////////

class AsyncImpl
{
	AsyncImpl(AsyncImpl&&) = delete;
	AsyncImpl(AsyncImpl const&) = delete;
	AsyncImpl& operator=(AsyncImpl&&) = delete;
	AsyncImpl& operator=(AsyncImpl const&) = delete;

public:
	int max_worker;
	int num_submit, num_finish;
	// do not use shared_ptr and unique_ptr
	deque<RefPtr<AsyncTask>> tasks;
	vector<AsyncWorker> workers;

#	if defined HAVE_PTHREADS_PF
	pthread_mutex_t lock_pool;
	pthread_mutex_t lock_wait;
	pthread_mutex_t lock_task;
	pthread_cond_t cond_wait;
	pthread_cond_t cond_task;
#	elif defined HAVE_WIN32_THREAD
	SRWLOCK lock_pool;
	SRWLOCK lock_wait;
	SRWLOCK lock_task;
	CONDITION_VARIABLE cond_wait;
	CONDITION_VARIABLE cond_task;
#	endif

	AsyncImpl();
	~AsyncImpl();
	int set(int size);
	void submit(RefPtr<AsyncTask> const& task);
	void submit(RefPtr<AsyncTask>* task, int len);
	void wait();
};

//////////////////// AsyncWorker ////////////////////

#	if defined HAVE_PTHREADS_PF

static void* Worker_Func(void* vp_worker)
{
	static_cast<AsyncWorker*>(vp_worker)->loop();
	return vp_worker; // just return a value
}

#	elif defined HAVE_WIN32_THREAD

static unsigned __stdcall Worker_Func(void* vp_worker)
{
	AsyncWorker* worker = static_cast<AsyncWorker*>(vp_worker);
	worker->loop();
	return worker->id; // just return a value
}

#	endif

AsyncWorker::AsyncWorker(AsyncImpl* p)
	: pool(p), owned(1), stop(0)
{
	log_assert(pool);
	id = atomic_fetch_add(&sWorkerIndex, 1);
	log_info("AsyncWorker: worker %d(%p) is being created\n", id, this);

	int err = 0;
#	if defined HAVE_PTHREADS_PF
	err |= pthread_create(&posix_thread, NULL, Worker_Func, this);
	if (err != 0)
	{
		log_error("worker %d(%p) can not create posix_thread, err = %d\n", id, this, err);
		return;
	}
#	elif defined HAVE_WIN32_THREAD
	// for initialize CRT runtime, dot not use CreateThread
	win32_thread = _beginthreadex(NULL, 0, Worker_Func, this, 0, &win32_id);
	if (win32_thread == 0)
	{
		err = GetLastError();
		log_error(
			"worker %d(%p) can not create thread, handle = %zx, win32_id = %u, err = %x\n",
			id, this, static_cast<size_t>(win32_thread), win32_id, err);
		return;
	}
#	endif
}

AsyncWorker::AsyncWorker(AsyncWorker&& rhs)
{
	pool = rhs.pool;
	id = rhs.id;
	owned = rhs.owned;
	stop = rhs.stop;
#	if defined HAVE_PTHREADS_PF
	posix_thread = rhs.posix_thread;
#	elif defined HAVE_WIN32_THREAD
	win32_id = rhs.win32_id;
	win32_thread = rhs.win32_thread;
#	endif
	rhs.owned = 0;
}

AsyncWorker::~AsyncWorker()
{
	if (!owned)
		return;
	acquire_lock(&(pool->lock_task));
	stop = 1;
	release_lock(&(pool->lock_task));
	wake_all_cond(&(pool->cond_task));
#	if defined HAVE_PTHREADS_PF
	pthread_join(posix_thread, NULL);
#	elif defined HAVE_WIN32_THREAD
	WaitForSingleObject(reinterpret_cast<HANDLE>(win32_thread), INFINITE);
	CloseHandle(reinterpret_cast<HANDLE>(win32_thread));
#	endif
	log_info("AsyncWorker: worker %d(%p) has been deleted\n", id, this);
}

void AsyncWorker::loop()
{
	log_info("AsyncWorker: worker %d start now\n", id);
	for (;;)
	{
		RefPtr<AsyncTask> task;
		acquire_lock(&(pool->lock_task));
		while (!stop && pool->tasks.empty())
		{
			log_info("AsyncWorker: worker %d wait (sleep)...\n", id);
			sleep_lock(&(pool->cond_task), &(pool->lock_task));
		}
		if (!stop)
		{
			task = pool->tasks.front();
			if (task)
				pool->tasks.pop_front();
		}
		release_lock(&(pool->lock_task));

		// 传递空指针表示结束
		if (stop || !task)
			break;

		task->call();
		acquire_lock(&(task->lock));
		int finish = ++(task->num_finish);
		int submit = task->num_submit;
		release_lock(&(task->lock));
		// log_assert(finish <= submit);
		log_info(
			"AsyncWorker: worker %d, task %d, finish %d, submit %d\n",
			id, task->id, finish, submit);
		if (finish == submit)
			wake_all_cond(&(task->cond));
		acquire_lock(&(pool->lock_wait));
		++(pool->num_finish);
		release_lock(&(pool->lock_wait));
		wake_all_cond(&(pool->cond_wait));
	}
	log_info("AsyncWorker: worker %d stop now\n", id);
}

//////////////////// AsyncImpl ////////////////////

AsyncImpl::AsyncImpl()
	: num_submit(0), num_finish(0)
{
	log_info(GK_Func);
#	if defined HAVE_PTHREADS_PF
	int err = 0;
	err |= pthread_mutex_init(&lock_pool, NULL);
	err |= pthread_mutex_init(&lock_wait, NULL);
	err |= pthread_mutex_init(&lock_task, NULL);
	err |= pthread_cond_init(&cond_wait, NULL);
	err |= pthread_cond_init(&cond_task, NULL);
	if (err)
		log_error("failed to initialize AsyncImpl (pthreads)");
	max_worker = pthread_num_processors_np() * 2; // not too much
#	elif defined HAVE_WIN32_THREAD
	InitializeSRWLock(&lock_pool);
	InitializeSRWLock(&lock_wait);
	InitializeSRWLock(&lock_task);
	InitializeConditionVariable(&cond_wait);
	InitializeConditionVariable(&cond_task);
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	max_worker = sysinfo.dwNumberOfProcessors * 2; // 2 for SMT/HT ?
#	endif
	workers.reserve(max_worker);
}

AsyncImpl::~AsyncImpl()
{
	// 明显起见，你需要手动等待所有任务完成
	// wait();
	acquire_lock(&lock_wait);
	log_assert(num_finish == num_submit);
	release_lock(&lock_wait);
	acquire_lock(&(lock_pool));
	log_assert(tasks.empty());
	int len = static_cast<int>(workers.size());
	log_info("AsyncImpl: destroy AsyncImpl with %d worker(s)\n", len);
	acquire_lock(&lock_task);
	tasks.push_back(nullptr);
	release_lock(&lock_task);
	wake_all_cond(&cond_task);
	while (len--)
		workers.pop_back();
	release_lock(&(lock_pool));
#	if defined HAVE_PTHREADS_PF
	log_assert(!pthread_cond_destroy(&cond_task));
	log_assert(!pthread_cond_destroy(&cond_wait));
	log_assert(!pthread_mutex_destroy(&lock_task));
	log_assert(!pthread_mutex_destroy(&lock_wait));
	log_assert(!pthread_mutex_destroy(&lock_pool));
#	elif defined HAVE_WIN32_THREAD
	_CRT_UNUSED(len);
	// nothing to do
#	endif
}

int AsyncImpl::set(int size)
{
	acquire_lock(&(lock_pool));
	int curr = static_cast<int>(workers.size());
	if (size != INT_MIN)
	{
		if (size <= 0)
			size = max_worker / 2;
		size = min(size, max_worker);
		for (int i = curr; i < size; ++i)
			workers.emplace_back(this);
		for (int i = curr; i > size; --i)
			workers.pop_back();
	}
	release_lock(&(lock_pool));
	return curr;
}

void AsyncImpl::submit(RefPtr<AsyncTask> const& task)
{
	if (!task)
		return;
	acquire_lock(&lock_pool);
	log_assert(workers.size());
	release_lock(&lock_pool);
	acquire_lock(&lock_wait);
	++num_submit;
	release_lock(&lock_wait);
	acquire_lock(&(task->lock));
	++(task->num_submit);
	release_lock(&(task->lock));
	acquire_lock(&(lock_task));
	tasks.push_back(task);
	release_lock(&(lock_task));
	wake_cond(&(cond_task));
}

void AsyncImpl::submit(RefPtr<AsyncTask>* task, int len)
{
	if (len < 1)
		return;
	acquire_lock(&lock_pool);
	log_assert(workers.size());
	release_lock(&lock_pool);
	acquire_lock(&lock_wait);
	num_submit += len;
	release_lock(&lock_wait);
	acquire_lock(&(lock_task));
	int i = 0;
	for (; i < len; ++i)
	{
		if (!task[i])
			continue;
		acquire_lock(&(task[i]->lock));
		++(task[i]->num_submit);
		release_lock(&(task[i]->lock));
		tasks.push_back(task[i]);
	}
	release_lock(&(lock_task));
	if (i == 1)
		wake_cond(&(cond_task));
	else if (i > 1)
		wake_all_cond(&(cond_task));
}

void AsyncImpl::wait()
{
	acquire_lock(&lock_wait);
	while (num_finish != num_submit)
	{
		// log_assert(num_finish < num_submit);
		sleep_lock(&cond_wait, &lock_wait);
	}
	release_lock(&lock_wait);
}
}

//////////////////// AsyncPool ////////////////////

AsyncPool::AsyncPool()
{
	impl = new details::AsyncImpl;
}

AsyncPool::AsyncPool(AsyncPool&& rhs)
{
	impl = rhs.impl;
	rhs.impl = nullptr;
}

AsyncPool::~AsyncPool()
{
	if (impl)
		delete impl;
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

#else

namespace gk
{
AsyncTask::AsyncTask()
	: num_submit(0), num_finish(0), id(-1) { }

AsyncTask::~AsyncTask() { }

void AsyncTask::wait() { }

AsyncPool::AsyncPool()
	: impl(nullptr) { }

AsyncPool::AsyncPool(AsyncPool&&) = default;

AsyncPool::~AsyncPool() { }

int AsyncPool::get()
{
	return 0;
}

int AsyncPool::set(int)
{
	return 0;
}

void AsyncPool::submit(RefPtr<AsyncTask> const& task)
{
	log_assert(task);
	task->call();
}

void AsyncPool::submit(RefPtr<AsyncTask>* task, size_t len)
{
	for (size_t i = 0; i < len; ++i)
	{
		log_assert(task[i]);
		task[i]->call();
	}
}

void AsyncPool::wait() { }

}

#endif
