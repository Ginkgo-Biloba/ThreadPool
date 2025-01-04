#include "parallel.hpp"
#include <algorithm>

namespace gk {

#if defined _WIN32

void JobEvent::wait(uint32_t desired)
{
	/* Must do comparation and addition in one atomic operation
	  Otherwise this thread maybe miss the wake signal and sleep forever */
	uint32_t* address = &value;
	uint32_t submit, val, old = atomic_load(address);
	do {
		submit = old & mask_value;
		if (submit == desired)
			break;
		val = old + one_sleep;
	} while (!atomic_compare_exchange(address, &old, val));
	if (submit != desired)
		GK_ASSERT(NT_SUCCESS(
			NtWaitForKeyedEvent(GlobalKeyedEventHandle(), address, FALSE, NULL)));
}

void JobEvent::wake()
{
	uint32_t* address = &value;
	uint32_t nsleep, val, old = atomic_load(address);
	do {
		nsleep = old & mask_sleep;
		if (!nsleep)
			break;
		val = old & ~mask_sleep;
	} while (!atomic_compare_exchange(address, &old, val));
	for (; nsleep; nsleep -= one_sleep) {
		GK_ASSERT(NT_SUCCESS(
			NtReleaseKeyedEvent(GlobalKeyedEventHandle(), address, FALSE, NULL)));
	}
}

#endif

AsyncJob::AsyncJob()
	: priority(0) { }

AsyncJob::~AsyncJob() { }

void AsyncJob::wait() { event.wait(0); }

AsyncPool::AsyncPool()
	: num_thread(0), current_id(0)
{
	for (size_t i = sizeof(workers) / sizeof(workers[0]); i--;) {
		workers[i].index = 0;
		workers[i].stop = 0;
		workers[i].pool = this;
		workers[i].thread = 0;
	}
}

AsyncPool::~AsyncPool()
{
	setNumThread(0);
	// Discard unfinished jobs
	waitlist.clear();
}

void AsyncPool::setNumThread(uint32_t n)
{
	n = min(n, static_cast<uint32_t>(MAX_THREAD));
	pool_lock.acquire();
	if (n < num_thread) {
		work_lock.acquire();
		for (uint32_t i = n; i < num_thread; ++i)
			workers[i].stop = 1;
		work_lock.release();
		work_cond.broadcast();
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
	pool_lock.release();
}

void AsyncPool::submit(std::shared_ptr<AsyncJob> job)
{
	uint32_t ntrd = 0;
	pool_lock.acquire();
	ntrd = num_thread;
	pool_lock.release();
	if (ntrd < 1) {
		job->call();
		return;
	}

	event.enter();
	job->event.enter();
	work_lock.acquire();
	uint32_t id = current_id++;
	if (job->priority) {
		// Cutting in queue by up to randomly 8 to 15
		uint32_t jump = static_cast<uint32_t>(rand() & 7) + 8;
		id -= min(min(job->priority, jump), id);
	}
	waitlist.push_back(IdJob {id, job});
	push_heap(waitlist.begin(), waitlist.end());
	work_lock.release();
	work_cond.signal();
}

void AsyncPool::wait() { event.wait(0); }

#if defined _WIN32
unsigned AsyncPool::trdRoutine(void* void_args)
#elif defined __linux__
void* AsyncPool::trdRoutine(void* void_args)
#endif
{
	auto wk = reinterpret_cast<Worker*>(void_args);
	auto pool = wk->pool;
	while (true) {
		std::shared_ptr<AsyncJob> job;
		pool->work_lock.acquire();
		while (!(wk->stop) && pool->waitlist.empty())
			pool->work_cond.wait(pool->work_lock);
		if (!(wk->stop)) {
			std::pop_heap(pool->waitlist.begin(), pool->waitlist.end());
			job = pool->waitlist.back().job;
			pool->waitlist.pop_back();
		}
		pool->work_lock.release();
		if (!job)
			break;
		job->call();
		// Job has been completed, notify sleeping threads
		if (job->event.leave() == 1)
			job->event.wake();
		// All jobs in queue are completed, notify the main thread
		if (pool->event.leave() == 1)
			pool->event.wake();
	}
#if defined _WIN32
	return wk->index;
#elif defined __linux__
	return void_args;
#endif
}

////////////////////////////////////////////////////////////

SyncPool::JobRef::JobRef(SyncJob& sjob, uint32_t ntrd)
{
	job = &sjob;
	nstripe = ntrd;
	maxcall = min(job->maxcall, sjob.allend - sjob.allstart);
	allstart = index = sjob.allstart;
	allend = sjob.allend;
}

SyncPool::JobRef::~JobRef()
{
	GK_ASSERT(!job || index >= allend);
}

void SyncPool::JobRef::execute(uint32_t tid)
{
	while (true) {
		uint32_t start = atomic_load(&index);
		if (start >= allend)
			break;
		uint32_t stripe = (allend - start) / nstripe / 4u;
		if (maxcall)
			stripe = (allend - allstart + maxcall - 1) / maxcall;
		stripe = max(stripe, 1u);
		start = atomic_fetch_add(&index, stripe);
		if (start >= allend)
			break;
		job->call(tid, start, min(start + stripe, allend));
	}
}

SyncPool::SyncPool()
	: num_worker(0)
{
	for (size_t i = sizeof(workers) / sizeof(workers[0]); i--;) {
		workers[i].index = 0;
		workers[i].stop = 0;
		workers[i].pool = this;
		workers[i].thread = 0;
	}
}

SyncPool::~SyncPool()
{
	// We will wait until all sub-threads stop
	// So no JobRef object is active
	setNumThread(0);
}

void SyncPool::setNumThread(uint32_t n)
{
	// if n == 1 or 0, only the main thread do jobs
	n = min(n, static_cast<uint32_t>(MAX_THREAD));
	n = max(n, 1u) - 1u;
	pool_lock.acquire();
	for (uint32_t i = n; i < num_worker; ++i) {
		workers[i].lock.acquire();
		workers[i].stop = 1;
		workers[i].ref = nullptr;
		workers[i].lock.release();
		workers[i].cond.signal();
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
	pool_lock.release();
}

void SyncPool::submit(SyncJob& job)
{
	if (job.allstart >= job.allend)
		return;

	uint32_t ntrd = job.allend - job.allstart;
	if (job.maxcall)
		ntrd = min(ntrd, job.maxcall);
	pool_lock.acquire();
	ntrd = min(ntrd, num_worker + 1);
	if (ntrd < 2) {
		pool_lock.release();
		job.call(0, job.allstart, job.allend);
		return;
	}

	// The main thread also needs to work
	uint32_t subtrd = ntrd - 1;
	auto ref = std::make_shared<JobRef>(job, ntrd);
	for (uint32_t i = 0; i < subtrd; ++i) {
		workers[i].lock.acquire();
		workers[i].ref = ref;
		workers[i].lock.release();
		workers[i].cond.signal();
	}
	pool_lock.release();
	ref->execute(subtrd);
	// Waiting for job completed
	ref->event.wait(0);
}

#if defined _WIN32
unsigned SyncPool::trdRoutine(void* void_args)
#elif defined __linux__
void* SyncPool::trdRoutine(void* void_args)
#endif
{
	auto wk = reinterpret_cast<Worker*>(void_args);
	while (true) {
		std::shared_ptr<JobRef> ref;
		wk->lock.acquire();
		while (!(wk->stop) && !(wk->ref))
			wk->cond.wait(wk->lock);
		if (!(wk->stop))
			ref.swap(wk->ref);
		wk->lock.release();
		if (!ref)
			break;
		ref->event.enter();
		ref->execute(wk->index);
		// Job completed, notify the main thread
		if (ref->event.leave() == 1)
			ref->event.wake();
	}
#if defined _WIN32
	return wk->index;
#elif defined __linux__
	return void_args;
#endif
}

}
