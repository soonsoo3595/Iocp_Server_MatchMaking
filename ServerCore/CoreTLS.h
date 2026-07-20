#pragma once
#include <stack>
// 스레드에서 사용할 TLS들을 관리

extern thread_local uint32				LThreadId;		// 스레드ID(순차적으로 늘어나게)
extern thread_local uint64				LEndTickCount;

extern thread_local std::stack<int32>	LLockStack;
extern thread_local SendBufferChunkRef  LSendBufferChunk;
extern thread_local class JobQueue*		LCurrentJobQueue;	// 내가 실행하고 있는 잡큐