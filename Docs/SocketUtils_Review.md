# SocketUtils 학습 정리

`SocketUtils`는 상태를 안 가지는 `static` 함수 모음이다. Winsock의 로우레벨 API(`WSASocket`, `setsockopt`, `bind`, `listen`, `closesocket`, ...)를 얇게 감싸서, 나머지 `ServerCore` 코드가 Winsock 세부사항을 직접 안 다뤄도 되게 해준다. 상태가 없으니 인스턴스도 필요 없고, 그래서 전부 `static`이다.

---

## Init() / Clear() — 프로세스 전체에서 딱 한 번

```cpp
void SocketUtils::Init()
{
	WSADATA wsaData;
	::WSAStartup(MAKEWORD(2, 2), &wsaData);

	SOCKET dummySocket = CreateSocket();
	BindWindowsFunction(dummySocket, WSAID_CONNECTEX, &ConnectEx);
	BindWindowsFunction(dummySocket, WSAID_DISCONNECTEX, &DisconnectEx);
	BindWindowsFunction(dummySocket, WSAID_ACCEPTEX, &AcceptEx);
	Close(dummySocket);
}
```

### `WSAStartup` / `WSACleanup`
Winsock은 프로세스 안에서 쓰기 전에 반드시 `WSAStartup`으로 라이브러리 초기화를 해야 한다(버전 협상 포함, 여기선 2.2). 짝을 맞춰 프로세스 종료 시 `WSACleanup`(`Clear()`)을 부른다. 소켓 함수를 쓰는 프로세스(`GameServer`, `GameClient` 둘 다)가 각자 자기 `main()`에서 한 번씩 호출해야 하는 부분.

### 왜 `AcceptEx`/`ConnectEx`/`DisconnectEx`는 그냥 호출할 수 없나
`accept`, `connect`, `send`, `recv` 같은 기본 함수는 Winsock DLL이 표준으로 내보내는(export) 심볼이라 그냥 링크해서 쓰면 된다. 근데 `AcceptEx`/`ConnectEx`/`DisconnectEx`는 **Microsoft의 확장 함수**라서 DLL이 표준 심볼로 내보내지 않는다 — 대신 `WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER)`로 런타임에 함수 포인터를 "물어봐서" 받아와야 한다(`BindWindowsFunction`이 하는 일). 이 확장 함수들은 소켓 하나에 종속된 게 아니라 이 컴퓨터/드라이버가 지원하는 기능이라, **아무 소켓이나 하나(`dummySocket`) 붙잡고 물어본 다음 그 소켓은 바로 버려도 된다.** 받아온 함수 포인터는 `static` 멤버로 캐싱해서 프로세스 내내 재사용한다 — 매번 물어보면 낭비니까.

**비유**: 일반 매장에 늘 진열돼있는 상품(`accept`/`connect`)은 그냥 집어오면 되지만, 일부 특수 사양 부품(`AcceptEx` 등)은 카탈로그에 없어서 "이런 부품 있나요?" 하고 별도로 문의해서 위치를 받아와야(`WSAIoctl`) 하는 것과 비슷하다. 한 번 위치를 알아두면 그다음부터는 바로 찾아갈 수 있어서 캐싱해둔다.

---

## CreateSocket() — IOCP와의 연결고리

```cpp
SOCKET SocketUtils::CreateSocket()
{
	return ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
}
```
평범한 `::socket()`이 아니라 `::WSASocket()`을 **`WSA_FLAG_OVERLAPPED`** 플래그와 함께 호출한다. 이 플래그가 있어야 그 소켓 핸들이 오버랩드(비동기) I/O를 지원하게 되고, `IocpCore::Register()`(`CreateIoCompletionPort`)로 완료포트에 등록해서 `WSARecv`/`WSASend`/`AcceptEx`/`ConnectEx`를 비동기로 걸 수 있게 된다. 이 플래그를 빼먹으면 애초에 IOCP를 쓸 수 있는 소켓이 아니게 되는, 이 프로젝트 전체 구조의 가장 밑단에 있는 설정이다.

---

## 소켓 옵션들 — `setsockopt` 얇은 래퍼

전부 아래 템플릿 하나로 구현돼있다:
```cpp
template<typename T>
static inline bool SetSockOpt(SOCKET socket, int32 level, int32 optName, T optVal)
{
	return SOCKET_ERROR != ::setsockopt(socket, level, optName, reinterpret_cast<char*>(&optVal), sizeof(T));
}
```

| 함수 | 옵션 | 실제 의미 | 이 프로젝트에서 |
|---|---|---|---|
| `SetReuseAddress` | `SO_REUSEADDR` | 서버 재시작 직후 "Address already in use" 에러 없이 같은 포트로 바로 재바인드 허용 | `Listener`(서버 소켓), `Session::RegisterConnect`(클라 소켓) 둘 다 켜서 사용 |
| `SetLinger` | `SO_LINGER` | `closesocket()` 호출 시 동작 제어. `l_onoff=0`(지금 코드가 쓰는 값)이면 기본 그레이스풀 종료 — 즉시 리턴하고 남은 데이터는 백그라운드로 마저 전송 | `Listener`(리슨 소켓)에서 `(0, 0)`으로 호출 — 사실상 기본 동작과 같음 |
| `SetUpdateAcceptSocket` | `SO_UPDATE_ACCEPT_CONTEXT` | `AcceptEx`로 받은 소켓에 리슨 소켓의 속성(옵션)을 물려줌 — 이게 없으면 그 소켓에서 `getpeername`/`getsockname` 등이 제대로 동작 안 함 | `Listener::ProcessAccept`에서 `AcceptEx` 완료 직후 필수로 호출, 그 다음 줄이 실제로 `getpeername`을 쓴다 |
| `SetTcpNoDelay` | `TCP_NODELAY` | Nagle 알고리즘 끄기 — 작은 패킷을 모아서 보내지 않고 즉시 전송(지연 감소, 대신 패킷 수 증가) | **정의만 돼있고 어디서도 호출 안 함** — 실시간성이 중요한 게임 서버라면 보통 켜는 옵션인데 아직 적용 안 된 상태 |
| `SetRecvBufferSize` / `SetSendBufferSize` | `SO_RCVBUF` / `SO_SNDBUF` | OS 커널이 이 소켓을 위해 잡아두는 송/수신 버퍼 크기 조절 | **정의만 돼있고 어디서도 호출 안 함** |

`SetLinger`를 진짜로 "그레이스풀 종료"가 아니라 **강제 종료(abortive close — 남은 데이터 버리고 즉시 RST 전송, `TIME_WAIT` 상태를 건너뜀)**로 쓰려면 `(1, 0)`으로 호출해야 하는데, 지금은 `(0, 0)`이라 사실상 기본값과 동일하다 — 짧은 연결이 아주 많이 발생하는 서버(예: 스트레스 테스트용 클라이언트가 접속/해제를 반복)에서는 `TIME_WAIT` 소켓이 쌓이는 걸 막기 위해 `(1, 0)`을 쓰는 경우가 실무에서 흔하다.

---

## Bind / Listen / Close

- **`Bind(socket, netAddr)`**: 지정한 IP:포트로 바인드. 서버가 "이 주소로 요청을 받겠다"고 OS에 알리는 것.
- **`BindAnyAddress(socket, port)`**: `INADDR_ANY`로 바인드 — 클라이언트가 `ConnectEx`를 걸기 전에 쓴다. `ConnectEx`는 (일반 `connect()`와 달리) **소켓이 미리 바인드돼 있어야만** 동작하는 API라서, `Session::RegisterConnect()`가 `ConnectEx` 호출 직전에 포트 0(=OS가 알아서 빈 포트 하나 골라줌)으로 바인드해두는 용도.
- **`Listen(socket, backlog = SOMAXCONN)`**: 리슨 상태로 전환. `backlog` 기본값이 `SOMAXCONN`(OS가 허용하는 최대치)이라 접속 대기열은 넉넉하게 열려있다. (`Listener` 문서에서 다룬 "정문 앞 줄"이 이 backlog다.)
- **`Close(SOCKET& socket)`**: `INVALID_SOCKET`이 아닐 때만 `closesocket()`을 호출하고, 항상 `INVALID_SOCKET`으로 리셋한다. 이 "항상 리셋" 덕분에 같은 소켓 변수에 대해 `Close()`를 두 번 불러도 안전하다(두 번째는 그냥 스킵) — `Listener`/`Session` 소멸자와 `CloseSocket()`이 중복 호출돼도 문제없는 이유가 여기 있다.

---

## 한 줄 요약
`SocketUtils`는 이 프로젝트에서 "IOCP를 쓸 수 있는 소켓을 만들고(`CreateSocket`), 몇 가지 필수 옵션을 세팅하고(`SetReuseAddress`/`SetUpdateAcceptSocket`), 주소를 묶고 듣게 만드는(`Bind`/`Listen`)" 것까지의 로우레벨 Winsock 작업을 한 곳에 모아둔 얇은 유틸리티 계층이다. 여기 자체에는 비동기/락/참조카운팅 같은 복잡한 로직이 전혀 없다 — 그런 건 전부 `IocpCore`/`Session`/`Listener`가 이 위에서 처리한다.
