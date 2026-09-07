#pragma once

class Player
{
public:

	uint64					playerId = 0;
	string					name;
	uint32					mmr = 0;		// 서버 내부 전용 — 어떤 패킷에도 실어 보내지 않음
	weak_ptr<GameSession>	ownerSession;	// Player가 소속된 세션 (약한 참조로 순환 참조 방지)
};

