#include "pch.h"
#include "Listener.h"
#include "SocketUtils.h"
#include "IocpEvent.h"
#include "Service.h"

/*--------------
	Listener
---------------*/

Listener::~Listener()
{
	CloseSocket();

	for (AcceptEvent* acceptEvent : _acceptEvents)
	{
		// TODO
		Xdelete(acceptEvent);
	}

	_service = nullptr;
}

bool Listener::StartAccept(ServerServiceRef service)
{
	_service = service;
	if (_service == nullptr)
		return false;

	_socket = SocketUtils::CreateSocket();
	if (_socket == INVALID_SOCKET)
	{
		LOG_ERROR(L"Listener::StartAccept CreateSocket failed. errCode=%d", ::WSAGetLastError());
		return false;
	}

	if (_service->Register(shared_from_this()) == false)
	{
		LOG_ERROR(L"Listener::StartAccept Register to IOCP failed");
		return false;
	}

	if (SocketUtils::SetReuseAddress(_socket, true) == false)
	{
		LOG_ERROR(L"Listener::StartAccept SetReuseAddress failed. errCode=%d", ::WSAGetLastError());
		return false;
	}

	if (SocketUtils::SetLinger(_socket, 0, 0) == false)
	{
		LOG_ERROR(L"Listener::StartAccept SetLinger failed. errCode=%d", ::WSAGetLastError());
		return false;
	}

	if (SocketUtils::Bind(_socket, _service->GetNetAddress()) == false)
	{
		LOG_ERROR(L"Listener::StartAccept Bind failed. errCode=%d", ::WSAGetLastError());
		return false;
	}

	if (SocketUtils::Listen(_socket) == false)
	{
		LOG_ERROR(L"Listener::StartAccept Listen failed. errCode=%d", ::WSAGetLastError());
		return false;
	}

	LOG_INFO(L"Listener Socket listening on %s:%d", _service->GetNetAddress().GetIpAddress().c_str(), _service->GetNetAddress().GetPort());

	const int32 maxAcceptCount = _service->GetMaxSessionCount();
	for (int32 i = 0; i < maxAcceptCount; i++)
	{
		AcceptEvent* acceptEvent = Xnew<AcceptEvent>();
		if (acceptEvent != nullptr)
		{
			// TODO : AcceptEvent의 owner를 release하는 지점이 없어서 Listener가 refcount상
			// 절대 소멸되지 않음(Session의 recv/send 이벤트와 달리 ProcessAccept가 끝나도 계속
			// 재사용/재등록되기 때문). Listener 종료 플래그를 두고, 종료 중일 때
			// ProcessAccept/RegisterAccept의 재시도 분기에서 재등록 대신 SetOwner(nullptr)로
			// 풀어주는 처리가 필요함.
			acceptEvent->SetOwner(shared_from_this());
			_acceptEvents.push_back(acceptEvent);
			RegisterAccept(acceptEvent);
		}
		else
		{
			LOG_ERROR(L"Failed to Xnew acceptEvent");
		}
	}

	LOG_INFO(L"Listener::StartAccept posted %d AcceptEx", _acceptEvents.size());

	return true;
}

void Listener::CloseSocket()
{
	SocketUtils::Close(_socket);
}

HANDLE Listener::GetHandle()
{
	return reinterpret_cast<HANDLE>(_socket);
}

void Listener::Dispatch(IocpEvent* iocpEvent, int32 numOfBytes)
{
	ASSERT_CRASH(iocpEvent->GetEventType() == EventType::Accept);
	AcceptEvent* acceptEvent = static_cast<AcceptEvent*>(iocpEvent);
	ProcessAccept(acceptEvent);
}

void Listener::RegisterAccept(AcceptEvent* acceptEvent)
{
	SessionRef session = _service->CreateSession();
	if (session == nullptr)
	{
		LOG_WARNING(L"Listener::RegisterAccept CreateSession failed, retrying");
		RegisterAccept(acceptEvent);
		return;
	}

	acceptEvent->Init();
	acceptEvent->session = session;

	DWORD bytesReceived = 0;
	if (false == SocketUtils::AcceptEx(_socket, session->GetSocket(), session->_recvBuffer.WritePos(), 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, OUT & bytesReceived, static_cast<LPOVERLAPPED>(acceptEvent)))
	{
		const int32 errorCode = ::WSAGetLastError();
		if (errorCode != WSA_IO_PENDING)
		{
			LOG_ERROR(L"Listener::RegisterAccept AcceptEx failed. errCode=%d", errorCode);
			RegisterAccept(acceptEvent);
			return;
		}
	}

	LOG_VERBOSE(L"Listener::RegisterAccept AcceptEx posted, waiting for connection");
}

void Listener::ProcessAccept(AcceptEvent* acceptEvent)
{
	SessionRef session = acceptEvent->session;

	if (false == SocketUtils::SetUpdateAcceptSocket(session->GetSocket(), _socket))
	{
		LOG_ERROR(L"Listener::ProcessAccept SetUpdateAcceptSocket failed. errCode=%d", ::WSAGetLastError());
		RegisterAccept(acceptEvent);
		return;
	}

	SOCKADDR_IN sockAddress;
	int32 sizeOfSockAddr = sizeof(sockAddress);
	if (SOCKET_ERROR == ::getpeername(session->GetSocket(), OUT reinterpret_cast<SOCKADDR*>(&sockAddress), &sizeOfSockAddr))
	{
		LOG_ERROR(L"Listener::ProcessAccept getpeername failed. errCode=%d", ::WSAGetLastError());
		RegisterAccept(acceptEvent);
		return;
	}

	session->SetNetAddress(NetAddress(sockAddress));
	session->ProcessConnect();

	LOG_INFO(L"Client Connected! %s:%d", session->GetAddress().GetIpAddress().c_str(), session->GetAddress().GetPort());

	// TODO

	RegisterAccept(acceptEvent);
}