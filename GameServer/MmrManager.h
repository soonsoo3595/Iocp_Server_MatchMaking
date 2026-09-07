#pragma once
#include <random>

/*------------------
	MmrManager

	MMR과 관련된 로직을 여기 모은다.
	닉네임 -> MMR 매핑을 서버 프로세스 생존 기간 동안 기억한다.
	DB가 없으므로 최초 접속 시 랜덤 MMR을 부여하고, 같은 닉네임으로
	재접속하면 이전에 부여한 MMR을 그대로 돌려준다. (서버 재시작 시 초기화)
-------------------*/

class MmrManager
{
	enum : uint32
	{
		MMR_MIN = 800,
		MMR_MAX = 1600,
	};

public:
	uint32 GetOrCreateMmr(const string& name);

private:
	USE_LOCK;
	HashMap<string, uint32> _mmrByName;
};

extern MmrManager GMmrManager;
