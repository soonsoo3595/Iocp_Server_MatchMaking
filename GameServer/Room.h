#pragma once
#include "JobQueue.h"

class Room : public JobQueue
{
public:
	void Enter(PlayerRef player);
	void Leave(PlayerRef player);
	void Broadcast(SendBufferRef sendBuffer);

private:
	map<uint64, PlayerRef> _players;
};

extern shared_ptr<Room> GRoom;

// main()에서 CoreGlobal(GMemory 등) 초기화가 끝난 뒤 명시적으로 호출해서 GRoom을 만든다.
// GRoom을 전역 변수 선언과 동시에 생성하면(정적 초기화), GMemory를 만드는
// CoreGlobal의 전역 객체와 초기화 순서가 번역 단위(cpp 파일)마다 달라서
// GMemory가 아직 nullptr인 상태에서 Room(JobQueue) 생성자가 메모리 풀을 건드릴 수 있다.
void InitRoom();