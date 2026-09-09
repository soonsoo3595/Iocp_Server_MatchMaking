#include "pch.h"
#include "MatchmakingManager.h"
#include "MatchAcceptSession.h"
#include "GameSession.h"
#include "ClientPacketHandler.h"
#include <algorithm>
#include <functional>
#include <utility>

shared_ptr<MatchmakingManager> GMatchmakingManager = nullptr;

void InitMatchmakingManager()
{
	GMatchmakingManager = make_shared<MatchmakingManager>();

	// 최초 1회 예약. 이후로는 Tick() 스스로가 매번 30초 뒤 자신을 다시 예약한다.
	GMatchmakingManager->DoAsync(&MatchmakingManager::Tick);
}

namespace
{
	const Protocol::Position POSITIONS[5] =
	{
		Protocol::POSITION_TOP,
		Protocol::POSITION_JUG,
		Protocol::POSITION_MID,
		Protocol::POSITION_BOT,
		Protocol::POSITION_SUP,
	};

	// pool에서 position을 맡을 후보 하나를 우선순위(1지망 -> 2지망 -> 상관없음) 순으로 찾아
	// pool에서 제거하고 반환한다. 못 찾으면 false.
	bool PickOneForPosition(Vector<MatchmakingTicket>& pool, Protocol::Position position, OUT MatchmakingTicket& out)
	{
		auto pick = [&](std::function<bool(const MatchmakingTicket&)> pred) -> bool
		{
			for (size_t i = 0; i < pool.size(); i++)
			{
				if (pred(pool[i]) == false)
					continue;

				out = pool[i];
				pool[i] = pool.back();
				pool.pop_back();
				return true;
			}
			return false;
		};

		if (pick([position](const MatchmakingTicket& t) { return t.primaryPosition == position; }))
			return true;

		if (pick([position](const MatchmakingTicket& t) { return t.secondaryPosition == position; }))
			return true;

		if (pick([](const MatchmakingTicket& t) { return t.primaryPosition == Protocol::POSITION_NONE; }))
			return true;

		return false;
	}

	// pool(같은 mmr대의 후보 묶음)에서 포지션이 꽉 찬 두 팀(5vs5)을 뽑아본다.
	// 기획서 §4.2의 "포지션별로 후보를 뽑는다"까지는 그대로 따르되, 그 다음 §4.2/§8에서
	// 열린 문제로 남겨뒀던 "MMR 지그재그 분배가 포지션 제약과 충돌하는 문제"는
	// 포지션 하나당 2명을 먼저 확정한 뒤, 그 2명을 두 팀에 나눠 배정하면서 그때그때
	// 누적 mmr이 더 낮은 팀에 더 높은 mmr을 붙이는 방식으로 균형을 맞추는 걸로 풀었다
	// (전역으로 10명을 다 모아놓고 지그재그하면 포지션이 한쪽 팀에 몰릴 수 있어서).
	bool TryFormMatch(Vector<MatchmakingTicket> pool, OUT Vector<MatchedPlayer>& teamA, OUT Vector<MatchedPlayer>& teamB)
	{
		uint32 teamAMmrSum = 0;
		uint32 teamBMmrSum = 0;

		for (Protocol::Position position : POSITIONS)
		{
			MatchmakingTicket first;
			MatchmakingTicket second;

			if (PickOneForPosition(pool, position, first) == false)
				return false;
			if (PickOneForPosition(pool, position, second) == false)
				return false;

			if (first.mmr < second.mmr)
				std::swap(first, second);

			// 지금까지 mmr 합이 더 낮은 팀에 이번 포지션의 더 높은 mmr을 몰아줘서 격차를 줄인다.
			if (teamAMmrSum <= teamBMmrSum)
			{
				teamA.push_back({ first, position });
				teamB.push_back({ second, position });
				teamAMmrSum += first.mmr;
				teamBMmrSum += second.mmr;
			}
			else
			{
				teamA.push_back({ second, position });
				teamB.push_back({ first, position });
				teamAMmrSum += second.mmr;
				teamBMmrSum += first.mmr;
			}
		}

		return true;
	}

	// 포지션까지 다 맞춘 두 팀을 확정 짓고, 실제 통보(S_MATCH_FOUND)와 수락 대기는
	// MatchAcceptSession(매치 하나당 하나 생기는 임시 JobQueue 객체)에 넘긴다.
	void FinalizeMatch(Vector<MatchedPlayer> teamA, Vector<MatchedPlayer> teamB)
	{
		static Atomic<uint64> matchIdGenerator = 1;
		const uint64 matchId = matchIdGenerator++;

		shared_ptr<MatchAcceptSession> acceptSession = make_shared<MatchAcceptSession>(matchId, std::move(teamA), std::move(teamB));
		acceptSession->Start();

		LOG_INFO(L"매치 성사 : matchId=%llu (수락 대기 시작)", matchId);
	}
}

MatchmakingManager::MatchmakingManager()
{
	_bucketedTickets.resize(MMR_BUCKET_COUNT);
}

int32 MatchmakingManager::GetBucketIndex(uint32 mmr) const
{
	int32 index = static_cast<int32>(mmr / MMR_BUCKET_SIZE);

	if (index < 0)
		index = 0;
	else if (index > MMR_BUCKET_COUNT - 1)
		index = MMR_BUCKET_COUNT - 1;

	return index;
}

void MatchmakingManager::AddTicket(MatchmakingTicket ticket)
{
	const int32 bucketIndex = GetBucketIndex(ticket.mmr);
	_bucketedTickets[bucketIndex].push_back(ticket);

	LOG_INFO(L"매칭 등록 : playerId=%llu, mmr=%u, bucket=%d", ticket.playerId, ticket.mmr, bucketIndex);
}

void MatchmakingManager::RemoveTicket(uint64 playerId, uint32 mmr)
{
	Vector<MatchmakingTicket>& bucket = _bucketedTickets[GetBucketIndex(mmr)];

	for (size_t i = 0; i < bucket.size(); i++)
	{
		if (bucket[i].playerId != playerId)
			continue;

		// 순서 유지 안 해도 되므로 마지막 원소와 바꿔치기 후 pop (O(1) 제거)
		bucket[i] = bucket.back();
		bucket.pop_back();
		return;
	}
}

void MatchmakingManager::CollectCandidates(uint32 mmr, int32 range, OUT Vector<MatchmakingTicket>& out) const
{
	// range를 커버하는 데 필요한 버킷 개수 (양옆으로)
	const int32 bucketSpan = (range + MMR_BUCKET_SIZE - 1) / MMR_BUCKET_SIZE;
	const int32 centerIndex = GetBucketIndex(mmr);

	int32 beginIndex = centerIndex - bucketSpan;
	if (beginIndex < 0)
		beginIndex = 0;

	int32 endIndex = centerIndex + bucketSpan;
	if (endIndex > MMR_BUCKET_COUNT - 1)
		endIndex = MMR_BUCKET_COUNT - 1;

	for (int32 i = beginIndex; i <= endIndex; i++)
	{
		for (const MatchmakingTicket& ticket : _bucketedTickets[i])
		{
			int32 diff = static_cast<int32>(ticket.mmr) - static_cast<int32>(mmr);
			if (diff < 0)
				diff = -diff;

			if (diff <= range)
				out.push_back(ticket);
		}
	}
}

void MatchmakingManager::Tick()
{
	// 대기 중인 티켓을 전부 모아서, 오래 기다린 순으로 정렬한다 (기획서 §4.2-1).
	// 버킷 구조는 "mmr 범위 조회"를 빠르게 하려는 거지, 대기 시간 순회에는 안 맞아서
	// 여기서는 그냥 다 펼쳐놓고 정렬한다 — 지금 규모에서는 이 비용도 무시할 만하다.
	Vector<MatchmakingTicket> allTickets;
	for (const Vector<MatchmakingTicket>& bucket : _bucketedTickets)
		for (const MatchmakingTicket& ticket : bucket)
			allTickets.push_back(ticket);

	std::sort(allTickets.begin(), allTickets.end(), [](const MatchmakingTicket& a, const MatchmakingTicket& b)
		{
			return a.queuedAt < b.queuedAt;
		});

	// 이번 tick 안에서 이미 다른 매치에 편성된 사람은 다시 후보로 뽑히면 안 된다.
	HashSet<uint64> alreadyMatched;

	for (const MatchmakingTicket& seed : allTickets)
	{
		if (alreadyMatched.find(seed.playerId) != alreadyMatched.end())
			continue;

		Vector<MatchmakingTicket> candidates;
		CollectCandidates(seed.mmr, MMR_MATCH_RANGE, candidates);

		candidates.erase(
			std::remove_if(candidates.begin(), candidates.end(), [&alreadyMatched](const MatchmakingTicket& t)
				{
					return alreadyMatched.find(t.playerId) != alreadyMatched.end();
				}),
			candidates.end());

		if (candidates.size() < 10)
			continue; // 후보가 10명이 안 모이면 이번 tick은 미성사 (§4.2-5)

		Vector<MatchedPlayer> teamA;
		Vector<MatchedPlayer> teamB;
		if (TryFormMatch(candidates, teamA, teamB) == false)
			continue; // 포지션 조합이 안 맞으면 미성사, 다음 tick에 재시도

		for (const MatchedPlayer& player : teamA)
		{
			RemoveTicket(player.ticket.playerId, player.ticket.mmr);
			alreadyMatched.insert(player.ticket.playerId);
		}
		for (const MatchedPlayer& player : teamB)
		{
			RemoveTicket(player.ticket.playerId, player.ticket.mmr);
			alreadyMatched.insert(player.ticket.playerId);
		}

		FinalizeMatch(std::move(teamA), std::move(teamB));
	}

	// 다음 30초 뒤 스스로를 다시 예약 (JobTimer는 "N초 뒤 한 번"만 지원하므로 반복은 이렇게 구현)
	DoTimer(TICK_INTERVAL_MS, &MatchmakingManager::Tick);
}
