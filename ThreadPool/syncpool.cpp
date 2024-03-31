// #define HAVE_WIN32_POOL
#if defined HAVE_WIN32_POOL
#	include <Windows.h>
#	include <threadpoolapiset.h>
#endif

#include "syncpool.hpp"
#include <vector>
#include <cstring>
#include <cstdint>
#include <climits>
#undef small
#undef min
#undef max
#undef abs

namespace gk {
static int sJobIndex = 0;
static int sWorkerIndex = 0;

//////////////////// SyncJob ////////////////////

SyncJob::SyncJob()
	: max_call(INT_MAX), start(0), stop(0)
{
	id = atomic_fetch_add(&sJobIndex, 1);
	GK_LOG_INFO("job %d(%p) has been created\n", id, static_cast<void*>(this));
}

SyncJob::~SyncJob()
{
	GK_ASSERT(active == completed);
	GK_ASSERT(finished);
	GK_LOG_INFO("job %d(%p) has been deleted\n", id, static_cast<void*>(this));
}

int SyncJob::schedule(int n)
{
	index = start;
	n = max(n, 1);
	// adjust stripe number depend on the specific work
	nstripe = min(n * min(n, 4), 128);
	nstripe = min(nstripe, max(stop - start, 1));
	int64_t stripe = static_cast<int64_t>(stop);
	stripe = (stripe - start + nstripe - 1) / nstripe;
	// if GK_ASSERT failed, consider scale or offset the range
	GK_ASSERT(stop < INT_MAX - stripe + start);
	active = completed = finished = 0;
	GK_LOG_INFO(
		"job %d(%p) has been scheduled [%d, %d) nstripe %d \n",
		id, static_cast<void*>(this), start, stop, nstripe);
	return nstripe;
}

int SyncJob::call(bool spawned)
{
	int sum = 0;
	int cur, len;
	for (;;) {
		cur = atomic_load(&index);
		if (cur >= stop)
			break;
		len = max((stop - cur) / nstripe, 1);
		cur = atomic_fetch_add(&index, len);
		if (cur >= stop)
			break;
		len = min(len, stop - cur);
		sum += len;
		call(cur, cur + len);
	}
	int fin = atomic_load(&finished);
	if (spawned && fin) {
		GK_LOG_ERROR(
			"xxxxx BUG xxxxxx\n"
			"\tjob %d(%p) start %d, stop %d, active %d, completed %d\n",
			id, static_cast<void*>(this), start, stop, active, completed);
	}
	return sum;
}

#if defined HAVE_WIN32_POOL

namespace details {
class SyncImpl {
	SyncImpl(SyncImpl&&) = delete;
	SyncImpl(SyncImpl const&) = delete;
	SyncImpl& operator=(SyncImpl&&) = delete;
	SyncImpl& operator=(SyncImpl const&) = delete;

	int max_worker, num_worker;
	// the actual Thread-Pool resource
	TP_POOL* pool;
	// connect work-items to our custom Thread-Pool
	TP_CALLBACK_ENVIRON callback_environ;
	// clean things up neatly when we are done
	TP_CLEANUP_GROUP* cleanup_group;
	SRWLOCK lock_pool;

public:
	SyncImpl();
	~SyncImpl();
	int set(int n);
	void submit(RefPtr<SyncJob> const& job);

	// no need to use PTP_CALLBACK_INSTANCE and PTP_WORK
	static VOID CALLBACK Func(PTP_CALLBACK_INSTANCE, PVOID vp_job, PTP_WORK)
	{
		SyncJob* job = static_cast<SyncJob*>(vp_job);
		if (atomic_load(&(job->index)) < job->stop)
			job->call(true);
	}
};

SyncImpl::SyncImpl()
{
	int err = 0;
	pool = CreateThreadpool(NULL);
	if (!pool) {
		err = GetLastError();
		GK_LOG_ERROR("SyncImpl: CreateThreadpool failed, err = %d\n", err);
	}

	TpInitializeCallbackEnviron(&callback_environ);
	TpSetCallbackThreadpool(&callback_environ, pool);

	cleanup_group = CreateThreadpoolCleanupGroup();
	if (cleanup_group == NULL) {
		err = GetLastError();
		GK_LOG_ERROR("SyncImpl: CreateThreadpoolCleanupGroup failed, err = %d\n", err);
	}
	TpSetCallbackCleanupGroup(&callback_environ, cleanup_group, NULL);

	lock_pool = SRWLOCK_INIT;
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	max_worker = sysinfo.dwNumberOfProcessors * 2;
	num_worker = 0;
}

SyncImpl::~SyncImpl()
{
	// wait for any previously scheduled tasks to complete, but stop accepting new ones
	CloseThreadpoolCleanupGroupMembers(cleanup_group, FALSE, NULL);
	// clean-up resources
	CloseThreadpoolCleanupGroup(cleanup_group);
	TpDestroyCallbackEnviron(&callback_environ);
	CloseThreadpool(pool);
}

int SyncImpl::set(int size)
{
	AcquireSRWLockExclusive(&lock_pool);
	int curr = num_worker;
	if (size != INT_MIN) {
		if (size <= 0)
			size = max_worker / 2;
		size = min(size, max_worker);
		if (size > 1)
			SetThreadpoolThreadMaximum(pool, size - 1);
		num_worker = size - 1;
	}
	ReleaseSRWLockExclusive(&lock_pool);
	return curr + 1;
}

void SyncImpl::submit(RefPtr<SyncJob> const& job)
{
	int child = num_worker;
	child = min(child, job->schedule(child + 1) - 1);
	if (child < 1) {
		job->call(job->start, job->stop);
		job->finished = 1;
		return;
	}

	AcquireSRWLockExclusive(&lock_pool);
	do {
		TP_WORK* work = CreateThreadpoolWork(Func, job.get(), &callback_environ);
		if (work == NULL) {
			GK_LOG_ERROR("SyncImpl: CreateThreadpoolWork failed, err = %d",
				static_cast<int>(GetLastError()));
			// try or not?
			// job->call(start, stop);
			// break;
		}
		for (int i = 0; i < num_worker; ++i)
			SubmitThreadpoolWork(work);
		job->call(true);
		GK_LOG_INFO(
			"SyncImpl: WaitForThreadpoolWorkCallbacks for job %d\n",
			job->id);
		WaitForThreadpoolWorkCallbacks(work, FALSE);
		CloseThreadpoolWork(work);
		job->finished = 1;
		GK_LOG_INFO("SyncImpl: done for job %d\n", job->id);
	} while (0);
	ReleaseSRWLockExclusive(&lock_pool);
}
}

#else

namespace details {
//////////////////// SyncWorker ////////////////////

class SyncWorker {
	enum {
		ActiveWait = 1024
	};

	SyncWorker(SyncWorker const&) = delete;
	SyncWorker& operator=(SyncWorker&&) = delete;
	SyncWorker& operator=(SyncWorker const&) = delete;

public:
	SyncImpl* pool;
	RefPtr<SyncJob> job;

	// index in `SyncImpl::workers'
	int id;
	int owned, stopped;
	int wake_signal;

	SRWLOCK lock_job;
	CONDITION_VARIABLE cond_job;
#	if defined __linux__
	pthread_t posix_thread;
#	elif defined _WIN32
	uintptr_t win32_thread;
	unsigned win32_id;
#	endif

	SyncWorker(SyncImpl* pool);
	// for vector
	SyncWorker(SyncWorker&&);
	~SyncWorker();
	void assign(RefPtr<SyncJob> const& rhs);
	void loop();
};

//////////////////// SyncImpl ////////////////////

class SyncImpl {
	SyncImpl(SyncImpl&&) = delete;
	SyncImpl(SyncImpl const&) = delete;
	SyncImpl& operator=(SyncImpl&&) = delete;
	SyncImpl& operator=(SyncImpl const&) = delete;

	enum {
		OnIdle = 0,
		OnBusy = 1,
		ActiveWait = 10240
	};

	int on_worker;
	int max_worker;
	std::vector<SyncWorker> workers;

public:
	SRWLOCK lock_work;
	CONDITION_VARIABLE cond_work;

	SyncImpl();
	~SyncImpl();
	int set(int size);
	void submit(RefPtr<SyncJob> const& job);
};

//////////////////// SyncWorker ////////////////////

#	if defined __linux__

static void* Worker_Func(void* vp_worker)
{
	static_cast<SyncWorker*>(vp_worker)->loop();
	return vp_worker; // just return a value
}

#	elif defined _WIN32

static unsigned __stdcall Worker_Func(void* vp_worker)
{
	SyncWorker* worker = static_cast<SyncWorker*>(vp_worker);
	worker->loop();
	return worker->id; // just return a value
}

#	endif

SyncWorker::SyncWorker(SyncImpl* p)
	: pool(p), owned(1), stopped(0), wake_signal(0)
{
	id = atomic_fetch_add(&sWorkerIndex, 1);
	GK_ASSERT(p && "must work with SyncImpl");
	GK_LOG_INFO("SyncWorker: worker %d is being created\n", id);
	int err = 0;
#	if defined __linux__
	err = pthread_create(&posix_thread, NULL, Worker_Func, this);
	if (err != 0) {
		GK_LOG_ERROR("worker %d can not create mutex, err = %d\n", id, err);
		return;
	}
#	elif defined _WIN32
	win32_thread = _beginthreadex(NULL, 0, Worker_Func, this, 0, &win32_id);
	if (win32_thread == 0) {
		err = GetLastError();
		GK_LOG_ERROR("worker %d can not create thread, "
								 "handle = %zx, win32_id = %u, err = %x\n",
			id, static_cast<size_t>(win32_thread), win32_id, err);
		return;
	}
#	endif
	lock_job = SRWLOCK_INIT;
	cond_job = CONDITION_VARIABLE_INIT;
	GK_LOG_INFO("SyncWorker: worker %d has been created\n", id);
}

SyncWorker::SyncWorker(SyncWorker&& rhs)
{
	pool = rhs.pool;
	job.swap(rhs.job);
	id = rhs.id;
	owned = rhs.owned;
	stopped = rhs.stopped;
	wake_signal = rhs.wake_signal;
	lock_job = rhs.lock_job;
	cond_job = rhs.cond_job;
#	if defined __linux__
	posix_thread = rhs.posix_thread;
#	elif defined _WIN32
	win32_thread = rhs.win32_thread;
	win32_id = rhs.win32_id;
#	endif
	rhs.owned = 0;
}

SyncWorker::~SyncWorker()
{
	if (!owned)
		return;
	GK_LOG_INFO("SyncWorker: worker %d is being deleted\n", id);
	AcquireSRWLockExclusive(&lock_job);
	GK_ASSERT(!stopped && "repeate stop");
	// job.reset();
	stopped = 1;
	wake_signal = 1;
	ReleaseSRWLockExclusive(&lock_job);
	WakeConditionVariable(&cond_job);

#	if defined __linux__
	pthread_join(posix_thread, NULL);
#	elif defined _WIN32
	WaitForSingleObject(reinterpret_cast<HANDLE>(win32_thread), INFINITE);
	CloseHandle(reinterpret_cast<HANDLE>(win32_thread));
#	endif
	GK_LOG_INFO("SyncWorker: worker %d has been deleted\n", id);
}

void SyncWorker::assign(RefPtr<SyncJob> const& rhs)
{
	GK_ASSERT(!stopped && "has stopped");
	GK_LOG_INFO("worker %d is assigned job %d\n", id, rhs->id);
	AcquireSRWLockExclusive(&lock_job);
	// make sure that previous job is finished
	GK_ASSERT(!job || job->finished);
	job = rhs;
	wake_signal = 1;
	ReleaseSRWLockExclusive(&lock_job);
	WakeConditionVariable(&cond_job);
}

void SyncWorker::loop()
{
	GK_LOG_INFO("worker %d start now\n", id);
	while (!stopped) {
		RefPtr<SyncJob> J;
		GK_LOG_INFO("worker %d loop (pause)...\n", id);
		for (int i = 0; i < ActiveWait; ++i) {
			if (atomic_load(&wake_signal))
				break;
			yield(16);
		}

		AcquireSRWLockExclusive(&lock_job);
		while (!wake_signal) {
			GK_LOG_INFO("worker %d wait (sleep)...\n", id);
			SleepConditionVariableSRW(&cond_job, &lock_job, INFINITE, 0);
		}
		J.swap(job);
		wake_signal = 0;
		ReleaseSRWLockExclusive(&lock_job);

		if (!stopped && J && (atomic_load(&(J->start)) < J->stop)) {
			int active = atomic_fetch_add(&(J->active), 1);
			GK_LOG_INFO("worker %d do job %d as %d\n", id, J->id, active);
			J->call(true);

			int completed = atomic_fetch_add(&(J->completed), 1) + 1;
			active = atomic_load(&(J->active));
			if (active == completed) {
				// finished (marked by others) before `I' mark it ?
				int finished = atomic_exchange(&(J->finished), 1);
				if (!finished) {
					GK_LOG_INFO("worker %d mark job %d finished and notify the main thread\n", id, J->id);
					AcquireSRWLockExclusive(&(pool->lock_work));
					ReleaseSRWLockExclusive(&(pool->lock_work));
					WakeConditionVariable(&(pool->cond_work));
				}
			}
		} else {
			if (J)
				GK_LOG_INFO("worker %d get job %d which has been finished\n", id, J->id);
			else
				GK_LOG_INFO("worker %d has no more jobs\n", id);
		}
	}
}

//////////////////// SyncImpl ////////////////////

SyncImpl::SyncImpl()
	: on_worker(OnIdle)
{
	GK_LOG_INFO("SyncImpl: create SyncImpl start\n");
	lock_work = SRWLOCK_INIT;
	cond_work = CONDITION_VARIABLE_INIT;
	max_worker = getNumberOfCPU() * 2;
	workers.reserve(max_worker);
	GK_LOG_INFO("SyncImpl: create SyncImpl stop, max_worker %d\n", max_worker);
}

SyncImpl::~SyncImpl()
{
	GK_ASSERT(atomic_exchange(&on_worker, OnBusy) == OnIdle);
	int len = static_cast<int>(workers.size());
	GK_LOG_INFO("SyncImpl: destroy SyncImpl with %d workers\n", len);
	while (len--) {
		GK_ASSERT(!(workers[len].job) || (workers[len].job->finished));
		workers.pop_back();
	}
	atomic_store(&on_worker, OnIdle);
}

int SyncImpl::set(int size)
{
	GK_ASSERT(atomic_exchange(&on_worker, OnBusy) == OnIdle);
	int curr = static_cast<int>(workers.size());
	if (size != INT_MIN) {
		if (size <= 0)
			size = max_worker / 2;
		size = min(size, max_worker);
		GK_LOG_INFO("SyncImpl: change the number of workers %d -> %d\n",
			curr, size - 1);
		for (int i = curr; i < size - 1; ++i)
			workers.emplace_back(this);
		for (int i = curr; i > size - 1; --i)
			workers.pop_back();
	}
	atomic_store(&on_worker, OnIdle);
	return curr + 1;
}

void SyncImpl::submit(RefPtr<SyncJob> const& job)
{
	int child = static_cast<int>(workers.size());
	child = min(child, job->schedule(child + 1) - 1);
	if (child < 1 || atomic_exchange(&on_worker, OnBusy) != OnIdle) {
		job->call(job->start, job->stop);
		job->finished = 1;
		return;
	}

	for (int i = 0; i < child; ++i)
		workers[i].assign(job);
	job->call(false);
	GK_ASSERT(job->index >= job->stop);
	atomic_exchange(&on_worker, OnIdle);

	int finished = atomic_load(&(job->finished));
	int active = atomic_load(&(job->active));
	// have finished job, or just the main thread is working
	if (finished || (active == 0)) {
		GK_LOG_INFO("SyncImpl: no worker in progress for job %d active %d\n", job->id, active);
		job->finished = 1;
	} else {
		GK_LOG_INFO("SyncImpl: loop (pause) for job %d\n", job->id);
		// don't spin too much in any case (inaccurate getTickCount())
		for (int i = 0; i < ActiveWait; ++i) {
			finished = atomic_load(&(job->finished));
			if (finished) {
				GK_LOG_INFO("SyncImpl: job %d is finished by others (pause)\n", job->id);
				break;
			}
			yield(16);
		}
		if (!finished) {
			GK_LOG_INFO("SyncImpl: wait (sleep) for job %d\n", job->id);
			AcquireSRWLockExclusive(&lock_work);
			for (;;) {
				finished = atomic_load(&(job->finished));
				if (finished) {
					GK_LOG_INFO("SyncImpl: job %d is finished by others (wait)\n", job->id);
					break;
				}
				GK_LOG_INFO("SyncImpl: wait (sleep)...\n");
				SleepConditionVariableSRW(&cond_work, &lock_work, INFINITE, 0);
				GK_LOG_INFO("SyncImpl: wake\n");
			}
			ReleaseSRWLockExclusive(&lock_work);
		}
	}
}
}

#endif
}

namespace gk {

SyncPool::SyncPool()
{
	using details::SyncImpl;
	constexpr ptrdiff_t to = GK_ALIGNOF(SyncImpl);
	static_assert(to + sizeof(SyncImpl) <= sizeof(storage), "");
	impl = nullptr;
	size_t base = reinterpret_cast<size_t>(static_cast<void*>(storage));
	SyncImpl* ptr = reinterpret_cast<SyncImpl*>((base + to - 1) & -to);
	new (ptr) SyncImpl;
	impl = ptr;
}

SyncPool::~SyncPool()
{
	if (impl)
		impl->~SyncImpl();
	impl = nullptr;
}

int SyncPool::set(int size)
{
	return impl->set(size);
}

int SyncPool::get()
{
	return impl->set(INT_MIN);
}

void SyncPool::submit(RefPtr<SyncJob> const& job)
{
	if (!job)
		return;
	return impl->submit(job);
}
}
