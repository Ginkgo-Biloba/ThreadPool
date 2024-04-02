#include "atomic.hpp"
#include <cstdarg>

namespace gk {

void logError(char const* file, char const* func, int line, char const* fmt, ...)
{
	char buf[1024];
	va_list vl;
	va_start(vl, fmt);
	size_t i = snprintf(buf, sizeof(buf) - 16,
		"ERROR on file %s, func %s, line %d: ", file, func, line);
	vsnprintf(buf + i, sizeof(buf) - i, fmt, vl);
	va_end(vl);
	fputs(buf, stderr);
	fflush(stderr);
	GK_TRAP;
}

#ifdef __linux__

enum {
	kOncePending = 1 << 0, // 有线程正在初始化
	kOnceWaiting = 1 << 8, // 有线程在等待初始化完成
	kOnceGuard = 1 << 16,  // 初始化已经完成
};

// __cxa_guard_acquire
int InitOnceBeginInitialize(LPINIT_ONCE lpInitOnce, uint32_t, int*, void**)
{
	uint32_t* futex = lpInitOnce->value;
	for (;;) {
		uint32_t old = 0;
		// 成功从未初始化设置到开始初始化状态
		if (atomic_compare_exchange(futex, &old, kOncePending))
			return 1;
		// 完成初始化
		if (old == kOnceGuard)
			return 0;
		if (old == kOncePending) {
			// 告诉初始化线程要唤醒自己
			int val = old | kOnceWaiting;
			if (!atomic_compare_exchange(futex, &old, val)) {
				// 可能初始化完了，从 pending 变成 guard
				if (old == kOnceGuard)
					return 0;
				// 有线程初始化失败，需要重新初始化
				// 目前不支持失败，因此不会到这个状态
				if (old == 0)
					continue;
			}
			old = val;
		}
		sysfutex(futex, FUTEX_WAIT_PRIVATE, old, NULL, NULL, 0);
	}
	GK_UNREACHABLE;
}

// __cxa_guard_release
int InitOnceComplete(LPINIT_ONCE lpInitOnce, uint32_t, void*)
{
	uint32_t* futex = lpInitOnce->value;
	uint32_t old = atomic_exchange(futex, kOnceGuard);
	if (old & kOnceWaiting)
		sysfutex(futex, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
	return 0;
}

#	if WINE_LOCK

/* Futex-based SRW lock implementation
 *
 * Since we can rely on the kernel to release all threads and don't need to
 * worry about NtReleaseKeyedEvent(), we can simplify the layout a bit. The
 * layout looks like this:
 *
 *    31 - Exclusive lock bit, set if the resource is owned exclusively.
 * 30-16 - Number of exclusive waiters. Unlike the fallback implementation,
 *         this does not include the thread owning the lock, or shared threads
 *         waiting on the lock.
 *    15 - Does this lock have any shared waiters? We use this as an
 *         optimization to avoid unnecessary FUTEX_WAKE_BITSET calls when
 *         releasing an exclusive lock.
 *  14-0 - Number of shared owners. Unlike the fallback implementation, this
 *         does not include the number of shared threads waiting on the lock.
 *         Thus the state [1, x, >=1] will never occur.
 * 
 * https://github.com/wine-mirror/wine/blob/87164ee3332c95f0cd9a1f3e4598056689cdfadc/dlls/ntdll/unix/sync.c
 * 
 * 完全用 futex 做同步，wine 里面的实现
 * 这里的读写等待只有计数，因此锁不公平，无法做到 读-写-读-写 的 FIFO 顺序
 * 
 * gcc 位域操作优化不好，因此直接用位运算处理
 * https://godbolt.org/z/zhPWY4rE8
 */

enum {
	kLockExclusiveFutexBitset = 1 << 16,
	kLockSharedFutexBitset = 1 << 0,
	kLockExclusiveLockedMask = 0x80000000,
	kLockExclusiveWaiterMask = 0x7fff0000,
	kLockExclusiveWaiterInc = 0x00010000,
	kLockSharedWaiterMask = 0x00008000,
	kLockSharedOwnedMask = 0x00007fff,
	kLockSharedOwnedInc = 0x00000001,
};

#		if 0

int TryAcquireSRWLockExclusive(PSRWLOCK lock)
{
	int ret = 0;
	uint32_t* futex = lock->value;
	SRWLOCK val, old = *lock;
	do {
		val = old;
		if (!old.bs.ex_lock && !old.bs.rd_hold) {
			// 没有写锁定也没有读锁定，自由状态，可以加写锁
			val.bs.ex_lock = 1;
			ret = 1;
		} else
			ret = 0;
	} while (!atomic_compare_exchange(futex, old.value, val.value[0]));
	return ret;
}

int TryAcquireSRWLockShared(PSRWLOCK lock)
{
	int ret = 0;
	uint32_t* futex = lock->value;
	SRWLOCK val, old = *lock;
	do {
		val = old;
		if (!old.bs.ex_lock && !old.bs.ex_wait) {
			// 没有写锁定也没有线程在等待写锁，可以加读锁
			GK_ASSERT(old.bs.rd_hold < INT16_MAX);
			val.bs.rd_hold = old.bs.rd_hold + 1;
			ret = 1;
		} else
			ret = 0;
	} while (!atomic_compare_exchange(futex, old.value, val.value[0]));
	return ret;
}

void AcquireSRWLockExclusive(PSRWLOCK lock)
{
	uint32_t* futex = lock->value;
	SRWLOCK val, old = *lock;
	bool wait = false;
	do {
		val = old;
		GK_ASSERT(old.bs.ex_wait < INT16_MAX);
		// 添加写锁等待计数，防止读锁轮流持续，写锁一直拿不到
		val.bs.ex_wait = old.bs.ex_wait + 1;
	} while (!atomic_compare_exchange(futex, old.value, val.value[0]));
	for (;;) {
		do {
			val = old;
			if (!old.bs.ex_lock && !old.bs.rd_hold) {
				// 锁住，并且把自己前面加的 waiter 计数减掉
				val.bs.ex_lock = 1;
				val.bs.ex_wait = old.bs.ex_wait - 1;
				wait = false;
			} else
				wait = true;
		} while (!atomic_compare_exchange(futex, old.value, val.value[0]));
		if (!wait)
			return;
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAIT_BITSET,
			old.value[0], NULL, NULL, kLockExclusiveFutexBitset);
	}
	GK_UNREACHABLE;
}

void AcquireSRWLockShared(PSRWLOCK lock)
{
	uint32_t* futex = lock->value;
	SRWLOCK val, old = *lock;
	bool wait = false;
	for (;;) {
		do {
			val = old;
			if (!old.bs.ex_lock && !old.bs.ex_wait) {
				GK_ASSERT(old.bs.rd_hold < INT16_MAX);
				val.bs.rd_hold = old.bs.rd_hold + 1;
				wait = false;
			} else {
				val.bs.rd_wait = 1;
				wait = true;
			}
		} while (!atomic_compare_exchange(futex, old.value, val.value[0]));
		if (!wait)
			return;
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAIT_BITSET,
			old.value[0], NULL, NULL, kLockSharedFutexBitset);
	}
	GK_UNREACHABLE;
}

void ReleaseSRWLockExclusive(PSRWLOCK lock)
{
	uint32_t* futex = lock->value;
	SRWLOCK val, old = *lock;
	do {
		if (!old.bs.ex_lock) {
			GK_LOG_ERROR("lock %p (%llx) is not owned exclusive\n",
				static_cast<void*>(lock), static_cast<long long>(lock->padding));
		}
		val = old;
		val.bs.ex_lock = 0;
		// 没有写锁在等待，就唤醒读锁
		// 这个锁不公平，有写锁在等，读锁就一直拿不到
		if (!old.bs.ex_wait)
			val.bs.rd_wait = 0;
	} while (!atomic_compare_exchange(futex, old.value, val.value[0]));
	if (old.bs.ex_wait) {
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAKE_BITSET,
			1, NULL, NULL, kLockExclusiveFutexBitset);
	} else if (old.bs.rd_wait) {
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAKE_BITSET,
			INT_MAX, NULL, NULL, kLockSharedFutexBitset);
	}
}

void ReleaseSRWLockShared(PSRWLOCK lock)
{
	uint32_t* futex = lock->value;
	SRWLOCK val, old = *lock;
	do {
		if (old.bs.ex_lock) {
			GK_LOG_ERROR("lock %p (%llx) is owned exclusive\n",
				static_cast<void*>(lock), static_cast<long long>(lock->padding));
		}
		if (!old.bs.rd_hold) {
			GK_LOG_ERROR("lock %p (%llx) is not owned shared\n",
				static_cast<void*>(lock), static_cast<long long>(lock->padding));
		}
		val = old;
		val.bs.rd_hold = old.bs.rd_hold - 1;
	} while (!atomic_compare_exchange(futex, old.value, val.value[0]));
	// 没有线程持有读锁，并且存在等待写锁的线程，尝试唤醒写锁等待者
	if (!val.bs.rd_hold && val.bs.ex_wait) {
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAKE_BITSET,
			1, NULL, NULL, kLockExclusiveFutexBitset);
	}
}

#		else

int TryAcquireSRWLockExclusive(PSRWLOCK lock)
{
	int ret = 0;
	uint32_t* futex = lock->value;
	uint32_t val, old = *futex;
	do {
		val = old;
		if (!(old & kLockExclusiveLockedMask) && !(old & kLockSharedOwnedMask)) {
			val |= kLockExclusiveLockedMask;
			ret = 1;
		} else
			ret = 0;
	} while (!atomic_compare_exchange(futex, &old, val));
	return ret;
}

int TryAcquireSRWLockShared(PSRWLOCK lock)
{
	int ret = 0;
	uint32_t* futex = lock->value;
	uint32_t val, old = *futex;
	do {
		val = old;
		if (!(old & kLockExclusiveLockedMask) && !(old & kLockExclusiveWaiterMask)) {
			GK_ASSERT((old & kLockSharedOwnedMask) < kLockSharedOwnedMask);
			val = old + kLockSharedOwnedInc;
			ret = 1;
		} else
			ret = 0;
	} while (!atomic_compare_exchange(futex, &old, val));
	return ret;
}

void AcquireSRWLockExclusive(PSRWLOCK lock)
{
	uint32_t* futex = lock->value;
	uint32_t val, old = *futex;
	bool wait = false;
	do {
		val = old;
		GK_ASSERT((old & kLockExclusiveWaiterMask) < kLockExclusiveWaiterMask);
		val = old + kLockExclusiveWaiterInc;
	} while (!atomic_compare_exchange(futex, &old, val));
	for (;;) {
		do {
			val = old;
			if (!(old & kLockExclusiveLockedMask) && !(old & kLockSharedOwnedMask)) {
				val = (old | kLockExclusiveLockedMask) - kLockExclusiveWaiterInc;
				wait = false;
			} else
				wait = true;
		} while (!atomic_compare_exchange(futex, &old, val));
		if (!wait)
			return;
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAIT_BITSET,
			old, NULL, NULL, kLockExclusiveFutexBitset);
	}
	GK_UNREACHABLE;
}

void AcquireSRWLockShared(PSRWLOCK lock)
{
	uint32_t* futex = lock->value;
	uint32_t val, old = *futex;
	bool wait = false;
	for (;;) {
		do {
			if (!(old & kLockExclusiveLockedMask) && !(old & kLockExclusiveWaiterMask)) {
				GK_ASSERT((old & kLockSharedOwnedMask) < kLockSharedOwnedMask);
				val = old + kLockSharedOwnedInc;
				wait = false;
			} else {
				val = old | kLockSharedWaiterMask;
				wait = true;
			}
		} while (!atomic_compare_exchange(futex, &old, val));
		if (!wait)
			return;
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAIT_BITSET,
			old, NULL, NULL, kLockSharedFutexBitset);
	}
	GK_UNREACHABLE;
}

void ReleaseSRWLockExclusive(PSRWLOCK lock)
{
	uint32_t* futex = lock->value;
	uint32_t val, old = *futex;
	do {
		if (!(old & kLockExclusiveLockedMask)) {
			GK_LOG_ERROR("lock %p (%llx) is not owned exclusive\n",
				static_cast<void*>(lock), static_cast<long long>(lock->padding));
		}
		val = old & ~kLockExclusiveLockedMask;
		if (!(old & kLockExclusiveWaiterMask))
			val &= ~kLockSharedWaiterMask;
	} while (!atomic_compare_exchange(futex, &old, val));
	if (old & kLockExclusiveWaiterMask) {
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAKE_BITSET,
			1, NULL, NULL, kLockExclusiveFutexBitset);
	} else if (old & kLockSharedWaiterMask) {
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAKE_BITSET,
			INT_MAX, NULL, NULL, kLockSharedFutexBitset);
	}
}

void ReleaseSRWLockShared(PSRWLOCK lock)
{
	uint32_t* futex = lock->value;
	uint32_t val, old = *futex;
	do {
		if (old & kLockExclusiveLockedMask) {
			GK_LOG_ERROR("lock %p (%llx) is owned exclusive\n",
				static_cast<void*>(lock), static_cast<long long>(lock->padding));
		}
		if (!(old & kLockSharedOwnedMask)) {
			GK_LOG_ERROR("lock %p (%llx) is not owned shared\n",
				static_cast<void*>(lock), static_cast<long long>(lock->padding));
		}
		val = old - kLockSharedOwnedInc;
	} while (!atomic_compare_exchange(futex, &old, val));
	if (!(val & kLockSharedOwnedMask) && (val & kLockExclusiveWaiterMask)) {
		sysfutex(futex, FUTEX_PRIVATE_FLAG | FUTEX_WAKE_BITSET,
			1, NULL, NULL, kLockExclusiveFutexBitset);
	}
}

#		endif

/* Futex-based condition variable implementation
 * https://github.com/wine-mirror/wine/blob/c577ce2671ba8b003dbbdb329ada56368a370778/dlls/ntdll/unix/sync.c
 */

int SleepConditionVariableSRW(PCONDITION_VARIABLE ConditionVariable,
	PSRWLOCK SRWLock, uint32_t dwMilliseconds, uint32_t Flags)
{
	timespec ts;
	ts.tv_sec = dwMilliseconds / 1000;
	ts.tv_nsec = (dwMilliseconds - ts.tv_sec * 1000) * 1000000;
	timespec* tp = dwMilliseconds == INFINITE ? NULL : &ts;
	uint32_t* futex = ConditionVariable->value;
	uint32_t old = *futex;
	if (Flags & CONDITION_VARIABLE_LOCKMODE_SHARED)
		ReleaseSRWLockShared(SRWLock);
	else
		ReleaseSRWLockExclusive(SRWLock);
	long ret = sysfutex(futex, FUTEX_WAIT_PRIVATE, old, tp, NULL, 0);
	if (Flags & CONDITION_VARIABLE_LOCKMODE_SHARED)
		AcquireSRWLockShared(SRWLock);
	else
		AcquireSRWLockExclusive(SRWLock);
	return !ret;
}

void WakeConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
	uint32_t* futex = ConditionVariable->value;
	// 如果等待的线程数等于 UINT_MAX 则 futex 会回环回去
	// 但是实际不会开这么多线程
	atomic_fetch_add(futex, 1);
	sysfutex(futex, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
}

void WakeAllConditionVariable(PCONDITION_VARIABLE ConditionVariable)
{
	uint32_t* futex = ConditionVariable->value;
	atomic_fetch_add(futex, 1);
	sysfutex(futex, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}

#	endif

#endif

} // namespace gk
