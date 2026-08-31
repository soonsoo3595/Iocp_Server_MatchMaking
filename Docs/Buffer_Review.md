# RecvBuffer / SendBuffer 학습 정리

`Session`은 `RecvBuffer _recvBuffer;`를 값으로 그냥 들고 있는데, 보낼 데이터는 `Queue<SendBufferRef> _sendQueue;`처럼 스마트포인터로 관리한다. 이 비대칭에는 이유가 있다: **RecvBuffer는 세션 하나가 독점하는 자원이고, SendBuffer는 여러 세션이 동시에 참조할 수 있는 자원**이기 때문이다.

---

## RecvBuffer — 선형 버퍼 + 지연 압축(lazy compaction)

**구조**: 진짜 링(원형) 버퍼가 아니라, 읽기/쓰기 커서를 가진 선형 버퍼다.
```
[ 이미 읽고 버린 부분 | 아직 안 읽은 데이터 | 빈 공간(FreeSize) ]
0            _readPos              _writePos          _capacity   (= bufferSize * 10)
```
- `DataSize() = _writePos - _readPos` — 처리 대기 중인 데이터량
- `FreeSize() = _capacity - _writePos` — 버퍼 끝까지 남은 여유 (읽고 버린 앞쪽 공간은 포함 안 됨)

### 한 사이클의 흐름
```
RegisterRecv(): WritePos()/FreeSize()를 WSARecv에 넘김
  → (커널이 그 주소에 직접 데이터를 씀) → ProcessRecv(numOfBytes)
      → OnWrite(numOfBytes): 받은 만큼 쓰기 커서 전진 (데이터 커밋)
      → OnRecv(): 완전한 패킷만 파싱, 처리한 바이트 수(processLen) 반환
      → OnRead(processLen): 처리한 만큼만 읽기 커서 전진 (잘린 패킷 조각은 버퍼에 남김)
      → Clean(): 필요하면 압축해서 공간 회수
      → RegisterRecv() 재호출
```

### `Clean()`이 매번 압축하지 않는 이유
`memcpy` 비용을 아끼려고, 세션당 `bufferSize`(64KB)의 10배(`BUFFER_COUNT=10`)나 되는 여유 공간을 미리 잡아두고, 여유가 `bufferSize` 미만으로 줄어들 때만 압축한다 — 메모리를 더 쓰는 대신 압축 빈도를 낮추는 트레이드오프.

### 오버플로우 체크의 성격이 서로 다르다
- **`OnWrite`의 `numOfBytes > FreeSize()` 체크**: `WSARecv`를 걸 때 애초에 `wsaBuf.len = FreeSize()`로 상한을 커널에 줬기 때문에, 커널이 실제로 이 상한을 넘겨 쓰는 일은 없다. 이 체크는 "논리적으로 절대 일어나면 안 되는 상황"을 잡아내는 방어 코드(assert에 가까움).
- **`OnRead`의 `processLen > DataSize()` 체크**: `processLen`은 커널이 아니라 **우리 파싱 코드(`OnRecv`)가 계산한 값**이라 버그나 조작된 패킷 헤더 때문에 실제로 틀릴 수 있다. 이 체크가 없으면 `_readPos`가 `_writePos`를 넘어서고, 다음 `Clean()`의 `memcpy` 크기 인자가 음수→거대한 값으로 취급돼 진짜 메모리 오염으로 이어질 수 있는 **핵심 방어선**이다.
- 둘 다 실패하면 `Disconnect()`로 빠진다 — 커널 레벨 문제가 아니라, "이 세션의 버퍼 상태를 더 이상 신뢰할 수 없다"는 소프트웨어적 판단이다.

### 실무 비유
TCP는 스트림이라 "메시지 경계"를 보장하지 않는다. 한 번의 `WSARecv` 완료가 패킷 하나랑 딱 맞아떨어진다는 보장이 없다 — 패킷이 중간에 잘려서 올 수도, 여러 패킷이 한 번에 뭉쳐서 올 수도 있다. 그래서 `[size(2)][id(2)][data...]`처럼 헤더에 크기를 박아두고, 그 크기만큼 다 도착했는지 확인한 다음에야 "패킷 하나 완성"으로 처리한다(`PacketSession::OnRecv`의 `while`문). 택배 기사가 큰 짐을 한 번에 다 못 들고 와서 오늘은 상자 절반만, 내일 나머지 절반을 놓고 가는 상황에서 "상자가 완전히 다 왔는지"를 매번 확인하고 열어보는 것과 같다. 이 창고(버퍼) 자체는 이 집(세션) 전용이라 누구와도 안 나눈다.

---

## SendBuffer — 청크 풀링 + 참조로 공유

### 세 계층
```
SendBufferManager  — 스레드별로 "지금 쓰고 있는 청크"를 관리, 다 쓴 청크는 풀에서 재사용
       ↓
SendBufferChunk    — 실제 메모리를 들고 있는 6000바이트짜리 덩어리
       ↓
SendBuffer         — 청크의 한 조각을 가리키는 "뷰(view)" — 자기 메모리가 없음
```

`SendBuffer`는 메모리를 직접 갖지 않고 `_owner`(`SendBufferChunkRef`)가 가진 배열의 한 조각을 가리키기만 한다 — `_owner`가 shared_ptr이라 이 `SendBuffer`가 살아있는 한 청크는 소멸 안 됨(자식이 부모를 붙잡는 구조).

### Send 요청 전체 흐름 (①생성 → ②적재 → ③발사 → ④정리)

```
① MakeSendBuffer()          — 패킷 생성
    → GSendBufferManager->Open(packetSize)
② Session::Send(sendBuffer) — 이 세션의 송신 큐에 넣기
③ Session::RegisterSend()   — 큐에 쌓인 걸 모아서 WSASend 한 번에 걸기
④ Session::ProcessSend()    — 완료되면 버퍼들 참조 해제
    → SendBuffer 소멸 → SendBufferChunk 참조 해제 → (마지막이면) 풀로 반납
```

**① `SendBufferManager::Open(size)` — 청크 배급**
```cpp
SendBufferRef SendBufferManager::Open(uint32 size)
{
	if (LSendBufferChunk == nullptr)              // 이 스레드가 아직 청크가 없으면
	{
		LSendBufferChunk = Pop();                  // 풀에서 하나 꺼내옴 (WRITE_LOCK)
		LSendBufferChunk->Reset();                 // _usedSize=0, _open=false로 초기화
	}

	if (LSendBufferChunk->FreeSize() < size)       // 지금 청크에 공간이 부족하면
	{
		LSendBufferChunk = Pop();                  // 새 청크로 교체
		LSendBufferChunk->Reset();
	}

	return LSendBufferChunk->Open(size);            // 실제로 조각 떼어내기
}
```
`thread_local SendBufferChunkRef LSendBufferChunk`로 스레드마다 "지금 쓰고 있는 청크"를 따로 들고 있다가, 공간이 부족할 때만 공유 풀(`_sendBufferChunks`, `WRITE_LOCK`으로 보호)에서 새 청크를 꺼내온다 — 패킷 하나 만들 때마다 매번 락을 잡을 필요가 없다.

`LSendBufferChunk = Pop()`로 교체되는 순간, **이전 청크에 대한 TLS 변수의 참조는 그냥 끊긴다.** 근데 그 이전 청크에서 이미 떼어져서 어느 세션의 `_sendQueue`에 들어가 있는 `SendBuffer`들은 각자 `_owner`로 그 청크를 여전히 붙잡고 있다. 그러니까 "청크를 다 안 썼는데 버려지는 거 아닌가" 걱정할 필요가 없다 — **아직 전송 안 된 조각이 하나라도 남아있으면 청크는 계속 살아있고**, 다 전송돼서 아무도 안 붙잡을 때가 돼서야 정리된다. 대신 그 청크의 "덜 쓰고 남은 자투리 공간"은 그대로 버려지는 셈이라(내부 단편화), 약간의 메모리 낭비는 감수하는 트레이드오프다.

`Pop()`이 공유 풀에 반납된 청크가 있으면 재사용하고, 없으면 새로 만든다:
```cpp
return SendBufferChunkRef(Xnew<SendBufferChunk>(), PushGlobal);
```
이때 `shared_ptr`의 **커스텀 디리터가 `PushGlobal`**이라, 이 청크는 나중에 refcount가 0이 돼도 진짜 `delete`되지 않고 **공유 풀로 돌아간다** — shared_ptr의 소멸 시점을 "메모리 해제"가 아니라 "재활용 통에 넣기"로 활용한 것.

실제 패킷 생성 코드(`ClientPacketHandler::MakeSendBuffer`)는 이렇게 쓴다:
```cpp
SendBufferRef sendBuffer = GSendBufferManager->Open(packetSize);   // 청크에서 조각 하나 예약
PacketHeader* header = reinterpret_cast<PacketHeader*>(sendBuffer->Buffer());
header->size = packetSize; header->id = pktId;
pkt.SerializeToArray(&header[1], dataSize);                        // 최종 위치에 바로 직렬화 (복사 없음)
sendBuffer->Close(packetSize);                                     // 실제 쓴 크기 확정
```

**② `Session::Send()` — 내 세션 큐에 적재**
`MakeSendBuffer()`가 돌려준 `SendBufferRef`를 애플리케이션 코드가 `session->Send(sendBuffer)`로 넘기면 `_sendQueue`에 쌓이고, `_sendRegistered` 게이트를 통과한 스레드가 있으면 `RegisterSend()`가 걸린다.

**③ `Session::RegisterSend()` — 모아서 한 번에 발사**
```cpp
while (_sendQueue.empty() == false)
{
	SendBufferRef sendBuffer = _sendQueue.front();
	_sendQueue.pop();
	_sendEvent.sendBuffers.push_back(sendBuffer);   // 큐에 있던 걸 전부 옮겨 담음
}
// wsaBufs에 각 sendBuffer->Buffer()/WriteSize()를 그대로 연결 (복사 없음)
WSASend(_socket, wsaBufs.data(), wsaBufs.size(), ...);  // 한 번의 시스템콜로 전부 발사
```
쌓여있던 여러 개의 `SendBufferRef`를 `_sendEvent.sendBuffers`로 옮기고, 각각의 실제 메모리 위치를 가리키는 `WSABUF` 배열로 **Scatter-Gather** 전송한다 — 여러 패킷을 하나로 합치는 복사 작업 없이, 흩어진 `SendBufferChunk` 메모리 여러 곳을 커널이 한 번의 호출로 다 읽어가게 하는 것.

**④ `Session::ProcessSend()` — 완료 후 정리, 여기서 청크가 풀려나간다**
```cpp
_sendEvent.SetOwner(nullptr);        // Session 자신에 대한 ADD_REF 해제 (버퍼랑은 별개)
_sendEvent.sendBuffers.clear();      // ★ 여기서 각 SendBufferRef의 참조가 사라짐
```
`clear()`가 호출되는 순간, 각 `SendBufferRef`의 참조가 하나씩 사라진다. 어떤 `SendBuffer`의 **마지막** 참조였다면(예: 1:1 귓속말처럼 딱 한 세션만 이 버퍼를 썼던 경우): `SendBuffer` 객체 소멸(→ `ObjectPool<SendBuffer>`로 반납) → 그게 갖고 있던 `_owner`(청크 참조)도 같이 사라짐 → 그게 청크의 마지막 참조였다면 청크도 `PushGlobal`로 공유 풀에 반납 → 다음 `Open()`에서 재사용.

### 이중 풀링 구조
`SendBuffer`라는 "얇은 뷰 객체" 자체도 `ObjectPool<SendBuffer>`로 풀링되고(뷰 객체의 메모리), 그 뷰가 가리키는 실제 바이트 저장공간(`SendBufferChunk`)도 `SendBufferManager`로 따로 풀링된다. 브로드캐스트라면 여러 세션이 같은 `SendBuffer`를 참조하고 있을 테니, `ProcessSend`가 각 세션마다 따로 도는 동안 참조 카운트가 하나씩 줄다가, **가장 늦게 전송을 끝낸 세션이 마지막으로 `clear()`할 때** 비로소 실제 정리(풀 반납)가 일어난다.

### 왜 `SendBuffer`는 `shared_ptr`로 관리하나 — 여러 세션이 같은 버퍼를 공유하기 때문
`Service::Broadcast(SendBufferRef sendBuffer)`가 방 안의 모든 세션에 같은 `SendBufferRef`를 넘기는 게 근거다:
```cpp
void Service::Broadcast(SendBufferRef sendBuffer)
{
	for (const auto& session : _sessions)
		session->Send(sendBuffer);   // 100명이면 100개 세션이 "같은" 버퍼를 참조
}
```
채팅 메시지 하나를 100명에게 보낸다면 직렬화는 한 번만 하고, 그 결과물을 100개 세션 큐에 참조로만 나눠준다. 각 세션은 서로 다른 타이밍에 송신을 완료하니, "언제 이 버퍼를 진짜로 치워도 되는가"는 **마지막으로 다 쓴 세션이 나갈 때** 자연스럽게 결정돼야 한다 — 정확히 `shared_ptr` 레퍼런스 카운팅이 하는 일이다.

### 실무 비유
큰 종이 두루마리(청크)에서 편지 한 장씩(`SendBuffer`) 뜯어 쓴다. 같은 편지를 여러 명에게 복사해서 보내는 대신(브로드캐스트), 원본 한 장을 여러 명이 돌려보게 하고, 모두가 다 읽고 나면 그 두루마리는 버리지 않고 재활용 창고로 돌아간다.

---

## 정리

| | RecvBuffer | SendBuffer |
|---|---|---|
| 소유 관계 | 세션 하나가 독점 | 여러 세션이 동시 참조 가능 |
| 관리 방식 | 값 멤버 (`RecvBuffer _recvBuffer`) | `shared_ptr` (`SendBufferRef`) |
| 메모리 재사용 단위 | 세션 자신의 버퍼를 압축(memcpy)해서 재사용 | 청크 단위로 풀링, refcount 0 되면 풀로 반납 |
| 핵심 안전장치 | `OnRead`의 오버플로우 체크 (버퍼 상태 무결성) | `owner`(청크) 참조로 use-after-free 방지 |
