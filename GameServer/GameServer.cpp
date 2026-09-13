#include "pch.h"
#include "ThreadManager.h"
#include "Service.h"
#include "Session.h"
#include "GameSession.h"
#include "GameSessionManager.h"
#include "ClientPacketHandler.h"
#include <tchar.h>
#include "Protocol.pb.h"
#include "Job.h"
#include "MatchmakingManager.h"
#include "Player.h"

enum
{
	WORKER_TICK = 64
};

void DoWorkerJob(ServerServiceRef& service)
{
	while (true)
	{
		LEndTickCount = ::GetTickCount64() + WORKER_TICK;

		// 네트워크 입출력 처리 -> 인게임 로직까지 (패킷 핸들러에 의해)
		service->Dispatch(10);

		// 예약된 일감 처리
		ThreadManager::DistributeReservedJobs();

		// 글로벌 큐
		ThreadManager::DoGlobalQueueWork();
	}
}

int main()
{
	GLogger->Init(LogOutput::Console, LogLevel::Log);

	InitMatchmakingManager();

	ClientPacketHandler::Init();

	ServerServiceRef service = MakeShared<ServerService>(
		NetAddress(L"127.0.0.1", 7777),
		MakeShared<IocpCore>(),
		MakeShared<GameSession>, // TODO : SessionManager 등
		100);

	ASSERT_CRASH(service->Start());

	// 워커 스레드 수를 서버의 논리 코어 수에 맞춘다.
	// hardware_concurrency()가 0을 돌려줄 수도 있어(가상화 등) 최소 2는 보장.
	int32 workerThreadCount = static_cast<int32>(std::thread::hardware_concurrency());
	if (workerThreadCount < 2)
		workerThreadCount = 2;

	LOG_INFO(L"워커 스레드 수 : %d", workerThreadCount - 1);

	// 메인 스레드도 아래에서 DoWorkerJob을 직접 도니까, 그만큼 하나 빼고 Launch.
	for (int32 i = 0; i < workerThreadCount - 1; i++)
	{
		GThreadManager->Launch([&service]()
			{
				DoWorkerJob(service);
			});
	}

	DoWorkerJob(service);

	GThreadManager->Join();
}