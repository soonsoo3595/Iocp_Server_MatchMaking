#pragma once

// 전역으로 사용하는 변수들
extern class ThreadManager*			GThreadManager;
extern class SendBufferManager*		GSendBufferManager;
extern class GlobalQueue*			GGlobalQueue;
extern class JobTimer*				GJobTimer;

extern class DeadLockProfiler*		GDeadLockProfiler;
extern class DBConnectionPool*		GDBConnectionPool;
extern class Logger*				GLogger;
