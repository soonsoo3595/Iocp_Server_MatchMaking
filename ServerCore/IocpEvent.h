#pragma once

class Session;

enum class EventType : uint8
{
	Connect,
	Disconnect,
	Accept,
	//PreRecv, Recv 이전 단계 정의 -> 0 byte recv
	Recv,
	Send
};

/*--------------
	IocpEvent

	가상 함수를 사용하게 되면 Offset 0번에 가상 함수 테이블이 들어가므로 주의
---------------*/

/// <summary>
/// OVERLAPPED를 상속받아 Offset 0에는 OVERLAPPED 메모리가 있을 것임
/// </summary>
class IocpEvent : public OVERLAPPED
{
public:
	IocpEvent(EventType type);

	void			Init();

public:
	EventType		eventType;
	IocpObjectRef	owner;
};

/*----------------
	ConnectEvent
-----------------*/

class ConnectEvent : public IocpEvent
{
public:
	ConnectEvent() : IocpEvent(EventType::Connect) {}
};

class DisconnectEvent : public IocpEvent
{
public:
	DisconnectEvent() : IocpEvent(EventType::Disconnect) {}
};

/*----------------
	AcceptEvent

	인자가 추가적으로 있을 수 있음
-----------------*/

class AcceptEvent : public IocpEvent
{
public:
	AcceptEvent() : IocpEvent(EventType::Accept) {}

public:
	// 세션을 가지고 있어야 나중에 디스패치를 통해 이벤트를 다시 받았을 때
	// 어떤 세션을 넘겨줬는지 알 수 있기 때문에
	SessionRef session = nullptr;
};

/*----------------
	RecvEvent
-----------------*/

class RecvEvent : public IocpEvent
{
public:
	RecvEvent() : IocpEvent(EventType::Recv) {}
};

/*----------------
	SendEvent
-----------------*/

class SendEvent : public IocpEvent
{
public:
	SendEvent() : IocpEvent(EventType::Send) {}

	// TEMP
	vector<SendBufferRef> sendBuffers;
};