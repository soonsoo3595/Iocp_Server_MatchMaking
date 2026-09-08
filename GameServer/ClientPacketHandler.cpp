#include "pch.h"
#include "ClientPacketHandler.h"
#include "GameSession.h"
#include "GameSessionManager.h"
#include "MmrManager.h"
#include "Player.h"
#include "Room.h"
#include "StringUtils.h"

PacketHandlerFunc GPacketHandler[UINT16_MAX];

// 실제 처리 로직들

bool Handle_INVALID(PacketSessionRef& session, BYTE* buffer, int32 len)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	// TODO : Log
	return false;
}

// 로그인 패킷 처리
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt)
{
	GameSessionRef gameSession = static_pointer_cast<GameSession>(session);

	Protocol::S_LOGIN loginPkt;

	// 이미 로그인된 세션이 다시 로그인을 시도하는 경우.
	// 검증 없이 새 Player로 덮어쓰면 이전에 예약해둔 이름이 영영 반납되지 않는다.
	if (gameSession->_player != nullptr)
	{
		LOG_WARNING(L"로그인 실패 : 이미 로그인된 세션 (playerId=%llu, 닉네임=%hs)",
			gameSession->_player->playerId, gameSession->_player->name.c_str());

		loginPkt.set_success(false);
		auto sendBuffer = ClientPacketHandler::MakeSendBuffer(loginPkt);
		session->Send(sendBuffer);
		return true;
	}

	const string name = StringUtils::Trim(pkt.name());

	if (StringUtils::IsValidNickname(name) == false || GSessionManager.TryReserveName(name) == false)
	{
		LOG_WARNING(L"로그인 실패 : 닉네임 = %hs", name.c_str());

		loginPkt.set_success(false);
		auto sendBuffer = ClientPacketHandler::MakeSendBuffer(loginPkt);
		session->Send(sendBuffer);
		return true;
	}

	static Atomic<uint64> idGenerator = 1;

	PlayerRef playerRef = MakeShared<Player>();
	playerRef->playerId = idGenerator++;
	playerRef->name = name;
	playerRef->mmr = GMmrManager.GetOrCreateMmr(name);   // 서버 내부 전용, 패킷엔 안 실음
	playerRef->ownerSession = gameSession;

	gameSession->_player = playerRef;

	LOG_INFO(L"로그인 성공 : playerId = %llu, 닉네임 = %hs, mmr=%u", playerRef->playerId, name.c_str(), playerRef->mmr);

	loginPkt.set_success(true);
	Protocol::Player* playerProto = loginPkt.mutable_player();
	playerProto->set_id(playerRef->playerId);
	playerProto->set_name(playerRef->name);

	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(loginPkt);
	session->Send(sendBuffer);

	return true;
}

bool Handle_C_ENTER_GAME(PacketSessionRef& session, Protocol::C_ENTER_GAME& pkt)
{
	/*
	GameSessionRef gameSession = static_pointer_cast<GameSession>(session);

	uint64 index = pkt.playerindex();
	// TODO : Validation

	gameSession->_currentPlayer = gameSession->_players[index]; // READ_ONLY?
	gameSession->_room = GRoom;
	// GRoom.Enter(player); // WRITE_LOCK
	// GRoom.PushJob(MakeShared<EnterJob>(GRoom, player));	// 예약
	GRoom->DoAsync(&Room::Enter, gameSession->_currentPlayer);

	Protocol::S_ENTER_GAME enterGamePkt;
	enterGamePkt.set_success(true);
	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(enterGamePkt);
	if (GameSessionRef ownerSession = gameSession->_currentPlayer->ownerSession.lock())
		ownerSession->Send(sendBuffer);

	*/
	return true;
}

bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	/*
	std::cout << pkt.msg() << endl;

	Protocol::S_CHAT chatPkt;
	chatPkt.set_msg(pkt.msg());
	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(chatPkt);

	// GRoom.Broadcast(sendBuffer); // WRITE_LOCK
	// GRoom.PushJob(MakeShared<BroadcastJob>(GRoom, sendBuffer));
	GRoom->DoAsync(&Room::Broadcast, sendBuffer);
	*/

	return true;
}