# 메모리 풀 학습 정리

`Allocator` → `Memory`/`MemoryPool` → (`ObjectPool`) 순서로 층이 나뉜 커스텀 메모리 풀 시스템. `Xnew<Type>()`/`MakeShared<Type>()`로 만드는 이 프로젝트의 거의 모든 객체가 이 위에서 메모리를 받는다.

---

## 전체 그림

```
Xnew<Type>() / MakeShared<Type>()      — 지금까지 계속 봐온 그 함수
        ↓
PoolAllocator::Alloc(size)              — 얇은 위임 (Allocator.h)
        ↓
GMemory->Allocate(size)                 — 크기별로 어느 풀을 쓸지 라우팅, O(1) (Memory.h/.cpp)
        ↓
MemoryPool::Pop()                       — 실제 프리 리스트에서 블록 하나 꺼냄, 락프리 (MemoryPool.h/.cpp)
```
별개로 `ObjectPool<T>`는 **타입 하나당 전용 `MemoryPool`**을 갖는 상위 계층으로, `Session`/`SendBuffer` 등 특정 타입만을 위해 별도로 존재한다.

---

## Allocator — 할당 "정책"을 갈아끼울 수 있게 분리

```cpp
class BaseAllocator   { static void* Alloc(int32 size); static void Release(void* ptr); };  // 그냥 malloc/free
class StompAllocator  { ... };                                                                // 디버깅용 (아래 설명)
class PoolAllocator   { static void* Alloc(int32 size); static void Release(void* ptr); };    // 실제로 쓰는 것
```
셋 다 인터페이스(정적 함수 시그니처)가 동일해서, 실제로는 `PoolAllocator`만 쓰지만 필요하면 `BaseAllocator`(순정 malloc)나 `StompAllocator`(아래)로 통째로 바꿔치기할 수 있게 설계되어 있다.

```cpp
void* PoolAllocator::Alloc(int32 size)    { return GMemory->Allocate(size); }
void  PoolAllocator::Release(void* ptr)   { GMemory->Release(ptr); }
```
`PoolAllocator`는 그냥 전역 `GMemory`(`Memory` 싱글톤)로 위임하는 얇은 껍데기다.

### `StompAllocator` — 힙 오버플로우를 그 자리에서 터뜨리는 디버깅 전용 할당자
```cpp
void* StompAllocator::Alloc(int32 size)
{
	const int64 pageCount = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	const int64 dataOffset = pageCount * PAGE_SIZE - size;   // 페이지 "끝"에 딱 맞춤
	void* baseAddress = ::VirtualAlloc(NULL, pageCount * PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	return static_cast<void*>(static_cast<int8*>(baseAddress) + dataOffset);
}
```
요청한 크기만큼의 메모리를 **페이지 경계 바로 끝에 붙여서** 돌려준다. 그러면 요청한 크기보다 단 1바이트라도 더 쓰는 순간 바로 다음 페이지(매핑 안 된 영역)를 건드리게 되어 **즉시 크래시**한다. 평소 힙 오버플로우는 조용히 옆 메모리를 오염시켰다가 한참 뒤 엉뚱한 곳에서 크래시 나는 경우가 많아 추적이 지옥인데, 이건 사고 지점에서 바로 죽여서 원인을 명확하게 만드는 디버깅 전용 도구다(`_STOMP` 매크로로만 켜짐, 평소엔 안 씀). `Lock`의 타임아웃 크래시, `ASSERT_CRASH`들과 같은 "버그를 숨기지 말고 바로 시끄럽게 드러내라"는 이 코드베이스의 일관된 철학.

---

## Memory — 크기별 풀(size-class) 관리자

### 풀을 미리 여러 개 만들어둔다
```cpp
enum {
	POOL_COUNT = (1024/32) + (1024/128) + (2048/256),
	MAX_ALLOC_SIZE = 4096
};
MemoryPool* _poolTable[MAX_ALLOC_SIZE + 1];   // 크기 -> 풀, O(1) 조회 테이블
```
생성자에서 32바이트 단위로(~1024까지), 그다음 128바이트 단위로(~2048까지), 그다음 256바이트 단위로(~4096까지) **고정 크기 풀을 여러 개** 만든다. 작은 크기일수록 촘촘하게(32바이트 단위), 큰 크기일수록 성기게(256바이트 단위) 나누는 건 — 작은 객체가 훨씬 자주 생성/해제되니 세밀하게 맞춰서 낭비(내부 단편화)를 줄이고, 큰 객체는 어차피 드무니까 대충 맞춰도 상대적 손해가 적다는 판단으로 보인다.

`_poolTable[i]`에는 "크기 `i`짜리 요청이 오면 어느 풀을 쓸지"가 미리 채워져 있다 — 배열 하나에 4097개 포인터(약 32KB)를 희생해서, 매 할당마다 어떤 풀을 쓸지 계산/탐색할 필요 없이 **배열 인덱싱 한 번으로 바로 찾게** 만든 것.

### `Allocate` / `Release`
```cpp
void* Memory::Allocate(int32 size)
{
	const int32 allocSize = size + sizeof(MemoryHeader);   // 헤더만큼 더 요청
	if (allocSize > MAX_ALLOC_SIZE)
		header = _aligned_malloc(allocSize, ...);            // 너무 크면 그냥 일반 할당(풀링 포기)
	else
		header = _poolTable[allocSize]->Pop();                // 풀에서 꺼내옴
	return MemoryHeader::AttachHeader(header, allocSize);
}

void Memory::Release(void* ptr)
{
	MemoryHeader* header = MemoryHeader::DetachHeader(ptr);
	const int32 allocSize = header->allocSize;
	if (allocSize > MAX_ALLOC_SIZE)
		_aligned_free(header);
	else
		_poolTable[allocSize]->Push(header);                  // 풀에 반납
}
```

### `MemoryHeader` — "이 메모리가 어디서 왔는지" 꼬리표
```cpp
// 메모리 레이아웃: [MemoryHeader][Data]   <- 사용자에게 돌려주는 포인터는 Data의 시작 주소
DECLSPEC_ALIGN(SLIST_ALIGNMENT)
struct MemoryHeader : public SLIST_ENTRY
{
	int32 allocSize;

	static void* AttachHeader(MemoryHeader* header, int32 size)
	{
		new(header) MemoryHeader(size);       // placement new로 헤더 초기화
		return reinterpret_cast<void*>(++header);   // 헤더 바로 다음 주소를 돌려줌
	}
	static MemoryHeader* DetachHeader(void* ptr)
	{
		return reinterpret_cast<MemoryHeader*>(ptr) - 1;   // 사용자 포인터 - 1 = 헤더 위치
	}
};
```
실제 데이터 앞에 작은 헤더를 붙여서, `Release(ptr)`가 호출될 때 `ptr`만 보고도(`header = ptr - 1`) "이게 몇 바이트짜리였는지, 어느 풀로 돌려줘야 하는지"를 알아낸다. `malloc`/`free`가 내부적으로 하는 걸 직접 흉내 낸 것 — `AttachHeader`/`DetachHeader`의 `++header`/`header - 1` 포인터 연산은, `IocpEvent`가 `OVERLAPPED`를 상속해서 주소를 직접 계산하던 것과 같은 종류의 "포인터 산술로 헤더/데이터를 오가는" 기법이다.

`MemoryHeader`가 `SLIST_ENTRY`를 상속하고 16바이트 정렬(`DECLSPEC_ALIGN(SLIST_ALIGNMENT)`)이 걸려있는 이유는 바로 아래 `MemoryPool`이 쓰는 Windows의 락프리 리스트 API 요구사항 때문이다.

---

## MemoryPool — 락 없는 프리 리스트

```cpp
void MemoryPool::Push(MemoryHeader* ptr)
{
	ptr->allocSize = 0;                              // "안 쓰는 중" 표시
	::InterlockedPushEntrySList(&_header, ptr);       // OS 제공 lock-free 스택에 반납
	_useCount.fetch_sub(1);
	_reserveCount.fetch_add(1);
}

MemoryHeader* MemoryPool::Pop()
{
	MemoryHeader* memory = ::InterlockedPopEntrySList(&_header);
	if (memory == nullptr)
		memory = _aligned_malloc(_allocSize, SLIST_ALIGNMENT);  // 재고 없으면 새로 할당
	else
		_reserveCount.fetch_sub(1);
	_useCount.fetch_add(1);
	return memory;
}
```
`Lock`(커스텀 스핀락)을 전혀 안 쓰고, Windows가 제공하는 **`SLIST`(Interlocked Singly-Linked List)** — 하드웨어 원자적 연산(CAS) 기반의 락프리 스택을 쓴다. 여러 스레드가 동시에 `Push`/`Pop`해도 안전하고, 뮤텍스보다 빠르다.

재고(반납된 블록)가 없으면 그때 처음으로 진짜 `_aligned_malloc`을 호출해서 늘려간다 — **미리 왕창 만들어두는 게 아니라 필요할 때마다 자라나는 구조**다. 한 번 만들어진 블록은 프로그램이 끝날 때까지 재사용되며 절대 OS에 반환되지 않는다(`~MemoryPool()`에서만 전부 `_aligned_free`).

`_useCount`/`_reserveCount`는 통계용(지금 몇 개가 대여 중인지, 몇 개가 유휴 상태로 쌓여있는지) — atomic이라 락 없이도 안전하게 집계된다.

---

## ObjectPool<T> — 타입 전용 풀

```cpp
template<typename Type>
class ObjectPool
{
public:
	template<typename... Args>
	static Type* Pop(Args&&... args)
	{
		Type* memory = static_cast<Type*>(MemoryHeader::AttachHeader(s_pool.Pop(), s_allocSize));
		new(memory) Type(forward<Args>(args)...);   // placement new
		return memory;
	}
	static void Push(Type* obj)
	{
		obj->~Type();                                // 소멸자 수동 호출
		s_pool.Push(MemoryHeader::DetachHeader(obj));
	}
	template<typename... Args>
	static shared_ptr<Type> MakeShared(Args&&... args)
	{
		return shared_ptr<Type>{ Pop(forward<Args>(args)...), Push };
	}

private:
	static int32      s_allocSize;   // sizeof(Type) + sizeof(MemoryHeader)
	static MemoryPool s_pool;         // 이 타입 전용 풀 (Memory의 공용 테이블과 별개)
};
```
`Memory`/`Xnew`가 **크기 구간별 공용 풀**이라면, `ObjectPool<T>`는 **타입 하나당 전용 풀**을 따로 만든다(`static MemoryPool s_pool`가 타입별로 인스턴스화됨 — C++ 템플릿의 static 멤버는 타입마다 별개). `SendBuffer`처럼 자주 만들고 부수는 특정 타입을 위해, 다른 타입과 풀을 안 나눠 쓰고 독점하게 하고 싶을 때 쓴다. 둘 다 결국 같은 `MemoryPool`/`MemoryHeader` 메커니즘 위에 얹혀있는 건 동일하다.

---

## 실제 할당 과정 한 번 따라가 보기 — `MakeShared<GameSession>()`

```cpp
shared_ptr<Type> MakeShared(Args&&... args)
{
	return shared_ptr<Type>{ Xnew<Type>(forward<Args>(args)...), Xdelete<Type> };
}

template<typename Type, typename... Args>
Type* Xnew(Args&&... args)
{
	Type* memory = static_cast<Type*>(PoolAllocator::Alloc(sizeof(Type)));
	new(memory) Type(forward<Args>(args)...);   // placement new
	return memory;
}
```

1. `MakeShared<GameSession>()` 호출 → `Xnew<GameSession>()` 실행.
2. `PoolAllocator::Alloc(sizeof(GameSession))` → `GMemory->Allocate(size)`.
3. `Memory::Allocate`가 `size + sizeof(MemoryHeader)`를 계산해서 `_poolTable[allocSize]`로 어느 크기 구간 풀을 쓸지 O(1) 조회.
4. 그 `MemoryPool::Pop()`이 락프리 스택(`SLIST`)에서 재사용 가능한 블록을 꺼내거나, 없으면 새로 `_aligned_malloc`.
5. `MemoryHeader::AttachHeader`로 헤더를 심고 사용자 데이터 위치(헤더 바로 뒤)의 포인터를 돌려줌.
6. `Xnew`가 그 raw 메모리 위에 `placement new`로 `GameSession` 생성자를 실제로 실행.
7. `shared_ptr<GameSession>`이 이 포인터를 감싸는데, **커스텀 디리터로 `Xdelete<GameSession>`**을 등록.
8. 나중에 이 `shared_ptr`의 refcount가 0이 되면: `Xdelete` → `obj->~GameSession()`(소멸자 수동 호출) → `PoolAllocator::Release(obj)` → `Memory::Release` → 헤더에서 크기 확인 → 해당 크기 구간 `MemoryPool::Push()`로 **진짜 OS에 반환하지 않고 풀에 반납**.

이 세션 내내 봐온 `MakeShared<Session>()`, `MakeShared<ServerService>()`, `MakeShared<Listener>()` 전부 다 이 경로를 거친다 — 매번 OS에 `new`/`malloc`을 요청하는 게 아니라, 한 번 쓰고 반납된 같은 크기의 메모리 블록을 계속 재사용하는 구조다.

---

## 정리

| 계층 | 역할 | 특징 |
|---|---|---|
| `BaseAllocator` | 순정 malloc/free | 안 쓰임, 정책 스왑용 |
| `StompAllocator` | 오버플로우 즉시 크래시 | 디버깅 전용(`_STOMP`), 페이지 단위라 메모리 낭비 큼 |
| `PoolAllocator` | `GMemory`로 위임 | 실제로 쓰는 것, `Xnew`/`MakeShared`가 내부적으로 사용 |
| `Memory` | 크기 구간(size-class)별 풀 관리, O(1) 라우팅 | 4096바이트 초과는 풀링 포기하고 일반 할당 |
| `MemoryPool` | 하나의 크기 구간을 위한 락프리 프리 리스트 | Windows `SLIST` 사용, 필요할 때마다 자라남 |
| `ObjectPool<T>` | 타입 하나 전용 풀 | `Memory`와 별개로 존재, `SendBuffer` 등에서 사용 |
