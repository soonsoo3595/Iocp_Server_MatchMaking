# Protobuf / 패킷 파이프라인 학습 정리

`.proto` 스키마 하나로 시작해서, 실제 게임 로직 핸들러 호출까지 이어지는 전체 파이프라인. `Session`/`Buffer` 문서에서 본 "받은 바이트를 어떻게 처리하는가"의 마지막 연결고리다.

---

## 0. Protobuf 문법 기초 (proto3)

이 프로젝트가 쓰는 세 파일(`Enum.proto`/`Struct.proto`/`Protocol.proto`)을 기준으로.

```protobuf
syntax = "proto3";      // 문법 버전 선언 (proto2와 필드 옵셔널 규칙 등이 다름)
package Protocol;        // C++에서는 namespace Protocol { ... } 로 대응됨

import "Enum.proto";     // 다른 .proto 파일의 정의를 가져다 씀
```

### enum
```protobuf
enum PlayerType
{
	PLAYER_TYPE_NONE = 0;     // proto3는 enum의 첫 번째 값이 반드시 0이어야 함
	PLAYER_TYPE_KNIGHT = 1;
	PLAYER_TYPE_MAGE = 2;
	PLAYER_TYPE_ARCHER = 3;
}
```
`= 0`, `= 1`처럼 붙는 숫자는 "몇 번째 값이냐"가 아니라 **이 값의 고유 번호(와이어에 실제로 실리는 값)**다. proto3는 기본값(필드를 안 채웠을 때)이 항상 0이라서, enum도 0번 값이 "기본/없음"을 의미하도록 강제한다 — 그래서 `NONE = 0`이 관례.

### message (구조체 역할)
```protobuf
message Player
{
	uint64 id = 1;
	string name = 2;
	PlayerType playerType = 3;
}
```
`message`가 C++의 `class`/`struct`에 대응된다(실제로 `protoc`가 `class Player`를 생성). `= 1`, `= 2`, `= 3`은 **필드 번호(field number)**로, 필드 이름이 아니라 **이 번호가 와이어 포맷(직렬화된 바이트)에 실제로 기록**된다. 그래서:
- 필드 번호는 한 메시지 안에서 유일해야 하고, **한 번 배포된 뒤에는 절대 바꾸면 안 된다**(바꾸면 이전에 저장/전송된 데이터와 호환이 깨짐).
- 필드 이름은 자유롭게 바꿔도 된다(와이어에 안 실리니까) — 반대로 번호를 재사용하는 것도 위험하다.
- 1~15번은 1바이트로 인코딩되니, 자주 쓰는 필드에 우선 배정하는 게 관례(이 프로젝트는 필드 수가 적어서 크게 신경 안 써도 됨).

### 스칼라 타입 / repeated / message 필드
```protobuf
message S_LOGIN
{
	bool success = 1;
	repeated Player players = 2;   // Player 여러 개 -> C++에서는 RepeatedPtrField<Player> (배열처럼 씀)
}
```
`uint64`/`string`/`bool`/`float`/`double` 같은 스칼라 타입 외에, 다른 `message`를 필드 타입으로 그대로 쓸 수 있다(`repeated Player`). `repeated`는 "0개 이상의 배열"이라는 뜻 — C++에서는 `.add_players()`/`.players(i)`/`.players_size()` 같은 접근자가 자동 생성된다.

### 빈 메시지도 유효하다
```protobuf
message C_LOGIN
{
}
```
필드가 하나도 없어도 된다 — "이 요청 자체가 이벤트"인 경우(로그인 요청 자체는 별도 데이터가 필요 없고 이미 연결된 세션 정보로 충분한 경우) 이렇게 빈 메시지를 쓴다.

---

## 1. 이 프로젝트의 스키마 구성

```
Enum.proto      — PlayerType 하나
Struct.proto    — Player (Enum.proto를 import)
Protocol.proto  — C_LOGIN/S_LOGIN/C_ENTER_GAME/S_ENTER_GAME/C_CHAT/S_CHAT (둘 다 import)
```
`C_`(Client→Server) / `S_`(Server→Client) 접두사로 방향을 구분하는 네이밍 컨벤션을 쓴다.

---

## 2. `protoc` — 스키마를 C++ 직렬화 코드로

```bat
protoc.exe -I=./ --cpp_out=./ ./Enum.proto
protoc.exe -I=./ --cpp_out=./ ./Struct.proto
protoc.exe -I=./ --cpp_out=./ ./Protocol.proto
```
각 `.proto`마다 `Xxx.pb.h`/`Xxx.pb.cc`가 생성된다. 여기서 나오는 클래스가 `SerializeToArray()`/`ParseFromArray()`/`ByteSizeLong()` 같은 함수를 제공하는데, `Buffer_Review.md`에서 본 `MakeSendBuffer`가 바로 이걸 쓴다:
```cpp
const uint16 dataSize = static_cast<uint16>(pkt.ByteSizeLong());   // 직렬화했을 때 크기 미리 계산
SendBufferRef sendBuffer = GSendBufferManager->Open(packetSize);
pkt.SerializeToArray(&header[1], dataSize);                          // 최종 위치에 바로 직렬화
```

---

## 3. `GenPackets.exe` (자체 제작 `PacketGenerator`) — 디스패치 코드 생성

```bat
GenPackets.exe --path=./Protocol.proto --output=ClientPacketHandler --recv=C_ --send=S_
GenPackets.exe --path=./Protocol.proto --output=ServerPacketHandler --recv=S_ --send=C_
```
`Protocol.proto`를 다시 파싱해서, **서버 입장**(`--recv=C_ --send=S_`)에서는 "`C_`로 시작하는 건 받는 것, `S_`로 시작하는 건 보내는 것"이라는 관점으로 `ClientPacketHandler.h`를 만들고, 클라이언트용은 반대(`ServerPacketHandler.h`)로 만든다. 결과물:

```cpp
enum : uint16 { PKT_C_LOGIN = 1000, PKT_S_LOGIN = 1001, PKT_C_ENTER_GAME = 1002, ... };  // 순번 자동 부여

using PacketHandlerFunc = std::function<bool(PacketSessionRef&, BYTE*, int32)>;
extern PacketHandlerFunc GPacketHandler[UINT16_MAX];   // ID -> 핸들러, O(1) 디스패치 테이블

// 실제 로직은 개발자가 채워야 하는 선언부만 생성
bool Handle_C_LOGIN(PacketSessionRef& session, Protocol::C_LOGIN& pkt);

class ClientPacketHandler
{
public:
	static void Init()
	{
		for (int32 i = 0; i < UINT16_MAX; i++)
			GPacketHandler[i] = Handle_INVALID;         // 기본값: 모르는 ID는 안전 처리
		GPacketHandler[PKT_C_LOGIN] = [](PacketSessionRef& session, BYTE* buffer, int32 len)
			{ return HandlePacket<Protocol::C_LOGIN>(Handle_C_LOGIN, session, buffer, len); };
		...
	}

	static bool HandlePacket(PacketSessionRef& session, BYTE* buffer, int32 len)
	{
		PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
		return GPacketHandler[header->id](session, buffer, len);   // 헤더의 id로 바로 인덱싱
	}

	static SendBufferRef MakeSendBuffer(Protocol::S_LOGIN& pkt) { return MakeSendBuffer(pkt, PKT_S_LOGIN); }
	...
};
```
`GPacketHandler`는 `UINT16_MAX`(65536)개짜리 함수 포인터 배열이라, 패킷 ID로 바로 인덱싱해서 O(1)로 핸들러를 찾는다. 못 보던 ID는 전부 `Handle_INVALID`로 초기화돼있어서 안전하게 무시된다. `PKT_C_LOGIN = 1000`처럼 **패킷 ID는 프로토콜 정의 순서대로 자동으로 순번이 매겨진다** — 값 자체에 의미는 없지만, 순서대로 고정된 값이라 정적 분석/리버스 엔지니어링에는 다소 취약한 편이다(알려진 이슈).

---

## 4. MSBuild PreBuildEvent — 매 빌드마다 자동 재생성

```xml
<PreBuildEvent>
  <Command>CALL $(SolutionDir)Common\Protobuf\bin\GenPackets.bat
CALL $(SolutionDir)Common\Procedures\GenProcs.bat</Command>
</PreBuildEvent>
```
`GameServer`/`GameClient` 둘 다 **빌드 시작 전에 매번** `GenPackets.bat`을 돈다. 스크립트 흐름:
```bat
protoc.exe ... (3개 .proto)
GenPackets.exe ... (ClientPacketHandler.h)
GenPackets.exe ... (ServerPacketHandler.h)

XCOPY /Y *.pb.h/.cc, ClientPacketHandler.h  -> ../../../GameServer
XCOPY /Y *.pb.h/.cc, ServerPacketHandler.h  -> ../../../GameClient

DEL /Q /F *.pb.h *.pb.cc *.h   (Common/Protobuf/bin 안의 사본은 정리)
```
`.proto`를 고치기만 하면 다음 빌드에서 자동으로 재생성/재배치된다 — 최종 산출물은 항상 `GameServer/`, `GameClient/` 폴더 안에만 남는다.

---

## 5. 전체 흐름 — 수신 패킷 하나가 실행되기까지

```
소켓에서 바이트 도착
  -> RecvBuffer (Buffer_Review.md) : 받은 바이트 쌓아둠
  -> PacketSession::OnRecv (Session 관련 문서) : [size][id][data] 단위로 완전한 패킷만 추출
  -> GameSession::OnRecvPacket(buffer, len)   (가상함수 오버라이드)
       ClientPacketHandler::HandlePacket(session, buffer, len);
  -> header->id로 GPacketHandler[id] 조회 (O(1))
  -> Protocol::C_LOGIN 같은 protobuf 메시지로 ParseFromArray 파싱
  -> Handle_C_LOGIN(session, pkt) 실제 게임 로직 실행
```
`Session`이 바이트 스트림을 안전하게 관리하고, `PacketSession`이 패킷 경계를 나누고, `ClientPacketHandler`가 ID로 라우팅하고, 마지막에 실제 `Handle_C_XXX` 함수가 게임 로직을 처리하는 것 — 네트워크 계층 전체가 여기서 하나로 이어진다.

## 정리

| 단계 | 도구 | 산출물 |
|---|---|---|
| 스키마 정의 | 직접 작성 | `Enum.proto`/`Struct.proto`/`Protocol.proto` |
| 직렬화 코드 생성 | `protoc.exe` | `Xxx.pb.h`/`Xxx.pb.cc` (Serialize/Parse) |
| 디스패치 코드 생성 | `GenPackets.exe`(자체 제작) | `ClientPacketHandler.h`/`ServerPacketHandler.h` (ID enum, 함수 테이블) |
| 실행 시점 | MSBuild PreBuildEvent | 매 빌드마다 자동 재생성 + 프로젝트 폴더로 복사 |
| 실제 로직 | 개발자가 직접 작성 | `Handle_C_XXX` 함수 몸통 |
