#pragma once
#include "JobQueue.h"

struct MatchmakingTicket
{
	uint64				playerId				= 0;
	uint32				mmr						= 0;
	Protocol::Position	primaryPosition			= Protocol::POSITION_NONE;
	Protocol::Position	secondaryPosition		= Protocol::POSITION_NONE;
	uint64				queuedAt				= 0;
};

// 모든 접근(등록/매칭 시도)을 Job으로만 처리하면 별도 락 없이도
// 순차 실행이 보장된다. 반드시 DoAsync/DoTimer를 통해서만 멤버 함수를 호출할 것 —
// 밖에서 직접 AddTicket을 부르면 스레드 세이프하지 않다.
class MatchmakingManager : public JobQueue
{
	enum
	{
		MMR_BUCKET_SIZE		= 100,
		MMR_BUCKET_COUNT	= 30,	// 0 ~ 3000 커버. MmrManager는 지금 800~1600으로 생성하지만
									// 나중에 승패로 MMR이 오르내리는 걸 감안해서 여유 있게 잡음.
	};

public:
	MatchmakingManager();

	void AddTicket(MatchmakingTicket ticket);
	void RemoveTicket(uint64 playerId, uint32 mmr);

	// mmr을 기준으로 +-range 이내인 티켓들을 out에 모아준다.
	// 전체 _bucketedTickets를 다 훑지 않고, mmr이 속한 버킷 주변만 스캔한다.
	void CollectCandidates(uint32 mmr, int32 range, OUT Vector<MatchmakingTicket>& out) const;

private:
	int32 GetBucketIndex(uint32 mmr) const;

private:
	// mmr / MMR_BUCKET_SIZE 값으로 인덱싱되는 2차원 구조.
	// 같은 버킷(비슷한 mmr)끼리만 묶어두면, 매칭 시도 시 전체 대기열을
	// 순회하지 않고 mmr이 가까운 버킷 몇 개만 훑으면 되므로 더 빠르다.
	Vector<Vector<MatchmakingTicket>> _bucketedTickets;
};

extern shared_ptr<MatchmakingManager> GMatchmakingManager;

// main()에서 명시적으로 호출해서 GMatchmakingManager를 만든다.
// (GRoom과 동일한 이유 - JobQueue를 상속받는 전역 객체는 shared_ptr로 관리돼야
// DoAsync/DoTimer 내부의 shared_from_this()가 정상 동작한다)
void InitMatchmakingManager();
