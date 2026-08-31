#include "pch.h"
#include "Lock.h"
#include "CoreTLS.h"
#include "DeadLockProfiler.h"

void Lock::WriteLock(const char* name)
{
#if _DEBUG
	GDeadLockProfiler->PushLock(name);
#endif

	// 동일한 스레드가 소유하고 있으면 중첩 횟수만 증가
	const uint32 lockThreadId = (_lockFlag.load() & WRITE_THREAD_MASK) >> 16;
	if (LThreadId == lockThreadId)
	{
		_writeCount++;
		return;
	}

	// 첫 번째 핵심 : 아무도 소유 및 공유하고 있지 않을 때(Empty Flag), 경합해서 소유권을 얻는다.
	const int64 beginTick = ::GetTickCount64();
	const uint32 desired = ((LThreadId << 16) & WRITE_THREAD_MASK);

	while (true)
	{
		for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; spinCount++)
		{
			uint32 expected = EMPTY_FLAG;
			if (_lockFlag.compare_exchange_strong(OUT expected, desired))
			{
				_writeCount++;
				return;
			}
		}

		if (::GetTickCount64() - beginTick >= ACQUIRE_TIMEOUT_TICK)
		{
			LOG_FATAL(L"Lock timeout. name=%hs, threadId=%d", name, LThreadId);
			CRASH("LOCK_TIMEOUT");
		}

		this_thread::yield();
	}
}

void Lock::WriteUnlock(const char* name)
{
#if _DEBUG
	GDeadLockProfiler->PopLock(name);
#endif

	// ReadLock 다 풀기 전에는 WriteUnlock 불가능.
	if (_writeCount == 0)
	{
		LOG_FATAL(L"WriteUnlock without matching WriteLock. name=%hs, threadId=%d", name, LThreadId);
		CRASH("MULTIPLE_UNLOCK");
	}

	if ((_lockFlag.load() & READ_COUNT_MASK) != 0)
	{
		LOG_FATAL(L"WriteUnlock called while ReadLock still held. name=%hs, threadId=%d", name, LThreadId);
		CRASH("INVALID_UNLOCK_ORDER");
	}

	const int32 lockCount = --_writeCount;
	if (lockCount == 0)
		_lockFlag.store(EMPTY_FLAG);
}

void Lock::ReadLock(const char* name)
{
#if _DEBUG
	GDeadLockProfiler->PushLock(name);
#endif
	// 동일한 쓰레드가 Write 락을 소유하고 있다면 Read 락을 잡는 것은 가능
	const uint32 lockThreadId = (_lockFlag.load() & WRITE_THREAD_MASK) >> 16;
	if (LThreadId == lockThreadId)
	{
		_lockFlag.fetch_add(1);
		return;
	}

	// 일반적 상황 : 아무도 소유(Write)하고 있지 않을 때 경합해서 공유 카운트를 올린다.
	const int64 beginTick = ::GetTickCount64();
	while (true)
	{
		for (uint32 spinCount = 0; spinCount < MAX_SPIN_COUNT; spinCount++)
		{
			uint32 expected = (_lockFlag.load() & READ_COUNT_MASK);
			if (_lockFlag.compare_exchange_strong(OUT expected, expected + 1))
				return;
		}

		if (::GetTickCount64() - beginTick >= ACQUIRE_TIMEOUT_TICK)
		{
			LOG_FATAL(L"Lock timeout. name=%hs, threadId=%d", name, LThreadId);
			CRASH("LOCK_TIMEOUT");
		}

		this_thread::yield();
	}
}

void Lock::ReadUnlock(const char* name)
{
#if _DEBUG
	GDeadLockProfiler->PopLock(name);
#endif
	// 그냥 Read 락 카운트를 줄이면 된다. 값이 0이었다면 문제니까 크래시
	if ((_lockFlag.fetch_sub(1) & READ_COUNT_MASK) == 0)
	{
		LOG_FATAL(L"ReadUnlock without matching ReadLock. name=%hs, threadId=%d", name, LThreadId);
		CRASH("MULTIPLE_UNLOCK");
	}
}
