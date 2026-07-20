#include "pch.h"
#include "IocpEvent.h"

/*--------------
	IocpEvent
---------------*/

IocpEvent::IocpEvent(EventType type) : eventType(type)
{
	Init();
}

void IocpEvent::Init()
{
	// 내부 값들은 운영체제가 사용하는 값
	OVERLAPPED::hEvent = 0;
	OVERLAPPED::Internal = 0;
	OVERLAPPED::InternalHigh = 0;
	OVERLAPPED::Offset = 0;
	OVERLAPPED::OffsetHigh = 0;
}
