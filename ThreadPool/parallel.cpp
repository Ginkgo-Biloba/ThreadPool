#include "parallel.hpp"
#include <algorithm>

namespace gk {

#if defined _WIN32

GK_ALWAYS_INLINE void dolock(SRWLOCK* lock)
{
	AcquireSRWLockExclusive(lock);
}

GK_ALWAYS_INLINE void unlock(SRWLOCK* lock)
{
	ReleaseSRWLockExclusive(lock);
}

GK_ALWAYS_INLINE void cond_wait(CONDITION_VARIABLE* cond, SRWLOCK* lock)
{
	SleepConditionVariableSRW(cond, lock, INFINITE, 0);
}

GK_ALWAYS_INLINE void cond_signal(CONDITION_VARIABLE* cond)
{
	WakeConditionVariable(cond);
}

GK_ALWAYS_INLINE void cond_broadcast(CONDITION_VARIABLE* cond)
{
	WakeAllConditionVariable(cond);
}

GK_ALWAYS_INLINE void waitOnAddress(
	uint32_t* address, uint32_t* sleeping, uint32_t desired)
{
	if (atomic_load(address) != desired) {
		atomic_fetch_add(sleeping, 1);
		GK_ASSERT(NT_SUCCESS(
			NtWaitForKeyedEvent(GlobalKeyedEventHandle(), address, FALSE, NULL)));
	}
}

GK_ALWAYS_INLINE void wakeByAddress(uint32_t* address, uint32_t* sleeping)
{
	uint32_t nsleep = atomic_exchange(sleeping, 0);
	while (nsleep--) {
		GK_ASSERT(NT_SUCCESS(
			NtReleaseKeyedEvent(GlobalKeyedEventHandle(), address, FALSE, NULL)));
	}
}

#elif defined __linux__

GK_ALWAYS_INLINE void dolock(pthread_mutex_t* lock)
{
	GK_ASSERT(!pthread_mutex_lock(lock));
}

GK_ALWAYS_INLINE void unlock(pthread_mutex_t* lock)
{
	GK_ASSERT(!pthread_mutex_unlock(lock));
}

GK_ALWAYS_INLINE void cond_wait(pthread_cond_t* cond, pthread_mutex_t* lock)
{
	GK_ASSERT(!pthread_cond_wait(cond, lock));
}

GK_ALWAYS_INLINE void cond_signal(pthread_cond_t* cond)
{
	GK_ASSERT(!pthread_cond_signal(cond));
}

GK_ALWAYS_INLINE void cond_broadcast(pthread_cond_t* cond)
{
	GK_ASSERT(!pthread_cond_broadcast(cond));
}

GK_ALWAYS_INLINE void waitOnAddress(
	uint32_t* address, uint32_t* /*sleeping*/, uint32_t desired)
{
	while (true) {
		uint32_t value = atomic_load(address);
		if (value == desired)
			return;
		sysfutex(address, FUTEX_WAIT_PRIVATE, value, NULL, NULL, 0);
	}
}

GK_ALWAYS_INLINE void wakeByAddress(uint32_t* address, uint32_t* /*sleeping*/)
{
	sysfutex(address, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}
#endif

AsyncJob::AsyncJob()
	: priority(0), working(0), sleeping(0)
{
}

AsyncJob::~AsyncJob() { GK_ASSERT(!working && !sleeping); }

void AsyncJob::wait()
{
	waitOnAddress(&working, &sleeping, 0);
}

AsyncPool::AsyncPool()
	: num_thread(0), current_id(0), working(0), sleeping(0)
{
	for (uint32_t i = 0; i < MAX_THREAD; ++i) {
		workers[i].index = 0;
		workers[i].stop = 0;
		workers[i].pool = this;
		workers[i].thread = 0;
	}
#if defined _WIN32
	pool_lock = SRWLOCK_INIT;
	work_lock = SRWLOCK_INIT;
	work_cond = CONDITION_VARIABLE_INIT;
#elif defined __linux__
	pthread_mutex_init(&pool_lock, NULL);
	pthread_mutex_init(&work_lock, NULL);
	pthread_cond_init(&work_cond, NULL);
#endif
}

AsyncPool::~AsyncPool()
{
	waitAllDone();
	setNumThread(0);
	// Discard unfinished jobs
	waitlist.clear();
#if defined _WIN32
#elif defined __linux__
	pthread_mutex_destroy(&pool_lock);
	pthread_mutex_destroy(&work_lock);
	pthread_cond_destroy(&work_cond);
#endif
}

void AsyncPool::setNumThread(uint32_t n)
{
	n = min(n, static_cast<uint32_t>(MAX_THREAD));
	dolock(&pool_lock);
	if (n < num_thread) {
		dolock(&work_lock);
		for (uint32_t i = n; i < num_thread; ++i)
			workers[i].stop = 1;
		unlock(&work_lock);
		cond_broadcast(&work_cond);
		for (uint32_t i = n; i < num_thread; ++i) {
#if defined _WIN32
			WaitForSingleObject(reinterpret_cast<HANDLE>(workers[i].thread), INFINITE);
#elif defined __linux__
			pthread_join(workers[i].thread, NULL);
#endif
			workers[i].thread = INT_MAX;
		}
	}
	if (n > num_thread) {
		for (uint32_t i = num_thread; i < n; ++i) {
			workers[i].stop = 0;
#if defined _WIN32
			workers[i].thread = _beginthreadex(
				NULL, 0, trdRoutine, workers + i, 0, &(workers[i].win32_id));
			if (workers[i].thread == 0
				|| workers[i].thread == static_cast<uintptr_t>(-1)) {
				DWORD err = GetLastError();
				GK_LOG_ERROR("worker %d can not create thread, "
										 "handle = %llx, err = %u, errno %s\n",
					i, workers[i].thread, static_cast<unsigned>(err), strerror(errno));
			}
#elif defined __linux__
			char info[16];
			pthread_create(&(workers[i].thread), NULL, trdRoutine, workers + i);
			snprintf(info, sizeof(info), "ATrd%u", i);
			pthread_setname_np(workers[i].thread, info);
#endif
		}
	}
	num_thread = n;
	unlock(&pool_lock);
}

void AsyncPool::submit(RefPtr<AsyncJob> job)
{
	uint32_t ntrd = 0;
	dolock(&pool_lock);
	ntrd = num_thread;
	unlock(&pool_lock);
	if (ntrd < 1) {
		job->call();
		return;
	}

	atomic_fetch_add(&working, 1);
	atomic_fetch_add(&(job->working), 1);
	dolock(&work_lock);
	uint32_t id = current_id++;
	if (job->priority) {
		// Cutting in queue by up to randomly 8 to 15
		uint32_t jump = static_cast<uint32_t>(rand() & 7) + 8;
		id -= min(min(job->priority, jump), id);
	}
	waitlist.push_back(IdJob {id, job});
	push_heap(waitlist.begin(), waitlist.end());
	unlock(&work_lock);
	cond_signal(&work_cond);
}

void AsyncPool::waitAllDone()
{
	waitOnAddress(&working, &sleeping, 0);
}

#if defined _WIN32
unsigned AsyncPool::trdRoutine(void* void_args)
#elif defined __linux__
void* AsyncPool::trdRoutine(void* void_args)
#endif
{
	auto wk = reinterpret_cast<Worker*>(void_args);
	auto pool = wk->pool;
	while (true) {
		RefPtr<AsyncJob> job;
		dolock(&(pool->work_lock));
		while (!(wk->stop) && pool->waitlist.empty())
			cond_wait(&(pool->work_cond), &(pool->work_lock));
		if (!(wk->stop)) {
			std::pop_heap(pool->waitlist.begin(), pool->waitlist.end());
			job = pool->waitlist.back().job;
			pool->waitlist.pop_back();
		}
		unlock(&(pool->work_lock));
		if (!job)
			break;
		job->call();
		// Job has been completed, notify sleeping threads
		if (atomic_fetch_add(&(job->working), -1) == 1) {
			wakeByAddress(&(job->working), &(job->sleeping));
		}
		// All jobs in queue are completed, notify the main thread
		if (atomic_fetch_add(&(pool->working), -1) == 1)
			wakeByAddress(&(pool->working), &(pool->sleeping));
	}
#if defined _WIN32
	return wk->index;
#elif defined __linux__
	return void_args;
#endif
}

////////////////////////////////////////////////////////////

SyncPool::JobRef::JobRef(SyncJob& sjob, uint32_t ntrd)
	: job(&sjob)
	, nstripe(ntrd)
	, maxcall(min(job->maxcall, sjob.allend - sjob.allstart))
	, allstart(sjob.allstart)
	, allend(sjob.allend)
	, index(sjob.allstart)
	, working(0)
	, sleeping(0)
{
}

SyncPool::JobRef::~JobRef() { GK_ASSERT(!job || (!working && !sleeping)); }

SyncPool::SyncPool()
	: num_worker(0)
{
	for (uint32_t i = 0; i < MAX_THREAD; ++i) {
		workers[i].index = 0;
		workers[i].stop = 0;
		workers[i].pool = this;
		workers[i].thread = 0;
#if defined _WIN32
		workers[i].lock = SRWLOCK_INIT;
		workers[i].cond = CONDITION_VARIABLE_INIT;
#elif defined __linux__
		pthread_cond_init(&(workers[i].cond), NULL);
		pthread_mutex_init(&(workers[i].lock), NULL);
#endif
	}
#if defined _WIN32
	pool_lock = SRWLOCK_INIT;
	pool_cond = CONDITION_VARIABLE_INIT;
#elif defined __linux__
	pthread_mutex_init(&pool_lock, NULL);
	pthread_cond_init(&pool_cond, NULL);
#endif
}

SyncPool::~SyncPool()
{
	setNumThread(0);
#if defined _WIN32
#elif defined __linux__
	for (uint32_t i = 0; i < MAX_THREAD; ++i) {
		pthread_cond_destroy(&(workers[i].cond));
		pthread_mutex_destroy(&(workers[i].lock));
	}
	pthread_cond_destroy(&pool_cond);
	pthread_mutex_destroy(&pool_lock);
#endif
}

void SyncPool::setNumThread(uint32_t n)
{
	// if n == 1 or 0, only the main thread do jobs
	n = min(n, static_cast<uint32_t>(MAX_THREAD));
	n = max(n, 1u) - 1u;
	dolock(&pool_lock);
	for (uint32_t i = n; i < num_worker; ++i) {
		dolock(&(workers[i].lock));
		workers[i].stop = 1;
		workers[i].ref = nullptr;
		unlock(&(workers[i].lock));
		cond_signal(&(workers[i].cond));
#if defined _WIN32
		WaitForSingleObject(reinterpret_cast<HANDLE>(workers[i].thread), INFINITE);
#elif defined __linux__
		pthread_join(workers[i].thread, NULL);
#endif
		workers[i].thread = 0;
	}
	for (uint32_t i = num_worker; i < n; ++i) {
		workers[i].stop = 0;
#if defined _WIN32
		workers[i].thread = _beginthreadex(
			NULL, 0, trdRoutine, workers + i, 0, &(workers[i].win32_id));
		if (workers[i].thread == 0
			|| workers[i].thread == static_cast<uintptr_t>(-1)) {
			DWORD err = GetLastError();
			GK_LOG_ERROR("worker %d can not create thread, "
									 "handle = %zx, err = %u, errno %s\n",
				i, workers[i].thread, static_cast<unsigned>(err), strerror(errno));
		}
#elif defined __linux__
		char info[16];
		pthread_create(&(workers[i].thread), NULL, trdRoutine, workers + i);
		snprintf(info, sizeof(info), "ATrd%u", i);
		pthread_setname_np(workers[i].thread, info);
#endif
	}
	num_worker = n;
	unlock(&pool_lock);
}

void SyncPool::submit(SyncJob& job)
{
	if (job.allstart >= job.allend)
		return;

	uint32_t ntrd = job.allend - job.allstart;
	if (job.maxcall)
		ntrd = min(ntrd, job.maxcall);
	dolock(&pool_lock);
	ntrd = min(ntrd, num_worker + 1);
	if (ntrd < 2) {
		unlock(&pool_lock);
		job.call(job.allstart, job.allend);
		return;
	}
	// The main thread also needs to work
	uint32_t subtrd = ntrd - 1;
	RefPtr<JobRef> ref = new JobRef(job, ntrd);
	for (uint32_t i = 0; i < subtrd; ++i) {
		dolock(&(workers[i].lock));
		workers[i].ref = ref;
		unlock(&(workers[i].lock));
		cond_signal(&(workers[i].cond));
	}
	unlock(&pool_lock);

	while (true) {
		uint32_t start = atomic_load(&(ref->index));
		if (start >= ref->allend)
			break;
		uint32_t stripe = (ref->allend - start) / ref->nstripe / 4u;
		if (ref->maxcall)
			stripe = (ref->allend - ref->allstart + ref->maxcall - 1) / ref->maxcall;
		stripe = max(stripe, 1u);
		start = atomic_fetch_add(&(ref->index), stripe);
		if (start >= ref->allend)
			break;
		ref->job->call(start, min(start + stripe, ref->allend));
	}

	// Waiting for job completed
	waitOnAddress(&(ref->working), &(ref->sleeping), 0);
}

#if defined _WIN32
unsigned SyncPool::trdRoutine(void* void_args)
#elif defined __linux__
void* SyncPool::trdRoutine(void* void_args)
#endif
{
	auto wk = reinterpret_cast<Worker*>(void_args);
	while (true) {
		RefPtr<JobRef> ref;
		dolock(&(wk->lock));
		while (!(wk->stop) && !(wk->ref))
			cond_wait(&(wk->cond), &(wk->lock));
		if (!(wk->stop))
			ref.swap(wk->ref);
		unlock(&(wk->lock));
		if (!ref)
			break;
		atomic_fetch_add(&(ref->working), 1);
		while (true) {
			uint32_t start = atomic_load(&(ref->index));
			if (start >= ref->allend)
				break;
			uint32_t stripe = (ref->allend - start) / ref->nstripe / 4u;
			if (ref->maxcall)
				stripe = (ref->allend - ref->allstart + ref->maxcall - 1) / ref->maxcall;
			stripe = max(stripe, 1u);
			start = atomic_fetch_add(&(ref->index), stripe);
			if (start >= ref->allend)
				break;
			ref->job->call(start, min(start + stripe, ref->allend));
		}
		// Job completed, notify the main thread
		if (atomic_fetch_add(&(ref->working), -1) == 1)
			wakeByAddress(&(ref->working), &(ref->sleeping));
	}
#if defined _WIN32
	return wk->index;
#elif defined __linux__
	return void_args;
#endif
}

}
