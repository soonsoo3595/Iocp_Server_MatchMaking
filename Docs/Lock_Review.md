# Lock / DeadLockProfiler 학습 정리

`Lock`은 `std::mutex`가 아니라 직접 구현한 **Reader-Writer 스핀락**이고, `DeadLockProfiler`는 그 락들 사이의 "위험한 순서"를 실행 중에 잡아내는 디버깅 도구다. Debug 빌드에서만 켜진다.

---

## Lock — Reader-Writer 스핀락

### 핵심 아이디어: 32비트 하나에 두 가지 정보
```
Atomic<uint32> _lockFlag;
[WWWWWWWW][WWWWWWWW][RRRRRRRR][RRRRRRRR]
 상위 16비트: 지금 Write 락을 쥔 스레드 ID (0이면 아무도 안 쥠)
 하위 16비트: 지금 Read 락을 쥔 개수
```
락 상태 전체를 정수 하나로 표현해서, CAS(compare-and-swap) 한 번으로 상태 전이를 원자적으로 처리한다.

### WriteLock — 재진입 가능한 배타 락
```cpp
if (LThreadId == lockThreadId)   // 이미 내가 쥐고 있으면
{
    _writeCount++;                // 재진입 카운트만 올리고 리턴
    return;
}
// 남이면: CAS로 EMPTY_FLAG(완전히 0)일 때만 획득 시도
// 스핀 5000번 -> 실패하면 yield -> 다시 스핀, 10초 넘으면 타임아웃
```
`EMPTY_FLAG`일 때만 성공하므로, **Write 락은 다른 Writer뿐 아니라 다른 Reader가 하나라도 있어도 못 얻는다** — 완전 배타적.

### ReadLock — 여럿이 동시에 가질 수 있는 공유 락
```cpp
if (LThreadId == lockThreadId)    // 내가 Write 락을 쥔 상태라면
{
    _lockFlag.fetch_add(1);        // Read count만 올리고 통과
    return;
}
// 남이면: 아무도 Write 안 하고 있을 때만 Read count를 +1 (여러 스레드가 동시에 성공 가능)
```

### 규칙: `W → R` 허용, `R → W` 금지
클래스 상단 주석 그대로다.
- **W → R (허용)**: Write 락을 쥔 스레드가 그 위에 Read 락을 추가로 잡을 수 있다. `WriteUnlock`은 이걸 강제로 순서 지키게 한다 — 중첩된 Read를 다 풀기 전엔 Write를 못 푼다(`READ_COUNT_MASK`가 0이 아니면 크래시).
- **R → W (금지)**: Read 락만 쥔 상태에서 같은 스레드가 Write로 "승급"하려 하면, `WriteLock`의 재진입 체크(`LThreadId == lockThreadId`, Write 소유자만 인식)를 통과 못 하고 일반 획득 경로로 빠진다. 근데 그 경로는 `_lockFlag`가 완전히 0(`EMPTY_FLAG`)이어야 성공하는데, **자기 자신이 쥔 Read 카운트 때문에 절대 0이 될 수 없다** — 결국 자기 자신 때문에 스스로 데드락에 빠져서 타임아웃 크래시가 난다.

**W→R이 필요한 실제 시나리오 예시** (지금 코드에 실제로 있는 패턴은 아니지만, 있었다면 이렇게 걸렸을 것):
```cpp
// Service.h
int32 GetCurrentSessionCount() { READ_LOCK; return static_cast<int32>(_sessions.size()); }

// 만약 AddSession이 로그를 찍을 때 이 접근자를 재사용했다면:
void Service::AddSession(SessionRef session)
{
	WRITE_LOCK;
	_sessions.insert(session);
	LOG_VERBOSE(L"... count=%d", GetCurrentSessionCount());  // 이미 WRITE_LOCK 쥔 채로 READ_LOCK 호출
}
```
"읽기 전용"이라고 짜둔 공용 함수를, 이미 Write 락을 쥔 다른 함수 내부에서 재사용하고 싶을 때가 W→R이 필요한 전형적인 상황이다.

### `USE_LOCK`/`WRITE_LOCK`/`READ_LOCK` 매크로
```cpp
#define USE_LOCK          Lock _locks[1];
#define WRITE_LOCK         WriteLockGuard writeLockGuard_0(_locks[0], typeid(this).name());
#define READ_LOCK          ReadLockGuard readLockGuard_0(_locks[0], typeid(this).name());
```
`Service`/`Session`/`Listener` 등에서 계속 봤던 `WRITE_LOCK;`이 이거였다 — RAII 가드 객체가 생성자에서 락을 잡고 스코프를 벗어나면 소멸자에서 자동 unlock. `typeid(this).name()`으로 클래스 이름을 락 이름으로 써서 프로파일러 식별에 쓴다.

### 발견/수정한 문제
- **크래시 직전에 로그가 없었다**: `LOCK_TIMEOUT`/`INVALID_UNLOCK_ORDER`/`MULTIPLE_UNLOCK` 전부 `LOG_FATAL`(락 이름 + 스레드 ID)을 추가해서, 크래시 로그만 보고도 원인을 추적할 수 있게 함.
- **`WriteUnlock`이 짝 안 맞는 호출을 방어 안 함**: `_writeCount`(uint16)가 0인 상태에서 `WriteUnlock()`이 한 번 더 불리면 언더플로우(65535)로 조용히 락이 고장 나던 문제 — `ReadUnlock`처럼 `_writeCount == 0`이면 `CRASH`하도록 방어 추가.
- 원래 CAS 루프 이전에 있던 비-원자적 초안 주석 코드는 정리 완료.

---

## DeadLockProfiler — 데드락을 "터지기 전에" 잡아내는 도구

### 핵심 아이디어: "누가 누구 다음에 잡혔는지"를 그래프로 누적
```cpp
map<int32, set<int32>>  _lockHistory;   // A -> {B, C, ...} : "A를 쥔 채로 B/C를 잡은 적 있다"
thread_local stack<int32> LLockStack;   // 이 스레드가 지금 겹쳐 쥐고 있는 락들 (스레드별 TLS)
```

### PushLock — 락을 잡을 때마다
```cpp
if (LLockStack.empty() == false)              // 내가 이미 뭔가 쥐고 있다면
{
	const int32 prevId = LLockStack.top();     // 직전에 잡은 락
	if (처음 보는 조합(prevId -> lockId))
	{
		history.insert(lockId);                 // 간선 기록
		CheckCycle();                            // 즉시 사이클 검사
	}
}
LLockStack.push(lockId);
```
새로운 조합이 나올 때만 그래프에 간선을 추가하고 검사한다 — 같은 조합을 반복 잡는 흔한 경우엔 매번 검사하지 않아 낭비를 줄인다.

### CheckCycle / Dfs — 방향 그래프에서 사이클 찾기
표준 DFS 기반 사이클 탐지(`_discoveredOrder`/`_finished`/`_parent`로 방문 상태 관리). 아직 탐색이 안 끝난(=현재 DFS 경로상의 조상) 노드로 돌아오는 간선(back edge)을 찾으면 사이클 → 그 경로를 출력하고 크래시.

### 왜 실제로 안 멈췄는데도 잡아내는가
데드락은 보통 두 스레드의 타이밍이 절묘하게 겹쳐야 재현된다(스레드1 "A잡고 B기다림" / 스레드2 "B잡고 A기다림"이 **동시에** 일어나야 함). 근데 이 프로파일러는 동시에 안 걸려도 잡아낸다: 스레드1이 (어느 시점에) "A→B" 순서로 잡은 이력이 그래프에 남고, 전혀 다른 시점에 스레드2가 "B→A" 순서로 잡으면, 그 순간 그래프에 두 방향 간선이 다 생겨서 사이클이 만들어진다. **"이 두 락을 서로 다른 순서로 잡는 코드가 어딘가에 공존한다"는 사실 자체가 잠재적 데드락**이라, 두 스레드가 실제로 경합하지 않아도 이 패턴이 처음 실행되는 순간 바로 잡아낸다.

### 트레이드오프
`PushLock`/`PopLock`이 매번 프로파일러 전용 전역 `Mutex`를 잡는다 — 프로그램의 **모든 스레드, 모든 `Lock` 사용처**가 이 하나의 뮤텍스를 거치게 되어, Debug 빌드에서는 이 스핀락 시스템 전체가 사실상 하나의 전역 락으로 직렬화되는 셈이다. 동접이 많은 스트레스 테스트를 Debug로 돌리면 병목이 될 수 있는데, 이건 디버깅 도구의 의도된 트레이드오프(정확성/추적 vs 성능)로 보는 게 맞다 — Release 빌드에선 통째로 컴파일에서 빠진다.

### 실무 관점
실제 데드락은 "어디서 멈췄는지"만 로그에 남고 원인 추적이 어려운 경우가 많은데, 이 방식은 "위험한 락 순서"가 최초로 실행되는 순간 확정적으로 잡아낸다는 점에서 정적 분석 도구(Clang Thread Safety Analysis)나 다른 런타임 락 프로파일러들과 같은 발상이다. `CRASH`로 죽는 설계 자체도 이 코드베이스 전반의 원칙("버그를 숨기지 않고 최대한 빨리, 시끄럽게 드러낸다")과 일치한다.
