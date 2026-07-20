#pragma once
#include "Session.h"

class GameSession : public PacketSession
{
public:
	~GameSession()
	{
		// 현재 소멸자 로그가 안 찍히고 있음 -> 메모리 Leak 발생!
		// Player -> GameSeesionRef를 들고 있어서 사이클 생김
		cout << "~GameSession" << endl;
	}

	virtual void OnConnected() override;
	virtual void OnDisconnected() override;
	virtual void OnRecvPacket(BYTE* buffer, int32 len) override;
	virtual void OnSend(int32 len) override;

public:
	Vector<PlayerRef> _players;

	PlayerRef _currentPlayer;		// 현재 어떤 플레이어로 접속중인지
	weak_ptr<class Room> _room;		// 어떤 Room에 들어가있는지
};