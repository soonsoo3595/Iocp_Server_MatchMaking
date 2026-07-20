#pragma once

/*----------------
	IocpObject
	
	IOCP에 등록된 객체
-----------------*/

// enable_shared_from_this : 내부적으로 자기 자신에 대한 weak 포인터를 가짐
class IocpObject : public enable_shared_from_this<IocpObject>
{
public:
	virtual HANDLE GetHandle() abstract;
	// iocpEvent를 통해 어떤 일감인지 확인할 수 있음
	virtual void Dispatch(class IocpEvent* iocpEvent, int32 numOfBytes = 0) abstract;
};

/*--------------
	IocpCore
---------------*/

class IocpCore
{
public:
	IocpCore();
	~IocpCore();

	HANDLE		GetHandle() { return _iocpHandle; }

	// 소켓을 만들면 Iocp에 등록해서 관찰 대상이다라는 것을 알림
	bool		Register(IocpObjectRef iocpObject);
	// 워커 스레드들이 IOCP에 일감이 없나 관찰하는 함수
	bool		Dispatch(uint32 timeoutMs = INFINITE);

private:
	HANDLE		_iocpHandle;
};