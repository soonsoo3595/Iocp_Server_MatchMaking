#include "pch.h"
#include "ClientPacketHandler.h"
#include "GameSession.h"
#include "GameSessionManager.h"
#include "ChampSelectSession.h"
#include "MatchAcceptSession.h"
#include "MatchmakingManager.h"
#include "MmrManager.h"
#include "Player.h"
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
		LOG_WARNING(L"로그인 실패 : 이미 로그인된 세션 (playerId=%llu, 닉네임=%s)",
			gameSession->_player->playerId, StringUtils::Utf8ToWide(gameSession->_player->name).c_str());

		loginPkt.set_success(false);
		auto sendBuffer = ClientPacketHandler::MakeSendBuffer(loginPkt);
		session->Send(sendBuffer);
		return true;
	}

	const string name = StringUtils::Trim(pkt.name());

	if (StringUtils::IsValidNickname(name) == false || GSessionManager.TryReserveName(name) == false)
	{
		LOG_WARNING(L"로그인 실패 : 닉네임 = %s", StringUtils::Utf8ToWide(name).c_str());

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

	LOG_INFO(L"로그인 성공 : playerId = %llu, 닉네임 = %s, mmr=%u", playerRef->playerId, StringUtils::Utf8ToWide(name).c_str(), playerRef->mmr);

	loginPkt.set_success(true);
	Protocol::Player* playerProto = loginPkt.mutable_player();
	playerProto->set_id(playerRef->playerId);
	playerProto->set_name(playerRef->name);

	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(loginPkt);
	session->Send(sendBuffer);

	return true;
}

bool Handle_C_MATCH_START(PacketSessionRef& session, Protocol::C_MATCH_START& pkt)
{
	GameSessionRef gameSession = static_pointer_cast<GameSession>(session);

	// 로그인 안 한 세션이 매칭을 신청하면 안 됨 (player->mmr 등 널 참조 방지)
	if (gameSession->_player == nullptr)
	{
		LOG_WARNING(L"매칭 신청 실패 : 로그인되지 않은 세션");
		return false;
	}

	MatchmakingTicket ticket;
	ticket.playerId = gameSession->_player->playerId;
	ticket.mmr = gameSession->_player->mmr;
	ticket.name = gameSession->_player->name;
	ticket.primaryPosition = pkt.primaryposition();
	ticket.secondaryPosition = pkt.secondaryposition();
	ticket.queuedAt = ::GetTickCount64();
	ticket.session = gameSession;

	GMatchmakingManager->DoAsync(&MatchmakingManager::AddTicket, ticket);

	Protocol::S_MATCH_QUEUED queuedPkt;
	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(queuedPkt);
	session->Send(sendBuffer);

	return true;
}

bool Handle_C_MATCH_CANCEL(PacketSessionRef& session, Protocol::C_MATCH_CANCEL& pkt)
{
	GameSessionRef gameSession = static_pointer_cast<GameSession>(session);

	if (gameSession->_player == nullptr)
	{
		LOG_WARNING(L"매칭 취소 실패 : 로그인되지 않은 세션");
		return false;
	}

	GMatchmakingManager->DoAsync(&MatchmakingManager::RemoveTicket, gameSession->_player->playerId, gameSession->_player->mmr);

	LOG_INFO(L"매칭 취소 : playerId=%llu", gameSession->_player->playerId);

	Protocol::S_MATCH_CANCELED canceledPkt;
	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(canceledPkt);
	session->Send(sendBuffer);

	return true;
}

bool Handle_C_MATCH_ACCEPT(PacketSessionRef& session, Protocol::C_MATCH_ACCEPT& pkt)
{
	GameSessionRef gameSession = static_pointer_cast<GameSession>(session);

	if (gameSession->_player == nullptr)
		return false;

	shared_ptr<MatchAcceptSession> acceptSession = gameSession->_matchAcceptSession.lock();
	if (acceptSession == nullptr)
	{
		LOG_WARNING(L"매치 수락 실패 : 대기 중인 매치가 없음 (playerId=%llu)", gameSession->_player->playerId);
		return false;
	}

	acceptSession->DoAsync(&MatchAcceptSession::OnAccept, gameSession->_player->playerId);

	return true;
}

bool Handle_C_MATCH_DECLINE(PacketSessionRef& session, Protocol::C_MATCH_DECLINE& pkt)
{
	GameSessionRef gameSession = static_pointer_cast<GameSession>(session);

	if (gameSession->_player == nullptr)
		return false;

	shared_ptr<MatchAcceptSession> acceptSession = gameSession->_matchAcceptSession.lock();
	if (acceptSession == nullptr)
	{
		LOG_WARNING(L"매치 거절 실패 : 대기 중인 매치가 없음 (playerId=%llu)", gameSession->_player->playerId);
		return false;
	}

	acceptSession->DoAsync(&MatchAcceptSession::OnDecline, gameSession->_player->playerId);

	return true;
}

bool Handle_C_PICK_CHAMPION(PacketSessionRef& session, Protocol::C_PICK_CHAMPION& pkt)
{
	GameSessionRef gameSession = static_pointer_cast<GameSession>(session);

	if (gameSession->_player == nullptr)
		return false;

	shared_ptr<ChampSelectSession> champSelectSession = gameSession->_champSelectSession.lock();
	if (champSelectSession == nullptr)
	{
		LOG_WARNING(L"픽 실패 : 참여 중인 챔피언 선택이 없음 (playerId=%llu)", gameSession->_player->playerId);
		return false;
	}

	champSelectSession->DoAsync(&ChampSelectSession::OnPick, gameSession->_player->playerId, pkt.championid());

	return true;
}

bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	GameSessionRef gameSession = static_pointer_cast<GameSession>(session);

	if (gameSession->_player == nullptr)
		return false;

	// 지금은 챔피언 선택 화면에서만 채팅을 지원한다 (요청 범위).
	// 다른 상태(로비/대기열 등)는 브로드캐스트 대상 그룹이 없어서 무시.
	shared_ptr<ChampSelectSession> champSelectSession = gameSession->_champSelectSession.lock();
	if (champSelectSession == nullptr)
	{
		LOG_WARNING(L"채팅 실패 : 챔피언 선택 중이 아님 (playerId=%llu)", gameSession->_player->playerId);
		return false;
	}

	champSelectSession->DoAsync(&ChampSelectSession::OnChat, gameSession->_player->playerId, pkt.msg());

	return true;
}