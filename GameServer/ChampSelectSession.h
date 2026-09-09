#pragma once
#include "JobQueue.h"
#include "MatchmakingManager.h"

// 챔피언 선택 화면에 들어온 한 명의 상태.
struct ChampSelectParticipant
{
	uint64				playerId		= 0;
	string				name;
	Protocol::Position	position		= Protocol::POSITION_NONE;
	uint32				pickedChampionId	= 0;	// 0 = 아직 픽 안 함
	weak_ptr<class GameSession> session;
};

// 챔피언 선택(픽) 단계를 전담하는 세션. 매치 하나당 하나씩 생성되는 임시 객체
// (MatchAcceptSession과 같은 수명 패턴 - 전역 싱글톤이 아니라 그때그때 만들어짐).
// JobQueue를 상속해서 10명의 픽 패킷이 겹쳐 들어와도 순차 실행이 보장된다 -
// 반드시 DoAsync를 통해서만 멤버 함수를 호출할 것.
class ChampSelectSession : public JobQueue
{
public:
	ChampSelectSession(uint64 matchId, const Vector<MatchedPlayer>& teamA, const Vector<MatchedPlayer>& teamB);

	// S_CHAMPSELECT_START 전송 + 각 GameSession에 자신을 등록.
	// shared_ptr로 감싼 뒤(MatchAcceptSession::Resolve에서) 호출해야 한다.
	void Start();

	void OnPick(uint64 playerId, uint32 championId);
	void OnChat(uint64 playerId, string msg);

private:
	int32 FindParticipant(uint64 playerId) const;
	bool IsChampionTaken(uint32 championId) const;
	void CheckAllPicked();

private:
	uint64							_matchId = 0;
	Vector<ChampSelectParticipant>	_participants;	// 10명
};
