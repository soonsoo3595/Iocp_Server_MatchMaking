#pragma once

/*
	메모리 풀을 여러개 할당 [32byte][32~64][...][][]
	각각의 메모리 풀은 자기가 담당하는 크기를 가지고 있음
	다른 방법은 메모리 풀을 한번에 크게 잡은 후 바이트 나눠서 할당
	대부분 전자가 관리가 편함
*/

/*-----------------
	MemoryHeader
	디버깅용 -> 표준 할당자도 비슷하게 이런 헤더를 사용함
------------------*/


enum
{
	SLIST_ALIGNMENT = 16
};

DECLSPEC_ALIGN(SLIST_ALIGNMENT)
struct MemoryHeader : public SLIST_ENTRY
{
	// [MemoryHeader][Data]
	MemoryHeader(int32 size) : allocSize(size) {}

	static void* AttachHeader(MemoryHeader* header, int32 size)
	{
		new(header)MemoryHeader(size); // placement new

		// 1을 더해주면 c++ 특성 상 memoryheader만큼 건너뛰게 되기에 Data의 시작 위치로 감
		return reinterpret_cast<void*>(++header);
	}

	static MemoryHeader* DetachHeader(void* ptr)
	{
		MemoryHeader* header = reinterpret_cast<MemoryHeader*>(ptr) - 1;
		return header;
	}

	int32 allocSize;
	// TODO : 필요한 추가 정보
};

/*-----------------
	MemoryPool
------------------*/

// 지금 비슷한 크기의 메모리는 다 같은 메모리 풀에 밀어서 사용하고 있음
// 어떤 메모리가 오염됐을 때 이게 다시 메모리 풀로 돌아가다보니 어떤 메모리풀이 오염됐는지 찾기 힘듦
// 메모리 풀을 사용하더라도 경우에 따라 특정 클래스는 같은 메모리풀을 모아 사용하면 어떨까

DECLSPEC_ALIGN(SLIST_ALIGNMENT)
class MemoryPool
{
public:
	MemoryPool(int32 allocSize);
	~MemoryPool();

	void			Push(MemoryHeader* ptr);
	MemoryHeader*	Pop();

private:
	SLIST_HEADER	_header;	// 실질적으로 락 프리 스택을 관리하는 변수. 첫 번째 노드 가리킴
	// 자신이 가지고 있는 메모리 사이즈
	int32 _allocSize = 0;
	atomic<int32> _useCount = 0;		// 실질적으로 사용중인 개수
	atomic<int32> _reserveCount = 0;	// 실질적으로 메모리 풀에 저장되어 있는 개수
};

