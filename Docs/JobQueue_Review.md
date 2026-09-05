# Job / JobQueue / GlobalQueue / JobTimer 학습 정리

`Room`/`GameSession` 같은 게임 로직 객체가 **락 없이** 안전하게 동작하는 이유가 이 시스템에 있다. "여러 워커 스레드가 같은 객체를 동시에 건드릴 수 있다"는 문제를, 락으로 매번 막는 대신 **"이 객체에 대한 작업은 항상 한 번에 한 스레드만 처리하게" 스케줄링**하는 방식으로 해결한다.

---

## 전체 그림

```
Job          — "나중에 실행할 일 하나" (콜백 or 멤버함수+인자)
LockQueue<T> — Job(등)을 담는 스레드 세이프 큐 (WRITE_LOCK 래퍼)
JobQueue     — 한 객체(Room 등)가 상속받아 쓰는 큐. "책임자 한 명만 처리" 스케줄링의 핵심
GlobalQueue  — "지금 처리할 게 있는 JobQueue들"의 대기열. 워커 스레드들이 여기서 꺼내감
JobTimer     — "N틱 뒤에 실행" 예약. 시간이 되면 대상 JobQueue에 Push
```

---

## Job — 나중에 실행할 일 하나

```cpp
using CallbackType = std::function<void()>;

class Job
{
public:
	Job(CallbackType&& callback) : _callback(std::move(callback)) {}

	// shared_ptr을 유지하기 위해
	template<typename T, typename Ret, typename... Args>
	Job(shared_ptr<T> owner, Ret(T::* memFunc)(Args...), Args&&... args)
	{
		_callback = [owner, memFunc, args...]()
			{
				(owner.get()->*memFunc)(args...);
			};
	}

	void Execute() { _callback(); }
private:
	CallbackType _callback;
};
```
`std::function<void()>` 하나를 들고 있다가 `Execute()`가 불리면 실행하는 단순한 래퍼. "콜백 하나"든 "어떤 객체의 멤버함수 + 인자들"이든 전부 `void()` 시그니처로 타입 소거해서, 큐 쪽은 이게 원래 뭐였는지 몰라도 된다.

**두 번째 생성자의 `[owner, ...]` 값 캡처가 핵심이다.** 람다가 `owner`(`shared_ptr<T>`)를 값으로 캡처하면, 이 `Job`이 살아있는 동안 대상 객체에 대한 강한 참조를 계속 붙잡는다 — `IocpEvent::SetOwner(shared_from_this())`(ADD_REF)가 "커널이 처리 중인 동안 객체가 먼저 사라지지 않게" 막아줬던 것과 같은 문제를, 여기선 수동 AddRef/Release 없이 람다 값 캡처 하나로 해결한 것.

---

## LockQueue<T> — 스레드 세이프 큐

```cpp
template<typename T>
class LockQueue
{
public:
	void Push(T item) { WRITE_LOCK; _items.push(item); }
	T Pop()
	{
		WRITE_LOCK;
		if (_items.empty()) return T();
		T ret = _items.front(); _items.pop(); return ret;
	}
	void PopAll(OUT Vector<T>& items)
	{
		WRITE_LOCK;
		while (T item = Pop())   // 자기 자신의 WRITE_LOCK 안에서 또 Pop()의 WRITE_LOCK을 잡음
			items.push_back(item);
	}
private:
	USE_LOCK;
	Queue<T> _items;
};
```
`PopAll()`이 자기 락을 잡은 채로 내부에서 `Pop()`을 또 부르는데, 이게 안전한 이유는 `Lock`의 **재진입 허용**(같은 스레드가 이미 쥔 Write 락을 또 잡으면 그냥 카운트만 올리고 통과) 덕분이다.

`while (T item = Pop())`은 `T()`(기본 생성값)를 "비어있음" 신호로 쓴다 — `JobRef`(`shared_ptr<Job>`)라면 `T()`는 `nullptr`이고, `shared_ptr`은 bool로 암시적 변환되니 자연스럽게 동작한다.

---

## JobQueue — "책임자 한 명만 처리" 스케줄링

### `Push()` — 누가 처리할지 정하기
```cpp
void JobQueue::Push(JobRef job, bool pushOnly)
{
	const int32 prevCount = _jobCount.fetch_add(1);   // push 전의 개수
	_jobs.Push(job);

	if (prevCount == 0)   // 내가 "빈 큐 -> 일감 있는 큐"로 전환시킨 장본인
	{
		if (LCurrentJobQueue == nullptr && pushOnly == false)
			Execute();                                 // 지금 이 스레드가 바로 처리
		else
			GGlobalQueue->Push(shared_from_this());     // 다른 스레드가 나중에 처리하도록 위임
	}
	// prevCount > 0: 이미 누군가 처리 중(또는 처리 예정)이니 그냥 넣고 끝
}
```
`Session::Send()`의 `_sendRegistered.exchange(true)==false` 게이트와 완전히 같은 발상 — `_jobCount`가 0에서 1로 바뀌는 걸 본 딱 한 스레드만 "이 큐를 처리할 책임"을 지고, 나머지는 그냥 넣고 리턴한다. `JobQueue`마다 전담 스레드를 따로 둘 필요 없이, 누구든 먼저 일감을 만든 스레드가 처리하거나 위임하는 구조.

- `LCurrentJobQueue == nullptr`(지금 이 스레드가 다른 JobQueue를 처리 중이 아님) **이고** `pushOnly == false`면 → 그 자리에서 즉시 `Execute()` (지연 없음).
- 아니면(이미 다른 큐 처리 중 — 재진입 방지 — 이거나 호출자가 명시적으로 "넣기만 해"라고 요청) → `GlobalQueue`로 위임.

### `Execute()` — 한 스레드가 독점 처리
```cpp
void JobQueue::Execute()
{
	LCurrentJobQueue = this;   // "나 지금 이 큐 처리 중" 표시

	while (true)
	{
		Vector<JobRef> jobs;
		_jobs.PopAll(OUT jobs);          // 쌓인 걸 통째로 꺼냄

		for (auto& j : jobs)
			j->Execute();                 // 순차 실행 (이 스레드 안에서만!)

		if (_jobCount.fetch_sub(jobCount) == jobCount)   // 처리한 만큼 뺐는데 정확히 0이 됐으면
		{
			LCurrentJobQueue = nullptr;
			return;   // 그 사이 아무도 새 잡을 안 넣었다 -> 종료
		}

		if (now >= LEndTickCount)   // 시간 예산(WORKER_TICK=64ms) 초과
		{
			LCurrentJobQueue = nullptr;
			GGlobalQueue->Push(shared_from_this());   // 남은 건 넘기고 손 뗌
			break;
		}
		// 예산 남았으면 계속 루프 돌며 새로 들어온 것도 마저 처리
	}
}
```
`_jobCount.fetch_sub(jobCount) == jobCount` 체크가 핵심: "빼기 전의 값이 정확히 방금 처리한 개수와 같았다"면 처리하는 동안 아무도 새로 안 넣었다는 뜻이라 종료. 다르면(처리 도중 다른 스레드가 더 넣었으면) 계속 루프를 돌아 이어서 처리하되, 시간 예산을 넘기면 강제로 손 떼고 `GlobalQueue`에 넘겨 다른 스레드가 이어받게 한다 — 한 큐에 일감이 끝없이 밀려도 워커 스레드 하나가 거기 영원히 묶이지 않게 하는 안전장치.

### 왜 락이 필요 없어지는가
서로 다른 `JobQueue`(예: 다른 `Room`)들은 완전히 다른 스레드에서 동시에 병렬 처리될 수 있다. 근데 **같은 `JobQueue` 하나**에 대한 잡들은 `prevCount==0` 게이트 덕분에 **항상 딱 한 스레드가 한 번에 하나씩만** 실행한다 — 애초에 동시에 두 스레드가 같은 객체를 건드릴 수 없는 구조라, 그 객체 내부 상태를 락으로 보호할 필요 자체가 사라진다.

---

## GlobalQueue — "처리 대기 중인 JobQueue들"의 큐

```cpp
class GlobalQueue
{
public:
	void Push(JobQueueRef jobQueue) { _jobQueues.Push(jobQueue); }
	JobQueueRef Pop() { return _jobQueues.Pop(); }
private:
	LockQueue<JobQueueRef> _jobQueues;
};
```
그냥 `LockQueue<JobQueueRef>` 래퍼다. `ThreadManager::DoGlobalQueueWork()`(워커 루프 안에서 매 틱 호출)가 여기서 하나씩 꺼내 `Execute()`를 불러준다:
```cpp
void ThreadManager::DoGlobalQueueWork()
{
	while (true)
	{
		if (::GetTickCount64() > LEndTickCount) break;   // 시간 예산 체크
		JobQueueRef jobQueue = GGlobalQueue->Pop();
		if (jobQueue == nullptr) break;
		jobQueue->Execute();
	}
}
```

---

## JobTimer — 지연 실행 예약

### `JobData`가 `weak_ptr`을 쓰는 이유
```cpp
struct JobData
{
	weak_ptr<JobQueue>	owner;   // ★ shared_ptr이 아니라 weak_ptr
	JobRef				job;
};
```
`Job`의 멤버함수 생성자는 대상을 `shared_ptr`로 붙잡아서 "실행될 때까지 살려두지만", 그건 `DoAsync`처럼 이번 틱 안에 곧 실행될 짧은 지연에나 적합하다. 타이머는 몇 초~몇 분 뒤일 수도 있는데, 그동안 `Room` 같은 대상을 `shared_ptr`로 강제로 붙잡아두면 **아무도 안 쓰는 객체가 오직 "언젠가 울릴 타이머 하나" 때문에 계속 살아있는** 문제가 생긴다. 그래서 `weak_ptr`로 "그때까지 살아있으면 실행, 이미 사라졌으면 조용히 버림"으로 처리한다:
```cpp
if (JobQueueRef owner = item.jobData->owner.lock())
	owner->Push(item.jobData->job);
// lock() 실패하면 아무 일도 안 하고 지나감 — 에러 아님, 정상 흐름
```

### 최소 힙(min-heap) 트릭
```cpp
struct TimerItem
{
	bool operator<(const TimerItem& other) const
	{
		return executeTick > other.executeTick;   // 부등호를 뒤집음
	}
	uint64 executeTick = 0;
	JobData* jobData = nullptr;   // raw 포인터 — 자주 복사되므로 refcount 비용을 피함
};
PriorityQueue<TimerItem> _items;
```
`std::priority_queue`는 기본이 최대 힙인데 `operator<`를 뒤집어서, `top()`이 항상 **실행 시각이 가장 이른 항목**이 되게 만든다. `jobData`가 스마트포인터가 아니라 raw 포인터인 이유는, `TimerItem`이 힙 push/pop과 벡터 복사 과정에서 자주 복사되는데 그때마다 원자적 refcount 연산이 들어가는 걸 피하려는 것 — 대신 `ObjectPool<JobData>`로 직접 풀링해서 생명주기를 관리한다.

### `Distribute(now)` — 매 틱마다 "때 된 것" 꺼내기
```cpp
void JobTimer::Distribute(uint64 now)
{
	if (_distributing.exchange(true) == true)
		return;   // 이미 누가 분배 중이면 그냥 포기 (다음 틱에 다시 시도됨)

	Vector<TimerItem> items;
	{
		WRITE_LOCK;
		while (_items.empty() == false)
		{
			const TimerItem& timerItem = _items.top();
			if (now < timerItem.executeTick)
				break;   // 제일 이른 것도 아직이면 나머지도 볼 필요 없음 (힙 성질)
			items.push_back(timerItem);
			_items.pop();
		}
	}

	for (TimerItem& item : items)
	{
		if (JobQueueRef owner = item.jobData->owner.lock())
			owner->Push(item.jobData->job);      // 대상의 JobQueue로 정상 편입
		ObjectPool<JobData>::Push(item.jobData);  // JobData는 풀로 반납
	}

	_distributing.store(false);
}
```
`_distributing` 게이트도 `_sendRegistered`/`JobQueue::_jobCount`와 같은 계열의 "한 명만 승자" 패턴이다. 다만 여기선 진 쪽이 큐에 넣고 승자를 믿는 게 아니라 **그냥 포기**한다 — `Distribute`는 모든 워커 스레드가 매 틱(64ms)마다 반복 호출하니, 이번에 못 하면 다음 틱에 다른 스레드가 다시 시도하면 그만이라 괜찮다.

---

## 한 사이클 전체 흐름 — `room->DoTimer(5000, &Room::SomeCleanup)` 예시

```
1. Job + JobData(weak_ptr owner) 생성, JobTimer의 힙에 예약
2. 매 워커 틱마다 DistributeReservedJobs() -> JobTimer::Distribute() 호출
3. 5초 뒤: 시각이 된 항목 발견, owner.lock()으로 Room이 아직 살아있는지 확인
4. 살아있으면 Room의 JobQueue::Push()로 편입
5. Push()가 "책임자"를 정함: 유휴 스레드면 즉시 Execute(), 아니면 GlobalQueue로 위임
6. 결국 이 Room을 담당하게 된 스레드가 Execute()로 SomeCleanup()을 락 없이, 안전하게 실행
   (그동안 이 Room에 대한 다른 DoAsync 요청들도 같은 큐에 쌓여 순서대로 같이 처리됨)
```

## 정리

| | 역할 | 핵심 패턴 |
|---|---|---|
| `Job` | 나중에 실행할 일 하나 | 멤버함수+인자를 람다로 타입 소거, `shared_ptr` 값 캡처로 생명주기 보장 |
| `LockQueue<T>` | 스레드 세이프 큐 | `WRITE_LOCK` + 재진입 활용 |
| `JobQueue` | 한 객체 전용 잡 큐 | "첫 push한 스레드가 책임자" 게이트로 락 없는 순차 실행 보장 |
| `GlobalQueue` | 대기 중인 JobQueue들의 큐 | 워커 스레드들이 나눠 가져가는 공용 창구 |
| `JobTimer` | 지연 실행 예약 | `weak_ptr`로 대상 생명주기 강제 안 함, min-heap으로 효율적 조회 |
