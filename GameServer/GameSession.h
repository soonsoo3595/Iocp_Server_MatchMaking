#pragma once
#include "Session.h"

class GameSession : public PacketSession
{
public:
	virtual void OnConnected() override;
	virtual void OnDisconnected() override;
	virtual void OnRecvPacket(BYTE* buffer, int32 len) override;
	virtual void OnSend(int32 len) override;

public:
	PlayerRef				_player;
	weak_ptr<class MatchAcceptSession>	_matchAcceptSession;	// 지금 응답을 기다리는 매치 수락 세션 (없으면 만료된 weak_ptr)
	weak_ptr<class ChampSelectSession>	_champSelectSession;	// 지금 참여 중인 챔피언 선택 세션

	// 지금 MatchmakingManager 대기열에 티켓이 올라가 있는지. 대기 중 접속이 끊기면
	// GameSession::OnDisconnected에서 이 값을 보고 티켓을 정리한다 (죽은 티켓이 버킷에 남는 것 방지).
	bool					_isQueued = false;
};