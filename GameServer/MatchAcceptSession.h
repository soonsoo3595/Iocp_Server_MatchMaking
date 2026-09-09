#pragma once
#include "JobQueue.h"
#include "MatchmakingManager.h"

// 매치 성사(S_MATCH_FOUND) 통보 ~ 전원 수락(챔피언 선택 진입) 또는 재매칭까지의
// "수락 대기" 단계를 전담하는 세션. 매치 하나당 하나씩 만들어지는 임시 객체라
// GRoom/GMatchmakingManager처럼 전역 싱글톤이 아니다 (FinalizeMatch에서 그때그때 생성).
//
// JobQueue를 상속해서, 10명의 수락/거절 패킷과 타임아웃 Job이 겹쳐 들어와도
// 순차 실행이 보장된다 - 반드시 DoAsync/DoTimer를 통해서만 멤버 함수를 호출할 것.
class MatchAcceptSession : public JobQueue
{
	enum
	{
		ACCEPT_TIMEOUT_MS = 15000, // 사용자 결정 : 15초 안에 응답 없으면 거절(시간 초과)로 간주
	};

public:
	MatchAcceptSession(uint64 matchId, Vector<MatchedPlayer> teamA, Vector<MatchedPlayer> teamB);

	// S_MATCH_FOUND 전송 + 타임아웃 예약. shared_ptr로 감싼 뒤(FinalizeMatch에서) 호출해야
	// 한다 - 생성자 안에서 하면 shared_from_this()가 아직 안 걸려있어서 실패한다.
	void Start();

	void OnAccept(uint64 playerId);
	void OnDecline(uint64 playerId);
	void OnTimeout();

private:
	bool IsParticipant(uint64 playerId) const;

	// allAccepted=true면 전원 챔피언 선택으로, false면 수락자만 원래 지망 유지한 채
	// 재매칭 큐로 복귀시키고 나머지는 로비로 돌려보낸다.
	void Resolve(bool allAccepted);

private:
	uint64					_matchId = 0;
	Vector<MatchedPlayer>	_teamA;
	Vector<MatchedPlayer>	_teamB;
	HashSet<uint64>			_accepted;
	bool					_resolved = false;
};
