#include "pch.h"
#include "MatchAcceptSession.h"
#include "GameSession.h"
#include "ClientPacketHandler.h"

namespace
{
	Protocol::PlayerInfo ToPlayerInfo(const MatchedPlayer& player)
	{
		Protocol::PlayerInfo info;
		info.set_id(player.ticket.playerId);
		info.set_name(player.ticket.name);
		info.set_position(player.position);
		return info;
	}
}

MatchAcceptSession::MatchAcceptSession(uint64 matchId, Vector<MatchedPlayer> teamA, Vector<MatchedPlayer> teamB)
	: _matchId(matchId)
	, _teamA(std::move(teamA))
	, _teamB(std::move(teamB))
{
}

bool MatchAcceptSession::IsParticipant(uint64 playerId) const
{
	for (const MatchedPlayer& player : _teamA)
		if (player.ticket.playerId == playerId)
			return true;

	for (const MatchedPlayer& player : _teamB)
		if (player.ticket.playerId == playerId)
			return true;

	return false;
}

void MatchAcceptSession::Start()
{
	shared_ptr<MatchAcceptSession> self = static_pointer_cast<MatchAcceptSession>(shared_from_this());

	Vector<Protocol::PlayerInfo> teamAInfos;
	for (const MatchedPlayer& player : _teamA)
		teamAInfos.push_back(ToPlayerInfo(player));

	Vector<Protocol::PlayerInfo> teamBInfos;
	for (const MatchedPlayer& player : _teamB)
		teamBInfos.push_back(ToPlayerInfo(player));

	auto notify = [&](const Vector<MatchedPlayer>& myTeam, const Vector<Protocol::PlayerInfo>& myInfos, const Vector<Protocol::PlayerInfo>& enemyInfos)
	{
		for (const MatchedPlayer& player : myTeam)
		{
			GameSessionRef session = player.ticket.session.lock();
			if (session == nullptr)
				continue; // 대기 중 접속 끊김 - 이탈 처리는 기획서 §5.4에서 확장 과제로 남겨둔 부분

			// 이 GameSession으로 들어오는 C_MATCH_ACCEPT/C_MATCH_DECLINE이 이 세션으로
			// 연결되도록 등록해둔다 (Room을 GameSession::_room에 등록하던 것과 같은 패턴).
			session->_matchAcceptSession = self;

			Protocol::S_MATCH_FOUND foundPkt;
			foundPkt.set_matchid(_matchId);
			for (const Protocol::PlayerInfo& info : myInfos)
				*foundPkt.add_myteam() = info;
			for (const Protocol::PlayerInfo& info : enemyInfos)
				*foundPkt.add_enemyteam() = info;
			foundPkt.set_myposition(player.position);

			auto sendBuffer = ClientPacketHandler::MakeSendBuffer(foundPkt);
			session->Send(sendBuffer);
		}
	};

	notify(_teamA, teamAInfos, teamBInfos);
	notify(_teamB, teamBInfos, teamAInfos);

	DoTimer(ACCEPT_TIMEOUT_MS, &MatchAcceptSession::OnTimeout);

	LOG_INFO(L"매치 수락 대기 시작 : matchId=%llu", _matchId);
}

void MatchAcceptSession::OnAccept(uint64 playerId)
{
	if (_resolved || IsParticipant(playerId) == false)
		return;

	_accepted.insert(playerId);

	LOG_INFO(L"매치 수락 : matchId=%llu, playerId=%llu (%d/10)", _matchId, playerId, static_cast<int32>(_accepted.size()));

	if (_accepted.size() == 10)
		Resolve(true);
}

void MatchAcceptSession::OnDecline(uint64 playerId)
{
	if (_resolved || IsParticipant(playerId) == false)
		return;

	LOG_INFO(L"매치 거절 : matchId=%llu, playerId=%llu", _matchId, playerId);
	Resolve(false);
}

void MatchAcceptSession::OnTimeout()
{
	if (_resolved)
		return;

	LOG_INFO(L"매치 수락 타임아웃 : matchId=%llu (%d/10 수락)", _matchId, static_cast<int32>(_accepted.size()));
	Resolve(false);
}

void MatchAcceptSession::Resolve(bool allAccepted)
{
	_resolved = true;

	if (allAccepted)
	{
		Vector<Protocol::PlayerInfo> allInfos;
		for (const MatchedPlayer& player : _teamA)
			allInfos.push_back(ToPlayerInfo(player));
		for (const MatchedPlayer& player : _teamB)
			allInfos.push_back(ToPlayerInfo(player));

		Protocol::S_CHAMPSELECT_START startPkt;
		startPkt.set_matchid(_matchId);
		for (const Protocol::PlayerInfo& info : allInfos)
			*startPkt.add_participants() = info;

		auto sendToTeam = [&](const Vector<MatchedPlayer>& team)
		{
			for (const MatchedPlayer& player : team)
			{
				GameSessionRef session = player.ticket.session.lock();
				if (session == nullptr)
					continue;

				session->_matchAcceptSession.reset();

				auto sendBuffer = ClientPacketHandler::MakeSendBuffer(startPkt);
				session->Send(sendBuffer);
			}
		};

		sendToTeam(_teamA);
		sendToTeam(_teamB);

		LOG_INFO(L"전원 수락 완료, 챔피언 선택 진입 : matchId=%llu", _matchId);
		return;
	}

	// 전원 수락 실패 : 수락했던 사람은 원래 지망(포지션)을 유지한 채 재매칭 큐로,
	// 거절했거나 응답 안 한 사람은 그냥 로비로.
	auto resolveTeam = [&](const Vector<MatchedPlayer>& team)
	{
		for (const MatchedPlayer& player : team)
		{
			GameSessionRef session = player.ticket.session.lock();
			if (session != nullptr)
				session->_matchAcceptSession.reset();

			if (session == nullptr)
				continue;

			if (_accepted.find(player.ticket.playerId) != _accepted.end())
			{
				// 원래 티켓(mmr/포지션/queuedAt 그대로) 재등록 -> 기다린 시간도 유지됨
				GMatchmakingManager->DoAsync(&MatchmakingManager::AddTicket, player.ticket);

				Protocol::S_MATCH_QUEUED queuedPkt;
				auto sendBuffer = ClientPacketHandler::MakeSendBuffer(queuedPkt);
				session->Send(sendBuffer);
			}
			else
			{
				Protocol::S_MATCH_CANCELED canceledPkt;
				auto sendBuffer = ClientPacketHandler::MakeSendBuffer(canceledPkt);
				session->Send(sendBuffer);
			}
		}
	};

	resolveTeam(_teamA);
	resolveTeam(_teamB);

	LOG_INFO(L"매치 무산, 수락자만 재매칭 큐로 복귀 : matchId=%llu", _matchId);
}
