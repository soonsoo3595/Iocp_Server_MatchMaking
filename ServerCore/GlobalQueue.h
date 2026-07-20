#pragma once

/*----------------
	GlobalQueue

	전역으로 잡을 배분하는데 도움을 주는 클래스
-----------------*/

class GlobalQueue
{
public:
	GlobalQueue();
	~GlobalQueue();

	void					Push(JobQueueRef jobQueue);
	JobQueueRef				Pop();

private:
	LockQueue<JobQueueRef> _jobQueues;
};

