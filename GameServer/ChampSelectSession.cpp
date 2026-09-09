#include "pch.h"
#include "ChampSelectSession.h"
#include "GameSession.h"
#include "ClientPacketHandler.h"

namespace
{
	ChampSelectParticipant ToParticipant(const MatchedPlayer& player)
	{
		ChampSelectParticipant p;
		p.playerId = player.ticket.playerId;
		p.name = player.ticket.name;
		p.position = player.position;
		p.session = player.ticket.session;
		return p;
	}
}

ChampSelectSession::ChampSelectSession(uint64 matchId, const Vector<MatchedPlayer>& teamA, const Vector<MatchedPlayer>& teamB)
	: _matchId(matchId)
{
	for (const MatchedPlayer& player : teamA)
		_participants.push_back(ToParticipant(player));
	for (const MatchedPlayer& player : teamB)
		_participants.push_back(ToParticipant(player));
}

int32 ChampSelectSession::FindParticipant(uint64 playerId) const
{
	for (int32 i = 0; i < static_cast<int32>(_participants.size()); i++)
	{
		if (_participants[i].playerId == playerId)
			return i;
	}

	return -1;
}

bool ChampSelectSession::IsChampionTaken(uint32 championId) const
{
	for (const ChampSelectParticipant& p : _participants)
	{
		if (p.pickedChampionId == championId)
			return true;
	}

	return false;
}

void ChampSelectSession::Start()
{
	shared_ptr<ChampSelectSession> self = static_pointer_cast<ChampSelectSession>(shared_from_this());

	Protocol::S_CHAMPSELECT_START startPkt;
	startPkt.set_matchid(_matchId);
	for (const ChampSelectParticipant& p : _participants)
	{
		Protocol::PlayerInfo* info = startPkt.add_participants();
		info->set_id(p.playerId);
		info->set_name(p.name);
		info->set_position(p.position);
	}

	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(startPkt);

	for (const ChampSelectParticipant& p : _participants)
	{
		GameSessionRef session = p.session.lock();
		if (session == nullptr)
			continue; // 대기/수락 중 접속이 끊긴 경우. 이탈 처리는 기획서 §5.4의 확장 과제.

		// 이 GameSession으로 들어오는 C_PICK_CHAMPION이 이 세션으로 연결되도록 등록.
		session->_champSelectSession = self;

		session->Send(sendBuffer);
	}

	LOG_INFO(L"챔피언 선택 시작 : matchId=%llu", _matchId);
}

void ChampSelectSession::OnPick(uint64 playerId, uint32 championId)
{
	const int32 index = FindParticipant(playerId);
	if (index == -1)
		return;

	if (_participants[index].pickedChampionId != 0)
	{
		LOG_WARNING(L"픽 실패 : 이미 픽을 완료함 (playerId=%llu)", playerId);

		GameSessionRef session = _participants[index].session.lock();
		if (session != nullptr)
		{
			Protocol::S_PICK_FAILED failedPkt;
			auto sendBuffer = ClientPacketHandler::MakeSendBuffer(failedPkt);
			session->Send(sendBuffer);
		}
		return;
	}

	if (IsChampionTaken(championId))
	{
		LOG_WARNING(L"픽 실패 : 이미 다른 사람이 픽한 챔피언 (playerId=%llu, championId=%u)", playerId, championId);

		GameSessionRef session = _participants[index].session.lock();
		if (session != nullptr)
		{
			Protocol::S_PICK_FAILED failedPkt;
			auto sendBuffer = ClientPacketHandler::MakeSendBuffer(failedPkt);
			session->Send(sendBuffer);
		}
		return;
	}

	_participants[index].pickedChampionId = championId;

	LOG_INFO(L"픽 완료 : matchId=%llu, playerId=%llu, championId=%u", _matchId, playerId, championId);

	Protocol::S_PICK_UPDATE updatePkt;
	updatePkt.set_playerid(playerId);
	updatePkt.set_championid(championId);
	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(updatePkt);

	for (const ChampSelectParticipant& p : _participants)
	{
		GameSessionRef session = p.session.lock();
		if (session != nullptr)
			session->Send(sendBuffer);
	}

	CheckAllPicked();
}

void ChampSelectSession::OnChat(uint64 playerId, string msg)
{
	if (FindParticipant(playerId) == -1)
		return;

	Protocol::S_CHAT chatPkt;
	chatPkt.set_playerid(playerId);
	chatPkt.set_msg(msg);
	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(chatPkt);

	for (const ChampSelectParticipant& p : _participants)
	{
		GameSessionRef session = p.session.lock();
		if (session != nullptr)
			session->Send(sendBuffer);
	}
}

void ChampSelectSession::CheckAllPicked()
{
	for (const ChampSelectParticipant& p : _participants)
	{
		if (p.pickedChampionId == 0)
			return; // 아직 다 안 뽑음
	}

	Protocol::S_GAME_START gameStartPkt;
	gameStartPkt.set_matchid(_matchId);
	for (const ChampSelectParticipant& p : _participants)
	{
		Protocol::PlayerInfo* info = gameStartPkt.add_finalteams();
		info->set_id(p.playerId);
		info->set_name(p.name);
		info->set_position(p.position);
		info->set_championid(p.pickedChampionId);
	}

	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(gameStartPkt);

	for (const ChampSelectParticipant& p : _participants)
	{
		GameSessionRef session = p.session.lock();
		if (session == nullptr)
			continue;

		session->_champSelectSession.reset();
		session->Send(sendBuffer);
	}

	LOG_INFO(L"전원 픽 완료, 게임 시작 : matchId=%llu", _matchId);

	// 실제 인게임 로직(전투/스킬/승패 판정)은 기획서 범위 밖 - 여기서 매치메이킹 흐름은 끝.
}
