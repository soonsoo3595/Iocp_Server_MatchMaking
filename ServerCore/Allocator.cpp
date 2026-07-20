#include "pch.h"
#include "Allocator.h"
#include "Memory.h"

/*-------------------
	BaseAllocator
-------------------*/

void* BaseAllocator::Alloc(int32 size)
{
	return ::malloc(size);
}

void BaseAllocator::Release(void* ptr)
{
	::free(ptr);
}

/*-------------------
	StompAllocator
-------------------*/

/// <summary>
/// 어떤 사이즈를 할당 요청할 때 그거에 제일 가까운 페이지 사이즈를 반올림해서 뱉어줄 것임
/// </summary>
void* StompAllocator::Alloc(int32 size)
{
	const int64 pageCount = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	
	// 아래 코드는 다운캐스트 시 오버플로우 문제를 해결하기 위해서
	// 데이터 배치해야 하는 오프셋 영역 계산
	const int64 dataOffset = pageCount * PAGE_SIZE - size;
	void* baseAddress = ::VirtualAlloc(NULL, pageCount * PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	return static_cast<void*>(static_cast<int8*>(baseAddress) + dataOffset);
}

void StompAllocator::Release(void* ptr)
{
	const int64 address = reinterpret_cast<int64>(ptr);
	const int64 baseAddress = address - (address % PAGE_SIZE);
	::VirtualFree(reinterpret_cast<void*>(baseAddress), 0, MEM_RELEASE);
}

/*-------------------
	PoolAllocator
-------------------*/

void* PoolAllocator::Alloc(int32 size)
{
	return GMemory->Allocate(size);
}

void PoolAllocator::Release(void* ptr)
{
	GMemory->Release(ptr);
}