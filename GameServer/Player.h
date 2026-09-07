#pragma once

class Player
{
public:

	uint64					playerId = 0;
	string					name;
	Protocol::PlayerType	type = Protocol::PLAYER_TYPE_NONE;
	weak_ptr<GameSession>	ownerSession; // Player가 소속된 세션 (약한 참조로 순환 참조 방지)
};

