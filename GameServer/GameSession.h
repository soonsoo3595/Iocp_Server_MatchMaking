#pragma once
#include "Session.h"

class GameSession : public PacketSession
{
public:
	~GameSession()
	{
		cout << "~GameSession" << endl;
	}

	virtual void OnConnected() override;
	virtual void OnDisconnected() override;
	virtual void OnRecvPacket(BYTE* buffer, int32 len) override;
	virtual void OnSend(int32 len) override;

public:
	PlayerRef				_player;
	weak_ptr<class Room>	_room;		// 어떤 Room에 들어가있는지
	weak_ptr<class MatchAcceptSession>	_matchAcceptSession;	// 지금 응답을 기다리는 매치 수락 세션 (없으면 만료된 weak_ptr)
	weak_ptr<class ChampSelectSession>	_champSelectSession;	// 지금 참여 중인 챔피언 선택 세션
};