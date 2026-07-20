#include "pch.h"
#include "GameSessionManager.h"
#include "GameSession.h"

GameSessionManager GSessionManager;

void GameSessionManager::Add(GameSessionRef session)
{
	WRITE_LOCK;
	_sessions.insert(session);
}

void GameSessionManager::Remove(GameSessionRef session)
{
	WRITE_LOCK;
	_sessions.erase(session);
}

// Broadcast가 일어날 때 sendBuffer를 한 번만 만들어줘서 다 걸어주면 SendQueue에 들어감
// 클라 접속수를 늘리고 실행 후 더미 클라이언트를 끄면 브로드캐스트하다가 터졌다
// 뮤텍스로 바꿔도 해결 안되더라
void GameSessionManager::Broadcast(SendBufferRef sendBuffer)
{
	WRITE_LOCK;

	// 이렇게 for문을 돌다가 _sessions에 변화가 생기면?? -> 메모리 오염
	// 연결을 끊으면 Session에서 WSASend를 하는 부분에 에러로 들어감
	// Disconnect -> 세션을 지워버림
	for (GameSessionRef session : _sessions)
	{
		session->Send(sendBuffer);
	}
}