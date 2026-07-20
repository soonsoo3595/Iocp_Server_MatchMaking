#pragma once

// 할당 정책 정의할 예정

/*-------------------
	BaseAllocator
-------------------*/

class BaseAllocator
{
public:
	static void*	Alloc(int32 size);
	static void		Release(void* ptr);
};

/*-------------------
	StompAllocator
-------------------*/
// 단점 : 아주 작은 사이즈만 할당할거라해도 엄청나게 큰 영역이 할당됨
// 하지만 개발 단계에서 메모리 오염 문제를 잘 잡을 수 있음
class StompAllocator
{
	// 페이지 크기 배수에 해당하는 메모리를 뱉어 줌
	enum { PAGE_SIZE = 0x1000 };

public:
	static void*	Alloc(int32 size);
	static void		Release(void* ptr);
};

/*-------------------
	PoolAllocator
-------------------*/

class PoolAllocator
{
public:
	static void*	Alloc(int32 size);
	static void		Release(void* ptr);
};

/*-------------------
	STL Allocator
-------------------*/

// STL 컨테이너의 할당자로 쓰기 위해 
template<typename T>
class StlAllocator
{
public:
	using value_type = T;

	StlAllocator() {}

	template<typename Other>
	StlAllocator(const StlAllocator<Other>&) {}

	// 메모리 사이즈가 아니라 벡터의 개수같은 개념
	T* allocate(size_t count)
	{
		const int32 size = static_cast<int32>(count * sizeof(T));
		return static_cast<T*>(PoolAllocator::Alloc(size));
	}

	void deallocate(T* ptr, size_t count)
	{
		PoolAllocator::Release(ptr);
	}
};