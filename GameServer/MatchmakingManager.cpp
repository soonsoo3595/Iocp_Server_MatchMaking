#include "pch.h"
#include "MatchmakingManager.h"

shared_ptr<MatchmakingManager> GMatchmakingManager = nullptr;

void InitMatchmakingManager()
{
	GMatchmakingManager = make_shared<MatchmakingManager>();
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
