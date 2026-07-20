#pragma once

struct JobData
{
	JobData(weak_ptr<JobQueue> owner, JobRef job) : owner(owner), job(job)
	{

	}

	weak_ptr<JobQueue>	owner;		// 실행해야 할 owner
	JobRef				job;
};

// 우선순위 큐에 들어갈 아이템
struct TimerItem
{
	bool operator<(const TimerItem& other) const
	{
		return executeTick > other.executeTick;
	}

	uint64 executeTick = 0;			// 실행돼야 될 Tick
	JobData* jobData = nullptr;		// raw 포인터인 이유는 여기저기 복사될 수 있어서 레퍼런스 카운팅에 영향을 줌
};

/*--------------
	JobTimer
---------------*/

class JobTimer
{
public:
	void			Reserve(uint64 tickAfter, weak_ptr<JobQueue> owner, JobRef job);
	void			Distribute(uint64 now);	// 시간이 된 애들 배분
	void			Clear();

private:
	USE_LOCK;
	PriorityQueue<TimerItem>	_items;
	Atomic<bool>				_distributing = false;	// 다시 배치를 하고 있는지 (한 번에 한 명만 일감 배분을 맡겠다)
};

