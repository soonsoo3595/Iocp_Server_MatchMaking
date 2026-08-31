#pragma once
#include "Types.h"

/*----------------
    RW SpinLock
-----------------*/

/*--------------------------------------------
[WWWWWWWW][WWWWWWWW][RRRRRRRR][RRRRRRRR] -> 32비트
W : WriteFlag (Exclusive Lock Owner ThreadId) : 락을 획득하고 있는 스레드의 id
R : ReadFlag (Shared Lock Count)
---------------------------------------------*/

// W -> R (O)
// R -> W (X)
class Lock
{
    enum : uint32
    {
        ACQUIRE_TIMEOUT_TICK = 10000,       // 최대로 기다려줄 틱
        MAX_SPIN_COUNT = 5000,              // 스핀 카운트를 몇 번 돌 것인가
        WRITE_THREAD_MASK = 0xFFFF'0000,    // 비트 플래그 방식 : 상위 16비트 추출
        READ_COUNT_MASK = 0x0000'FFFF,      // 비트 플래그 방식 : 하위 16비트 추출
        EMPTY_FLAG = 0x0000'0000            // 초반 상태
    };

public:
    void WriteLock(const char* name);
    void WriteUnlock(const char* name);
    void ReadLock(const char* name);
    void ReadUnlock(const char* name);

private:
    Atomic<uint32> _lockFlag = EMPTY_FLAG;
    uint16 _writeCount = 0;     // 락을 잡은 애만 사용하기해 경합이 발생하지 않음
};

/*----------------
    LockGuards
-----------------*/

class ReadLockGuard
{
public:
    ReadLockGuard(Lock& lock, const char* name) : _lock(lock), _name(name) { _lock.ReadLock(name); }
    ~ReadLockGuard() { _lock.ReadUnlock(_name); }

private:
    Lock& _lock;
    const char* _name;
};

class WriteLockGuard
{
public:
    WriteLockGuard(Lock& lock, const char* name) : _lock(lock), _name(name) { _lock.WriteLock(name); }
    ~WriteLockGuard() { _lock.WriteUnlock(_name); }

private:
    Lock& _lock;
    const char* _name;
};