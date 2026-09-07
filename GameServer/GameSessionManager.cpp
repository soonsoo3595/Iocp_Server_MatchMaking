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

// Send()는 소켓 I/O(WSASend)까지 걸릴 수 있는 무거운 작업이라, 락을 쥔 채로
// 세션 수만큼 반복하면 그동안 Add/Remove가 전부 막힌다. 동접이 많을 때는
// Lock의 10초 타임아웃(LOCK_TIMEOUT)에 걸려 크래시로 이어질 수 있음.
// 그래서 락은 세션 목록을 복사하는 짧은 순간만 잡고, 실제 전송은 락 밖에서 한다.
// 복사된 GameSessionRef가 각자 refcount를 하나씩 들고 있어서, 복사 이후에
// 원본 _sessions에서 세션이 제거되어도 이 함수가 끝날 때까지는 안전하게 살아있다.
// (단, 이 스냅샷 이후에 새로 접속/해제된 세션은 이번 브로드캐스트에 반영되지 않는다)
void GameSessionManager::Broadcast(SendBufferRef sendBuffer)
{
	Vector<GameSessionRef> snapshot;
	{
		WRITE_LOCK;
		snapshot.assign(_sessions.begin(), _sessions.end());
	}

	for (GameSessionRef& session : snapshot)
	{
		session->Send(sendBuffer);
	}+
}