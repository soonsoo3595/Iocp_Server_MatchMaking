# 게임 흐름 정리 — 로그인부터 게임 시작까지

`ServerCore` 엔진 위에 실제로 얹은 게임 로직(`GameServer`/`GameClient`)이 지금 어떻게 동작하는지, 접속의 처음부터 끝까지 한 번에 훑는 문서다. 엔진 내부(IOCP, 메모리, 락, JobQueue 자체)는 이미 `Docs/*_Review.md`에 정리돼 있으니 여기서는 "그 엔진 위에서 게임 로직이 어떤 순서로 실행되는가"에 집중한다.

기획 의도가 궁금하면 `Docs/Matchmaking_Design.md`(구현 전 기획서)를, 엔진 자체가 궁금하면 `Docs/JobQueue_Review.md`/`Docs/PacketFlow_Review.md`를 같이 보면 좋다.

---

## 1. 큰 그림

```
GameClient                                    GameServer
─────────                                     ──────────
(연결) ──────────────────────────────────────▶ GameSession 생성
C_LOGIN ────────────────────────────────────▶ Handle_C_LOGIN
                                                 ├─ GSessionManager (이름 중복 체크)
                                                 ├─ GMmrManager (MMR 부여/조회)
◀──────────────────────────────────────────── S_LOGIN

C_MATCH_START (포지션 지망) ──────────────────▶ Handle_C_MATCH_START
                                                 └─ GMatchmakingManager.AddTicket
◀──────────────────────────────────────────── S_MATCH_QUEUED

                    (서버가 30초마다 혼자 돌아가며 매칭 시도: MatchmakingManager::Tick)

◀──────────────────────────────────────────── S_MATCH_FOUND      (MatchAcceptSession 생성됨)
C_MATCH_ACCEPT / C_MATCH_DECLINE ────────────▶ Handle_C_MATCH_ACCEPT/DECLINE
                                                 └─ MatchAcceptSession.OnAccept/OnDecline
◀──────────────────────────────────────────── S_CHAMPSELECT_START (전원 수락 시, ChampSelectSession 생성됨)
             또는 S_MATCH_QUEUED/S_MATCH_CANCELED (누가 거절/시간초과 시)

C_PICK_CHAMPION / C_CHAT ────────────────────▶ Handle_C_PICK_CHAMPION / Handle_C_CHAT
                                                 └─ ChampSelectSession.OnPick/OnChat
◀──────────────────────────────────────────── S_PICK_UPDATE / S_PICK_FAILED / S_CHAT

◀──────────────────────────────────────────── S_GAME_START        (전원 픽 완료 — 기획 범위 끝)
```

이 프로젝트가 다루는 범위는 딱 여기까지다. `S_GAME_START` 이후의 실제 전투/스킬/승패 판정은 기획서에서부터 범위 밖으로 못 박아뒀다.

---

## 2. 스레드가 몇 개나 돌고 있나

`GameServer.cpp`/`GameClient.cpp`의 `main()`을 보면:

- **서버**: IOCP 워커 스레드가 `std::thread::hardware_concurrency()`개(최소 2개 보장) 돈다. 각 워커는 `service->Dispatch()`(네트워크 I/O 처리 → 패킷 핸들러 실행) → `ThreadManager::DistributeReservedJobs()`(타이머로 예약된 잡 실행) → `ThreadManager::DoGlobalQueueWork()`(밀린 JobQueue 대신 처리)를 반복한다.
- **클라이언트**: IOCP 워커 스레드 1개(네트워크 I/O 전담) + 메인 스레드 1개(콘솔 메뉴 루프 `RunConsoleMenuLoop`).

즉 **핸들러 함수(`Handle_C_LOGIN` 등)는 항상 여러 워커 스레드 중 하나에서, 어떤 순서로 겹쳐 들어올지 모르는 채로 실행된다.** 그래서 이 문서에서 계속 나오는 `MatchmakingManager`/`MatchAcceptSession`/`ChampSelectSession`이 전부 `JobQueue`를 상속받는 이유가 여기 있다 — 자세한 원리는 `Docs/JobQueue_Review.md`를 보면 되고, 여기서는 "핸들러는 절대 상태를 직접 안 건드리고 `DoAsync`로 잡을 던지기만 한다"는 규칙만 기억하면 된다.

```cpp
// Handle_C_MATCH_START 안
GMatchmakingManager->DoAsync(&MatchmakingManager::AddTicket, ticket);
```

`AddTicket` 함수 내부는 그냥 평범한 싱글스레드 코드처럼 짜여 있다(락 없음). `GMatchmakingManager`라는 큐 하나에 대해서는 어느 순간이든 딱 한 스레드만 잡을 처리한다고 `JobQueue`가 보장해주기 때문이다.

---

## 3. 1단계 — 접속과 로그인

**서버**: `GameSession::OnConnected()`가 `GSessionManager.Add(...)`로 세션을 등록한다. 아직 로그인은 안 한 상태 — `GameSession::_player`가 비어있다.

**클라**: `ServerSession::OnConnected()`에서 콘솔로 입력받은 닉네임을 `C_LOGIN`에 실어 보낸다.

```cpp
wstring wideName;
std::getline(wcin, wideName);
WSTR_TO_UTF8(wideName, GLoginName);   // 콘솔 입력(로캘) -> UTF-8 (protobuf string은 항상 UTF-8)
```

**서버 `Handle_C_LOGIN`**:
1. 이미 로그인된 세션이면 거부 (재로그인 시 이름 누수 방지)
2. 닉네임 트림/검증(`StringUtils`) + `GSessionManager.TryReserveName()`으로 중복 체크
3. 통과하면 `GMmrManager.GetOrCreateMmr(name)`으로 MMR을 부여받는다 — **DB가 없어서 닉네임 최초 등장 시 800~1600 사이 랜덤값을 주고, 서버가 살아있는 동안은 그 값을 계속 돌려준다.**
4. `Player` 객체를 만들어 `GameSession::_player`에 연결하고 `S_LOGIN` 응답

MMR은 `Player::mmr` 필드에만 있고, **어떤 패킷에도 실리지 않는다** — 이건 이 프로젝트 전체를 관통하는 규칙이다(§7 참고).

---

## 4. 2단계 — 매치메이킹 신청

**클라 로비 메뉴**(`RunLobbyMenu`)에서 "매칭 시작"을 고르면 1지망/2지망 포지션을 순서대로 물어보고(`SelectPosition`), `C_MATCH_START`로 전송한다. 1지망이 "상관없음"이면 2지망은 아예 안 물어본다.

**서버 `Handle_C_MATCH_START`**는 `gameSession->_player`의 정보(playerId, mmr, name)와 요청받은 포지션을 묶어 `MatchmakingTicket`을 만들고, `GMatchmakingManager`에 `DoAsync`로 던진 뒤 `S_MATCH_QUEUED`를 바로 돌려준다(성사 여부와 무관하게 "등록됐다"는 사실만 알려줌 — 기획서 결정).

```cpp
struct MatchmakingTicket
{
    uint64 playerId; uint32 mmr; string name;
    Protocol::Position primaryPosition, secondaryPosition;
    uint64 queuedAt;
    weak_ptr<GameSession> session;   // 나중에 통보할 대상 (약한 참조)
};
```

### 왜 `MatchmakingManager`가 MMR을 버킷으로 나눠서 들고 있나

`MatchmakingManager::_bucketedTickets`는 `mmr / 100` 값으로 인덱싱되는 벡터의 벡터다. 도서관에서 책을 청구기호 구간별로 책장에 나눠 꽂아두면 "300번대 책 찾기"가 서가 하나만 훑으면 되는 것과 같은 이유 — "이 MMR ±200 안의 후보 다 모아줘"라는 질의(`CollectCandidates`)가 전체 대기열을 훑지 않고 근처 버킷 몇 개만 보면 되게 하려는 것이다. 지금 규모(테스트용 클라 몇 개)에서는 사실 체감 차이가 없지만, 구조 자체는 확장성 있게 짜여 있다.

---

## 5. 3단계 — 매칭 알고리즘 (`MatchmakingManager::Tick`)

`InitMatchmakingManager()`가 서버 시작 시 `Tick()`을 한 번 예약해두면, 그 뒤로는 `Tick()` 자신이 매번 끝에서 `DoTimer(30000, &MatchmakingManager::Tick)`로 30초 뒤 자신을 재예약한다 — `JobTimer`는 "N초 뒤 한 번"만 지원하기 때문에 반복은 이렇게 자가 재예약으로 구현한다.

`Tick()` 한 번이 하는 일:

1. 버킷에 흩어진 티켓을 전부 펼쳐서 **대기 시간이 긴 순서**로 정렬 (오래 기다린 사람부터 매칭 기회를 줌)
2. 각 티켓을 "씨앗"으로 삼아 `CollectCandidates(mmr, ±200)`로 근처 후보를 모음
3. 후보가 10명이 안 되면 이번엔 포기, 있으면 `TryFormMatch`로 포지션이 꽉 찬 5vs5를 시도

### 포지션 채우기 + 팀 밸런싱을 같이 푸는 방법

기획서(§8)가 스스로 "열린 문제"로 남겨뒀던 부분이다 — "MMR 순으로 정렬해서 지그재그로 팀 나누기"와 "팀마다 포지션 5개가 정확히 하나씩" 두 조건이 동시에 만족되기 까다롭다. 여기서는:

- 포지션 하나(TOP/JUG/MID/BOT/SUP)마다 후보를 **1지망 → 2지망 → 상관없음** 우선순위로 2명 뽑고
- 그 2명 중 MMR이 높은 쪽을 **지금까지 누적 MMR 합이 더 낮은 팀**에 배정

포지션 단위로 "한 쌍 뽑아서 즉시 배분"하는 방식이라, 각 팀에 포지션이 정확히 하나씩 채워지는 게 구조적으로 보장되면서 동시에 팀 간 MMR 격차도 매 포지션마다 좁혀진다.

매치가 성사되면 두 팀 10명의 티켓을 큐에서 제거(`RemoveTicket`)하고, 실제 통보는 `MatchAcceptSession`에 넘긴다(`FinalizeMatch`) — `Tick()`은 매칭 "성립"까지만 책임지고, 그 다음(수락 대기)은 별도 객체가 맡는다.

---

## 6. 4단계 — 매치 수락/거절 (`MatchAcceptSession`)

원래 기획서는 "매치 성사 즉시 자동으로 챔피언 선택 진입(수락 단계 없음)"이었는데, 이후 사용자 요청으로 **의도적으로 뒤집힌 부분**이다. LoL처럼 수락/거절을 물어보고, 한 명이라도 거절(또는 15초 타임아웃)하면 수락했던 사람만 원래 지망을 유지한 채 재매칭 큐로 돌려보낸다.

`MatchAcceptSession`은 **매치 하나당 하나씩 생기는 임시 객체**다(`MatchmakingManager`처럼 전역 하나가 아니라 `FinalizeMatch`에서 그때그때 `make_shared`). `Start()`가 하는 일:

1. 10명에게 `S_MATCH_FOUND`(내 팀/상대 팀/내 포지션) 전송
2. **각 `GameSession::_matchAcceptSession`에 자기 자신(`shared_ptr`)을 등록** — 이게 핵심 패턴이다 (아래 §9 참고)
3. `DoTimer(15000, &MatchAcceptSession::OnTimeout)`로 15초 타임아웃 예약

`Handle_C_MATCH_ACCEPT`/`Handle_C_MATCH_DECLINE`은 `gameSession->_matchAcceptSession.lock()`으로 "지금 이 세션이 응답을 기다리는 중인 그 세션"을 찾아서 `DoAsync`로 넘길 뿐이다 — 어떤 매치인지 ID를 따로 안 넘겨도, GameSession에 등록해둔 참조를 따라가면 되니까 간단하다.

- **10명 전원 수락** → `Resolve(true)` → `ChampSelectSession` 생성 후 `Start()`로 위임
- **한 명이라도 거절 / 15초 초과** → `Resolve(false)` → 수락했던 사람은 **원래 티켓 그대로**(포지션/MMR/`queuedAt` 유지 — 즉 대기 시간 손해를 안 봄) 다시 `AddTicket`, 안 한 사람은 `S_MATCH_CANCELED`로 로비 복귀

---

## 7. 5단계 — 챔피언 선택 (`ChampSelectSession`)

`MatchAcceptSession::Resolve(true)`가 만들어서 `Start()`를 부른다. 하는 일은 `MatchAcceptSession`과 판박이 패턴이다 — `S_CHAMPSELECT_START` 전송 + 각 `GameSession::_champSelectSession`에 자기 등록.

**픽 처리** (`OnPick`):
1. 본인이 이미 픽했으면 실패
2. 다른 사람이 이미 그 챔피언을 픽했으면 실패 (팀 안/밖 구분 없이 전역 중복 금지 — 기획서 §5.2)
3. 통과하면 상태 갱신 후 참가자 10명 **전원**에게 `S_PICK_UPDATE` 브로드캐스트

실패하면 본인에게만 `S_PICK_FAILED`가 가고, 클라는 다시 챔피언 선택 화면으로 돌아간다.

**채팅**(`OnChat`)도 같은 세션 안에서 처리된다 — 지금은 "챔피언 선택 중"일 때만 채팅이 되는데, `Handle_C_CHAT`이 `gameSession->_champSelectSession`을 찾아서 라우팅하는 구조라 그렇다(로비/대기열에는 묶여있는 세션 그룹이 없어서 무시됨).

**완료 판정**(`CheckAllPicked`): 10명 전원의 `pickedChampionId`가 0이 아니게 되면, 각자의 최종 팀 구성(포지션+챔피언)을 담은 `S_GAME_START`를 전원에게 보내고 `_champSelectSession` 참조를 정리한다. **여기서 이 프로젝트의 매치메이킹 흐름이 끝난다.**

---

## 8. 클라이언트 상태 머신

클라는 `ClientState`(Atomic) 하나로 지금 화면을 관리한다. 서버가 보내는 패킷이 곧 상태 전환 신호다:

```
LOGOUT ─(S_LOGIN 성공)─▶ LOBBY ─(C_MATCH_START)─▶ MATCHING
   ▲                                                  │
   │                                          (S_MATCH_FOUND)
   │                                                  ▼
   └─(S_MATCH_CANCELED)──────────────────── MATCH_FOUND
                                                       │ (수락/거절 응답)
                                                       ▼
                                          WAITING_ACCEPT_RESULT
                                    ┌──────────────────┴──────────────────┐
                            (S_MATCH_QUEUED)                    (S_CHAMPSELECT_START)
                                    ▼                                      ▼
                                MATCHING                            CHAMP_SELECT
                                                                           │ (픽 전송)
                                                                           ▼
                                                                  WAITING_GAME_START
                                                            (S_PICK_FAILED)│  │(S_GAME_START)
                                                          CHAMP_SELECT◀────┘  ▼
                                                                        GAME_STARTED
```

메인 스레드(`RunConsoleMenuLoop`)는 지금 상태에 맞는 메뉴 함수를 부르거나(`LOBBY`→`RunLobbyMenu` 등), 서버 응답을 기다리는 중이면 그냥 짧게 자면서(`sleep_for(100ms)`) 상태가 바뀌길 기다린다. 상태 전환 자체는 네트워크 워커 스레드(`Handle_S_*`)가 하므로 `ClientState`는 `Atomic`이어야 한다(§2).

---

## 9. 이 프로젝트를 관통하는 설계 패턴 3가지

**① "지금 담당 세션"을 `weak_ptr`로 GameSession에 등록해두는 릴레이 패턴**

`_matchAcceptSession`, `_champSelectSession` 둘 다 같은 모양이다 — 어떤 임시 세션 객체(`MatchAcceptSession`/`ChampSelectSession`)가 자기 참가자들의 `GameSession`에 "지금 나한테 연결돼있다"는 걸 등록해두면, 이후 그 플레이어가 보내는 패킷은 `gameSession->_xxx.lock()`으로 바로 그 세션을 찾아서 넘길 수 있다. 매치 ID를 매번 패킷에 실어 보내고 서버가 그걸로 세션을 찾아 헤맬 필요가 없다 — 우편함에 "지금 담당자는 이 사람" 스티커를 붙여두는 것과 비슷하다. 담당이 바뀌면(수락 완료 → 챔피언 선택으로) 스티커만 새로 붙이면 된다(`.reset()` 후 재등록).

약한 참조(`weak_ptr`)를 쓰는 이유도 일관적이다 — 세션 객체가 참가자의 생명주기를 붙들면 안 되고(접속 끊겨도 정리돼야 함), 반대로 `GameSession`도 임시 세션의 생명주기를 붙들면 안 된다(매치 하나 끝나면 사라져야 함).

**② MMR은 절대 패킷에 안 실림**

`Player::mmr`, `MatchmakingTicket::mmr`, `ChampSelectParticipant`엔 MMR이 없다 — `PlayerInfo` proto 메시지에도 `mmr` 필드 자체가 없다. 매칭 계산은 전부 서버 프로세스 안에서만 일어나고, 클라이언트가 자기 MMR을 서버로 보내는 경로 자체가 없다(치팅 방지).

**③ 티켓/참가자 정보는 값으로 복사해서 들고 다님**

`MatchmakingTicket`, `MatchedPlayer`, `ChampSelectParticipant` 전부 `PlayerRef`(강한 참조)를 직접 들고 있지 않고 `playerId`/`mmr`/`name` 등을 값으로 복사해서 가진다. `Player`/`GameSession`의 생명주기와 매칭 로직의 생명주기를 분리해서, 예전에 있었던 `Player`↔`GameSession` 순환 참조 같은 문제가 재발하지 않게 하려는 설계다.

---

## 10. 알려진 빈틈

`CLAUDE.md`의 "알려진 이슈"에도 있는 내용:

- **연결 끊김 처리 미비** — 대기/수락/픽 도중 접속이 끊기면 `weak_ptr.lock()`이 실패해서 그 사람에게 통보만 조용히 스킵될 뿐, 남은 팀원에게 알리거나 대기열 티켓을 정리하는 로직은 없다.
- **챔피언 선택에 타임아웃 없음** — 매치 수락은 15초 제한이 있는데 픽은 무제한 대기라, 한 명이 안 뽑으면 나머지 9명이 계속 기다리게 된다.

두 가지 다 기획서 §5.4가 "확장 과제"로 명시적으로 남겨둔 범위라, 알고 있는 상태로 보류 중이다.
