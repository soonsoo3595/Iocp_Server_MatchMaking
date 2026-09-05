# 패킷 수신 흐름 상세 정리

소켓에 바이트가 도착하는 순간부터 `Handle_C_XXX` 게임 로직이 실행되기까지, 한 단계도 건너뛰지 않고 따라가 본다. `Session`/`Buffer`/`Protobuf` 문서에서 각각 다뤘던 내용을 하나의 흐름으로 이어붙인 것.

---

## 전체 단계 요약

```
① 커널이 WSARecv 완료 -> IocpCore::Dispatch -> Session::ProcessRecv(numOfBytes)
② RecvBuffer.OnWrite(numOfBytes)         : 받은 바이트를 유효 데이터로 커밋
③ PacketSession::OnRecv(ReadPos, dataSize) : 완전한 패킷만큼만 반복해서 뽑아냄
     -> 완전한 패킷 하나마다 OnRecvPacket(buffer, size) 호출
④ GameSession::OnRecvPacket -> ClientPacketHandler::HandlePacket(session, buffer, len)
⑤ header->id로 GPacketHandler[id] 조회 (O(1))
⑥ HandlePacket<PacketType> 템플릿 : ParseFromArray로 protobuf 역직렬화
⑦ Handle_C_XXX(session, pkt) 실제 게임 로직 실행
⑧ RecvBuffer.OnRead(processLen) + Clean()  : 처리한 만큼 커서 전진, 남은 조각 보존
⑨ RegisterRecv() 재호출, 다음 수신 대기
```

---

## ① 커널이 데이터를 채워줌

`RegisterRecv()`가 미리 `WSARecv`를 걸어둔 상태였다:
```cpp
wsaBuf.buf = reinterpret_cast<char*>(_recvBuffer.WritePos());
wsaBuf.len = _recvBuffer.FreeSize();
::WSARecv(_socket, &wsaBuf, 1, ..., &_recvEvent, nullptr);
```
데이터가 도착하면 커널이 `WritePos()`가 가리키던 그 메모리에 **직접** 바이트를 써준다. 완료되면 IOCP 큐에 통지가 올라가고, 워커 스레드가 그걸 꺼내서 `Session::Dispatch` → `ProcessRecv(numOfBytes)`를 호출한다. `numOfBytes`는 이번에 실제로 도착한 바이트 수 — 한 번에 패킷 하나가 딱 맞게 오는 게 아니라 TCP 세그먼트 단위로 임의의 크기만큼 온다.

---

## ② `RecvBuffer.OnWrite` — 커밋

```cpp
if (_recvBuffer.OnWrite(numOfBytes) == false) { Disconnect(...); return; }
```
방금 커널이 채운 만큼 `_writePos`를 전진시켜서 "유효한 데이터"로 인정한다. 이 시점의 `_recvBuffer`에는 **이전에 처리 못하고 남겨뒀던 조각 + 이번에 새로 온 것**이 이어붙어 있을 수 있다.

---

## ③ `PacketSession::OnRecv` — 패킷 경계 자르기

```cpp
int32 PacketSession::OnRecv(BYTE* buffer, int32 len)
{
	int32 processLen = 0;
	while (true)
	{
		int32 dataSize = len - processLen;

		if (dataSize < sizeof(PacketHeader))   // 헤더(4바이트: size(2)+id(2))도 안 왔으면
			break;

		PacketHeader header = *(reinterpret_cast<PacketHeader*>(&buffer[processLen]));

		if (dataSize < header.size)            // 헤더는 왔는데 본문이 덜 왔으면
			break;

		OnRecvPacket(&buffer[processLen], header.size);   // 완전한 패킷 하나 처리
		processLen += header.size;
	}
	return processLen;   // 실제로 소비한 바이트 수만 돌려줌
}
```
`buffer`는 `_recvBuffer.ReadPos()`(아직 안 읽은 데이터의 시작), `len`은 `_recvBuffer.DataSize()`(지금까지 쌓인 미처리 데이터 전체 크기)다. `while` 루프가 도는 이유: **한 번의 `WSARecv` 완료에 패킷이 여러 개 들어있을 수도, 하나도 다 안 들어있을 수도** 있기 때문이다.

- 헤더 4바이트도 안 왔으면 → 더 볼 것도 없이 바로 중단.
- 헤더는 왔는데 `header.size`(패킷 전체 크기)만큼 아직 안 왔으면 → 역시 중단, 다음 수신을 기다림.
- 둘 다 충족하면 → `OnRecvPacket`으로 완전한 패킷 하나를 넘기고, 그만큼 `processLen`을 전진시켜서 **다음 패킷이 있는지 또 확인**(루프 반복).

### 구체적인 예시 — 패킷이 두 번에 걸쳐 나뉘어 온 경우
클라이언트가 `C_CHAT`(헤더 4바이트 + `msg` 필드 몇 바이트, 총 20바이트라고 하자) 패킷 하나를 보냈는데, 네트워크 사정으로 TCP가 이걸 두 조각으로 쪼개서 보냈다고 하면:

1. **1차 수신**: 12바이트만 도착. `OnWrite(12)`로 커밋. `OnRecv` 호출 → `dataSize=12 < header.size=20` → **아무것도 처리 못 하고 `processLen=0`으로 리턴.**
2. `ProcessRecv`가 `OnRead(0)`을 호출(사실상 아무것도 안 읽음), `Clean()`은 아직 여유 공간이 충분하니 그냥 넘어감. `RegisterRecv()`로 다시 수신 대기 — **이 12바이트는 버려지지 않고 `RecvBuffer`에 그대로 남아있다.**
3. **2차 수신**: 나머지 8바이트 도착. `OnWrite(8)`로 커밋 → 이제 버퍼에는 총 20바이트(1차분 12 + 2차분 8)가 이어붙어 있음.
4. `OnRecv` 재호출 → 이번엔 `dataSize=20 >= header.size=20` → **비로소 완전한 패킷으로 인식, `OnRecvPacket` 호출.**

즉 애플리케이션 코드(`Handle_C_CHAT`)는 패킷이 몇 번에 걸쳐 나뉘어 왔는지 전혀 신경 쓸 필요가 없다 — `PacketSession::OnRecv`가 이 조립 과정을 전부 숨겨주고, **완전한 패킷 단위로만** 다음 단계로 넘긴다.

---

## ④ `OnRecvPacket` — 가상함수를 통해 실제 게임 세션으로

```cpp
// GameSession.cpp
void GameSession::OnRecvPacket(BYTE* buffer, int32 len)
{
	PacketSessionRef session = GetPacketSessionRef();
	ClientPacketHandler::HandlePacket(session, buffer, len);
}
```
`PacketSession::OnRecvPacket`은 순수가상함수라 `GameSession`(또는 클라의 `ServerSession`)이 반드시 구현해야 한다. 여기서 처음으로 "이 프로젝트만의 로직"(`ClientPacketHandler`)이 등장한다.

---

## ⑤ `HandlePacket` — ID로 O(1) 라우팅

```cpp
static bool HandlePacket(PacketSessionRef& session, BYTE* buffer, int32 len)
{
	PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
	return GPacketHandler[header->id](session, buffer, len);
}
```
`buffer`의 맨 앞 4바이트를 다시 `PacketHeader`로 캐스팅해서 `id`를 뽑고(`OnRecv`에서 이미 한 번 읽었던 걸 여기서 다시 읽는 셈), `GPacketHandler`(65536칸짜리 함수 포인터 배열)를 그 `id`로 바로 인덱싱한다 — 조건문 없이 배열 접근 한 번으로 끝나는 디스패치.

---

## ⑥ 람다 → `HandlePacket<PacketType>` 템플릿 → Protobuf 역직렬화

`Init()`에서 미리 등록해둔 람다가 실행된다:
```cpp
GPacketHandler[PKT_C_CHAT] = [](PacketSessionRef& session, BYTE* buffer, int32 len)
	{ return HandlePacket<Protocol::C_CHAT>(Handle_C_CHAT, session, buffer, len); };

template<typename PacketType, typename ProcessFunc>
static bool HandlePacket(ProcessFunc func, PacketSessionRef& session, BYTE* buffer, int32 len)
{
	PacketType pkt;
	if (pkt.ParseFromArray(buffer + sizeof(PacketHeader), len - sizeof(PacketHeader)) == false)
		return false;   // 역직렬화 실패 -> 조작된/손상된 패킷일 수 있음
	return func(session, pkt);   // 실제 Handle_C_CHAT 호출
}
```
`buffer + sizeof(PacketHeader)`로 헤더를 건너뛰고, 그 뒤(`data` 부분)만 protobuf의 `ParseFromArray`로 역직렬화해서 `Protocol::C_CHAT` 객체를 만든다. 이 객체가 바로 `Handle_C_CHAT(session, pkt)`의 두 번째 인자로 넘어간다.

---

## ⑦ 실제 게임 로직 실행

```cpp
bool Handle_C_CHAT(PacketSessionRef& session, Protocol::C_CHAT& pkt)
{
	std::cout << pkt.msg() << endl;

	Protocol::S_CHAT chatPkt;
	chatPkt.set_msg(pkt.msg());
	auto sendBuffer = ClientPacketHandler::MakeSendBuffer(chatPkt);

	GRoom->DoAsync(&Room::Broadcast, sendBuffer);   // JobQueue로 안전하게 위임
	return true;
}
```
여기서부터는 순수하게 개발자가 작성한 코드다. `pkt.msg()`처럼 protobuf가 생성해준 접근자로 필드를 읽고, 응답이 필요하면 `S_XXX` 메시지를 만들어 `MakeSendBuffer` → `Send`(또는 위 예시처럼 `Room`의 `JobQueue`에 `DoAsync`로 위임)한다.

---

## ⑧-⑨ 뒷정리, 그리고 다음 수신 준비

`Handle_C_CHAT`가 리턴하면 제어가 `PacketSession::OnRecv`의 `while` 루프로 돌아가서, 버퍼에 남은 다음 패킷이 있는지 계속 확인한다. 루프가 끝나면 최종 `processLen`(이번에 실제로 처리한 총 바이트 수)이 `ProcessRecv`로 돌아가고:
```cpp
_recvBuffer.OnRead(processLen);   // 읽기 커서 전진 (처리 못한 조각은 그대로 남음)
_recvBuffer.Clean();               // 필요하면 압축
RegisterRecv();                    // 다음 WSARecv 다시 걸어둠
```
이걸로 한 사이클이 끝나고, 다시 ①로 돌아가 다음 데이터를 비동기로 기다린다.

---

## 정리 — 각 계층이 책임지는 것

| 계층 | 책임 |
|---|---|
| 커널 / IOCP | "데이터가 왔다"는 사실과 바이트 수만 알려줌. 패킷 개념 자체를 모름 |
| `RecvBuffer` | 여러 번의 수신에 걸친 바이트를 누적/보존. 처리 못한 조각도 잃어버리지 않음 |
| `PacketSession::OnRecv` | 스트림을 "완전한 패킷" 단위로 잘라냄. 조각난 패킷을 감춤 |
| `ClientPacketHandler` | 패킷 ID -> 핸들러 함수로 O(1) 라우팅, protobuf 역직렬화 |
| `Handle_C_XXX` | 실제 게임 로직 (검증, 상태 변경, 응답 전송) |

핵심은 **각 계층이 "이전 단계가 다 처리 못 했을 수도 있다"는 걸 전제로 짜여있다**는 점이다 — 커널은 몇 바이트가 왔는지만 보장하고, `RecvBuffer`는 그걸 잃어버리지 않게 쌓아두고, `OnRecv`는 완전한 단위가 될 때까지 참을성 있게 기다렸다가 넘긴다. 이 계층 분리 덕분에 `Handle_C_XXX`를 작성하는 개발자는 "패킷이 네트워크상에서 어떻게 쪼개져 오는지" 전혀 신경 쓸 필요가 없다.
