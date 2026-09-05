# ServerCore 학습 정리

`ServerCore`의 IOCP 네트워크 계층을 하나씩 뜯어보면서 이해한 내용을 정리한 문서. 흐름은 `IocpCore`(엔진의 심장) → `Service`(누가 이 엔진을 쓰는지) → `Listener`(신규 접속을 받는 문지기) → `Session`(연결 하나하나)의 순서로 본다.

---

## 1. IocpCore / IocpEvent — 엔진의 심장

### 한 줄 요약
여러 워커 스레드가 하나의 "완료 큐"를 공유해서 들여다보다가, 비동기 I/O(연결/수신/송신/종료/접속수락)가 끝난 게 있으면 꺼내서 그 이벤트의 주인에게 넘겨주는 구조.

### 흐름
```
Register(소켓) → 비동기 요청(WSARecv/WSASend/AcceptEx/...) → 커널이 처리 → 완료되면 IOCP 큐에 통지 적재
→ 워커 스레드가 GetQueuedCompletionStatus로 통지를 꺼냄 → owner->Dispatch(event, bytes) 호출
```

핵심은 **"요청을 걸어두고, 끝나면 나중에 통지로 알려준다"**는 비동기 패턴이다. `WSARecv`를 호출한 스레드가 결과를 기다리는 게 아니라, 그냥 요청만 걸고 다른 일을 하러 가고, 나중에 (어쩌면 완전히 다른 스레드가) 완료 통지를 받아서 처리한다.

### 실무 비유
콜센터를 생각하면 된다. 상담원(워커 스레드) 여러 명이 **하나의 대기 티켓 함**(완료 포트)을 같이 보고 있다. 누가 어떤 손님을 맡았는지는 상관없이, 티켓이 뜨면 그중 아무나 비어있는 상담원이 가져가서 처리한다. 상담원을 손님(소켓)마다 전담으로 붙이는 게 아니라, 일이 끝나는 대로 유동적으로 배분하는 게 IOCP의 핵심 아이디어다 — 스레드 수보다 훨씬 많은 소켓을 적은 스레드로 감당할 수 있는 이유.

### `IocpEvent`가 `OVERLAPPED`를 상속하는 이유
`GetQueuedCompletionStatus`는 커널이 물고 있던 `LPOVERLAPPED` 포인터를 그대로 돌려준다. 이 코드는 그걸 주소 보정 없이 바로 `IocpEvent*`로 취급하는데(`reinterpret_cast`), 이게 맞으려면 `OVERLAPPED`가 객체의 **맨 앞(offset 0)**에 있어야 한다. 그래서 `IocpEvent`(와 파생 클래스들)에는 **절대 가상함수를 두면 안 된다** — 가상함수가 생기면 컴파일러가 vtable 포인터를 offset 0에 끼워넣어서 `OVERLAPPED`가 밀려나고, 캐스팅이 다 어긋나버린다.

### 레퍼런스 카운팅 (ADD_REF / RELEASE_REF)
커널은 `shared_ptr`을 모른다. 그냥 raw pointer(`OVERLAPPED*`)만 들고 있다가 완료되면 그 포인터를 돌려줄 뿐이다. 그런데 그 사이에 이 객체(예: `Session`)를 아무도 안 붙잡고 있으면, 커널이 아직 일하고 있는 도중에 객체가 소멸돼버릴 수 있다 — use-after-free.

그래서 비동기 요청을 걸기 **직전**에 `event->SetOwner(shared_from_this())`로 `shared_ptr`을 하나 더 만들어서 붙잡아두고(ADD_REF), 완료 처리가 시작되면 `SetOwner(nullptr)`로 풀어준다(RELEASE_REF). **비유하자면 "택배가 배송 중인 동안엔 절대 반품 처리를 못 하게 걸어두는 보류 딱지"** 같은 거다 — 택배(비동기 요청)가 배송 완료(커널 처리 완료)되기 전까지는 상품(객체)을 창고에서 치울 수 없게 막아두는 것.

---

## 2. Service — 누가 이 엔진을 쓰는가

### 한 줄 요약
`IocpCore` 하나를 소유하고, 세션들을 관리하고, 서버(`ServerService`)와 클라이언트(`ClientService`)가 이 위에서 서로 다른 방식으로 연결을 시작한다.

### 서버 vs 클라이언트
- `ServerService::Start()` — `Listener`를 만들어서 "누가 접속해오면 받아줄 준비"만 한다. 실제 접속은 클라이언트가 걸어온다.
- `ClientService::Start()` — `maxSessionCount`만큼 반복해서 직접 서버로 연결을 시도한다(`ConnectEx`). 클라이언트 프로그램 하나가 여러 개의 연결을 동시에 열 수 있는 구조 — 부하테스트용 더미클라이언트가 "가짜 유저 100명"을 한 프로세스에서 동시에 접속시키는 시나리오가 여기서 가능해진다.

### CreateSession과 AddSession이 나뉜 이유
`CreateSession()`은 세션을 만들고 IOCP에 등록만 한다 — 아직 실제로 연결이 끝난 게 아니다(`AcceptEx`/`ConnectEx`는 비동기라서). `AddSession()`은 진짜로 연결이 **완료된 시점**(`Session::ProcessConnect()`)에서만 호출돼서 세션 목록에 정식으로 등록한다.

**비유**: 식당 예약 시스템이라면, `CreateSession()`은 "예약 슬롯을 하나 잡아둔 것"이고, `AddSession()`은 "손님이 실제로 도착해서 자리에 앉은 것"이다. 예약만 잡아놓고 안 오는 손님(연결 실패)을 손님 명단에 넣으면 안 되니까 이 둘을 분리해둔 것.

### IocpCore를 직접 노출하지 않는 이유
`Service`가 `_iocpCore`를 감싸서 `Dispatch()`/`Register()`라는 얇은 위임 함수만 공개한다. 만약 `GetIocpCore()`로 `IocpCore` 전체를 노출하면, 이걸 쓰는 애플리케이션 코드(`GameServer.cpp`)가 `Register()`까지 마음대로 호출할 수 있게 되는데, 정작 필요한 건 "완료된 이벤트 있으면 처리해줘" 하나뿐이다. **필요한 능력만 인터페이스로 내주고 나머지는 감추는 것** — 캡슐화의 기본이다.

---

## 3. Listener — 신규 접속을 받는 문지기

### 실무 비유로 통째로 설명
게임 오픈 첫날 "대기열: 1234번째" 화면을 본 적이 있을 텐데, 이 구조를 그대로 빗대면:

1. **정문 앞 줄 (OS의 listen backlog)** — 아무리 사람이 몰려도 일단 줄은 서게 해준다. `SocketUtils::Listen(_socket, SOMAXCONN)`으로 OS가 허용하는 최대치로 열어둠. 아직 게임 서버 "안"으로 들어간 게 아니라 TCP 핸드셰이크 대기 상태.
2. **접수 창구 (`AcceptEx` 슬롯, `maxAcceptCount`개)** — 실제로 접수를 받아주는 창구가 동시에 여러 개(`_service->GetMaxSessionCount()`만큼) 열려 있다. 창구 하나가 손님 접수를 끝내면(`ProcessAccept`), 그 창구는 곧바로 다음 사람을 부른다(`RegisterAccept` 재등록). 그래서 한 번에 최대 `maxAcceptCount`명까지 "접수 진행 중" 상태를 유지할 수 있다.
3. **접수 담당 직원 (워커 스레드)** — 이 창구들을 실제로 굴리는 직원 수는 정해져 있고(`GameServer.cpp`에서 5개+메인스레드), 이 직원들은 접수만 하는 게 아니라 이미 입장한 손님 응대(recv/send)도 같이 한다. 그래서 접수가 몰리면 살짝 밀릴 수는 있어도, 밀린 요청은 유실되지 않고 IOCP 큐에서 순서대로 기다린다 — 직원 수는 **처리 속도**에 영향을 줄 뿐 "받을 수 있는 총 인원"의 하드 캡이 아니다.

### 흐름
```
StartAccept()
  → maxAcceptCount개의 AcceptEvent를 만들어 RegisterAccept()로 커널에 미리 걸어둠
  → (클라이언트 접속) → 그중 하나가 완료됨
  → Dispatch → ProcessAccept()
      → 세션 정보 확정(SetUpdateAcceptSocket, getpeername)
      → session->ProcessConnect() (Session이 "연결됨" 상태로 전환)
      → 같은 AcceptEvent로 RegisterAccept() 재호출 (슬롯 재사용)
```
같은 `AcceptEvent` 객체가 서버가 도는 내내 계속 재사용된다는 게 `Session`의 recv/send 이벤트와 다른 점이다 — recv/send는 한 번 쓰고 끝나지만, accept 슬롯은 영구적으로 돌아간다.

### 발견한 문제: Listener가 절대 소멸될 수 없던 구조
`AcceptEvent::owner`는 최초 한 번 `SetOwner(shared_from_this())`로 `Listener`를 붙잡은 뒤, 재사용되는 이벤트라 다시 놓아주는 지점이 없었다. 이러면 `Listener`가 참조 카운트상 **절대 0이 될 수 없어서**, `ServerService::CloseService()`가 `_listener = nullptr`을 해도(자기 몫 참조 하나만 놓는 것) 실제 `~Listener()`는 영원히 안 불린다.

지금처럼 프로세스가 죽을 때까지 서버가 계속 도는 구조에서는 실질적 피해가 없지만(OS가 프로세스 종료 시 다 회수), **"프로세스는 유지한 채 리스너만 닫는" 그레이스풀 셧다운**을 구현하면 문제가 된다. 실무에서는 이게 흔하다 — 점검/배포 시 리스너부터 닫아서 신규 접속을 막고, 기존 유저는 하던 걸 마칠 때까지 기다렸다가 마지막 유저가 나가면 프로세스를 종료하는 패턴(Kubernetes의 SIGTERM + graceful period가 정확히 이거다).

**해결**: `Listener`에 `_closing` 플래그를 두고, `CloseSocket()`이 소켓을 닫기 전에 이걸 켠다. 이후 `RegisterAccept()`(재등록을 시도하는 유일한 지점)가 맨 앞에서 `_closing`을 체크해서, 켜져 있으면 재등록 대신 `SetOwner(nullptr)`로 참조만 풀고 리턴한다. 모든 `AcceptEvent`가 자기 차례(완료 통지)가 올 때마다 하나씩 참조를 놓게 되고, 다 놓이면 `Listener`가 정상적으로 소멸 가능해진다.

---

## 4. Session — 연결 하나하나

### 흐름 (Recv 쪽)
```
ProcessConnect() (최초 1회)
  → RegisterRecv(): _recvBuffer의 빈 공간을 WSARecv에 걸어둠
  → (데이터 도착) → ProcessRecv(bytes)
      → _recvBuffer.OnWrite(bytes): 받은 만큼 "쓰기 커서" 전진
      → OnRecv(): 완전한 패킷만큼만 파싱해서 처리, 처리한 바이트 수 반환
      → _recvBuffer.OnRead(): "읽기 커서" 전진, 처리 못한 조각은 버퍼에 남김
      → _recvBuffer.Clean(): 남은 조각을 앞으로 당겨서 공간 정리
      → RegisterRecv() 재호출 (다음 수신 대기)
```

**실무 비유 — 택배 상자 나눠오기**: TCP는 스트림이라 "메시지 경계"를 보장하지 않는다. 한 번의 `WSARecv` 완료가 패킷 하나랑 딱 맞아떨어진다는 보장이 없다 — 패킷이 중간에 잘려서 올 수도 있고, 여러 패킷이 한 번에 뭉쳐서 올 수도 있다. 그래서 `[size(2)][id(2)][data...]`처럼 헤더에 크기를 박아두고, 그 크기만큼 다 도착했는지 확인한 다음에야 "패킷 하나 완성"으로 처리한다(`PacketSession::OnRecv`의 `while`문). 택배 기사가 큰 짐을 한 번에 다 못 들고 와서 오늘은 상자 절반만, 내일 나머지 절반을 놓고 가는 상황에서 "상자가 완전히 다 왔는지"를 매번 확인하고 열어보는 것과 같다.

### 흐름 (Send 쪽) — 왜 `_sendRegistered`가 필요한가
`Send()`는 여러 스레드가 동시에 호출할 수 있는 공개 API다(게임 로직 여러 스레드가 같은 세션에 보낼 데이터를 만들 수 있으니까). 근데 같은 소켓에 `WSASend`를 동시에 두 개 걸면 데이터 순서가 꼬일 수 있어서, **"실제로 WSASend를 커널에 거는 건 항상 한 스레드만"** 하도록 게이트를 건다.

```
Send(buffer) 호출 스레드가 여럿이어도:
  1. _sendQueue에 밀어넣기 (WRITE_LOCK)
  2. _sendRegistered.exchange(true) 결과가 false였던 딱 한 스레드만 RegisterSend() 호출
  3. 나머지는 그냥 큐에 넣고 리턴 (누군가 이미 보내는 중이라고 믿고)
```

**실무 비유 — 택배 트럭 한 대**: 이 세션은 택배 트럭이 한 번에 한 대만 나갈 수 있다. 여러 사람이 동시에 택배를 접수해도(`Send()`), 트럭이 이미 나가 있으면 그냥 다음 트럭을 위해 집하장(`_sendQueue`)에 쌓아두고, 트럭이 돌아오면(`ProcessSend`) 그때 쌓인 걸 한꺼번에 싣고 다시 나간다(`RegisterSend()` 재호출).

여기서 `_sendQueue.empty()` 체크와 `_sendRegistered.store(false)`를 **같은 락으로 묶는 게 중요**하다. 안 묶으면: "트럭 기사가 집하장이 비었다고 확인한 바로 그 순간, 손님이 택배를 새로 접수하면서 '이미 트럭 나가 있네' 하고 그냥 놔두고 가버리는" 레이스가 생긴다. 그 사이 기사는 "비었으니 오늘은 끝"이라고 트럭 운행을 종료해버리면, 그 택배는 다음 손님이 올 때까지 아무도 안 가져가는 상태로 계속 방치된다.

### Recv는 왜 이런 플래그가 필요 없나
`RegisterRecv()`를 부르는 곳이 `ProcessConnect`/`ProcessRecv` 자기 자신뿐이라, 외부에서 동시에 경쟁할 진입점 자체가 없다. `Send()`처럼 애플리케이션 코드가 아무 때나 부르는 공개 API가 아니라, 순전히 "이전 수신이 끝나야 다음 수신을 건다"는 체인으로만 이어지기 때문에 구조적으로 항상 순차적이다.

### ProcessSend는 어느 쪽에서 불리나
`WSASend`를 건 소켓과, 그 완료 통지를 받는 `IocpCore`는 **항상 같은 프로세스, 같은 소켓**이다. `ProcessSend(numOfBytes)`가 알려주는 건 "커널이 네 데이터를 몇 바이트 가져갔다"이지, **"상대방이 그걸 받았다"는 뜻이 절대 아니다.** 상대방이 실제로 받았는지는 상대방 소켓의 `WSARecv` 완료(그쪽의 `ProcessRecv`)로만 알 수 있고, 그마저도 "받았다"이지 "처리했다/이해했다"는 아니다 — 그건 애플리케이션 레벨 ACK이 따로 필요하다.

### Connect는 사실상 클라이언트 전용
`RegisterConnect()`는 `GetServiceType() != ServiceType::Client`면 바로 실패한다. 서버 쪽 세션은 `ConnectEx`를 아예 안 쓰고, `Listener::ProcessAccept()`가 `session->ProcessConnect()`를 **직접** 호출한다(그래서 `Listener`가 `Session`의 `friend`) — "연결 완료"를 트리거하는 방식이 클라(`ConnectEx` 완료 이벤트)와 서버(리스너가 accept 처리 중 직접 호출)로 나뉘어 있을 뿐, 그 뒤의 "세션 등록 + 수신 대기 시작" 마무리(`ProcessConnect` 내부)는 완전히 공유한다.

---

## 참고: 접속 인원 제한에 대한 오해
게임에서 흔히 보는 "서버 정원 초과, 대기 순번 몇 번째"는 **"동접이 N을 넘으면 신규 입장을 막는" 정원 제한**인데, 지금 이 프로젝트엔 그게 없다. `_maxSessionCount`는 실제로는 "접수 창구를 몇 개 열지"(위 Listener의 ②) 결정하는 데만 쓰이고, 동접자가 그 수를 넘어도 막는 코드는 없다 — 로그로 개수만 남긴다. "접수 창구 개수"와 "직원 수"는 있어도, 진짜 정원 제한(대기열 시스템)은 아직 없는 상태.

---

## 다음으로 볼 만한 곳
- **RecvBuffer / SendBuffer**: 별도 문서로 정리함 → [Buffer_Review.md](Buffer_Review.md)
- **SocketUtils**: 별도 문서로 정리함 → [SocketUtils_Review.md](SocketUtils_Review.md)
- **Lock / DeadLockProfiler**: 별도 문서로 정리함 → [Lock_Review.md](Lock_Review.md)
- **Job/JobQueue/GlobalQueue/JobTimer**: 별도 문서로 정리함 → [JobQueue_Review.md](JobQueue_Review.md)
- **Memory/ObjectPool/PoolAllocator**: 별도 문서로 정리함 → [Memory_Review.md](Memory_Review.md)
- **Protobuf / 패킷 파이프라인**: 별도 문서로 정리함 → [Protobuf_Review.md](Protobuf_Review.md)
- **패킷 수신 흐름 상세(도착~파싱~디스패치)**: 별도 문서로 정리함 → [PacketFlow_Review.md](PacketFlow_Review.md)
- `Room`/`GameSession`/`ClientPacketHandler` 등 게임 로직 골격 — 엔진 내부보다는 "엔진 위에 게임을 얹는" 영역이라 상대적으로 낮은 우선순위.
