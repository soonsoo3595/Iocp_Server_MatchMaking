#pragma once
#include "Allocator.h"

class MemoryPool;


/*-------------
	Memory
	메모리 풀을 관리하는 클래스
	
	일반적으로 메모리가 작은 객체들을 많이 사용하게 되고 커질수록 겹칠 확률이 적기에
	메모리가 작은 애들은 촘촘하게 많이 만들고 큰 애들은 풀 개수를 유동적으로 줄일 것임
---------------*/

class Memory
{
	enum
	{
		// 0 ~ 1024까지 32단위, 1024 ~ 2048까지 128단위, 2048 ~ 4096까지 256단위
		POOL_COUNT = (1024 / 32) + (1024 / 128) + (2048 / 256),
		MAX_ALLOC_SIZE = 4096	// 이보다 큰 메모리의 경우는 풀링을 할 필요가 없음
	};

public:
	Memory();
	~Memory();

	void*	Allocate(int32 size);
	void	Release(void* ptr);

private:
	vector<MemoryPool*> _pools;

	// 메모리 크기 <-> 메모리 풀
	// O(1) 빠르게 찾기 위한 테이블
	MemoryPool* _poolTable[MAX_ALLOC_SIZE + 1];
};

// (프로젝트이름new)와 같이 네이밍
template<typename Type, typename... Args>
Type* Xnew(Args&&... args)
{
	Type* memory = static_cast<Type*>(PoolAllocator::Alloc(sizeof(Type)));

	new(memory)Type(forward<Args>(args)...); // placement new
	return memory;
}

template<typename Type>
void Xdelete(Type* obj)
{
	// BaseAllocator::Release(obj);
	obj->~Type();	// 소멸자 호출
	PoolAllocator::Release(obj);
}

template<typename Type, typename... Args>
shared_ptr<Type> MakeShared(Args&&... args)
{
	return shared_ptr<Type>{ Xnew<Type>(forward<Args>(args)...), Xdelete<Type> };
}