#include "pch.h"
#include "CoreGlobal.h"
#include "ThreadManager.h"
#include "Memory.h"
#include "DeadLockProfiler.h"
#include "SocketUtils.h"
#include "SendBuffer.h"
#include "GlobalQueue.h"
#include "JobTimer.h"
#include "DBConnectionPool.h"
#include "Logger.h"

ThreadManager*				GThreadManager = nullptr;
Memory*						GMemory = nullptr;
SendBufferManager*			GSendBufferManager = nullptr;
GlobalQueue*				GGlobalQueue = nullptr;
JobTimer*					GJobTimer = nullptr;	
DeadLockProfiler*			GDeadLockProfiler = nullptr;
DBConnectionPool*			GDBConnectionPool = nullptr;
Logger*						GLogger = nullptr;

// CoreGlobal에서 매니저끼리 생성, 소멸 순서를 맞춰줘야 할 수도 있기에
class CoreGlobal
{
public:
	CoreGlobal()
	{
		GThreadManager = new ThreadManager();
		GMemory = new Memory();
		GSendBufferManager = new SendBufferManager();
		GGlobalQueue = new GlobalQueue();
		GJobTimer = new JobTimer();
		GDeadLockProfiler = new DeadLockProfiler();
		GDBConnectionPool = new DBConnectionPool();
		GLogger = new Logger();
		SocketUtils::Init();
	}
	~CoreGlobal()
	{
		delete GThreadManager;
		delete GMemory;
		delete GSendBufferManager;
		delete GGlobalQueue;
		delete GJobTimer;
		delete GDeadLockProfiler;
		delete GDBConnectionPool;
		delete GLogger;
		SocketUtils::Clear();
	}
}GCoreGlobal;