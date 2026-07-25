#include "pch.h"
#include "IocpCore.h"
#include "IocpEvent.h"

/*--------------
	IocpCore
---------------*/

IocpCore::IocpCore()
{
	_iocpHandle = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (_iocpHandle == INVALID_HANDLE_VALUE)
		LOG_FATAL(L"IocpCore::IocpCore CreateIoCompletionPort failed. errCode=%u", ::GetLastError());
	else
		LOG_INFO(L"IocpCore::IocpCore IOCP handle created. handle=0x%p", _iocpHandle);

	ASSERT_CRASH(_iocpHandle != INVALID_HANDLE_VALUE);
}

IocpCore::~IocpCore()
{
	::CloseHandle(_iocpHandle);
}

bool IocpCore::Register(IocpObjectRef iocpObject)
{
	if (::CreateIoCompletionPort(iocpObject->GetHandle(), _iocpHandle, 0, 0) == nullptr)
	{
		LOG_ERROR(L"IocpCore::Register failed. errCode=%u", ::GetLastError());
		return false;
	}

	LOG_VERBOSE(L"%hs Register succeeded, handle=0x%p", typeid(*iocpObject).name(), iocpObject->GetHandle());
	return true;
}

bool IocpCore::Dispatch(uint32 timeoutMs)
{
	DWORD numOfBytes = 0;
	ULONG_PTR key = 0;
	IocpEvent* iocpEvent = nullptr;

	if (::GetQueuedCompletionStatus(_iocpHandle, OUT & numOfBytes, OUT &key, OUT reinterpret_cast<LPOVERLAPPED*>(&iocpEvent), timeoutMs))
	{
		IocpObjectRef iocpObject = iocpEvent->GetOwner();
		iocpObject->Dispatch(iocpEvent, numOfBytes);
	}
	else
	{
		int32 errCode = ::WSAGetLastError();
		switch (errCode)
		{
		case WAIT_TIMEOUT:
			return false;

		default:
			if (iocpEvent == nullptr)
			{
				LOG_ERROR(L"IocpCore::Dispatch GQCS failed with null event. errCode=%d", errCode);
				return false;
			}

			IocpObjectRef iocpObject = iocpEvent->GetOwner();
			ASSERT_CRASH(iocpObject != nullptr);

			LOG_WARNING(L"IocpCore::Dispatch GQCS failed. errCode=%d", errCode);
			iocpObject->Dispatch(iocpEvent, numOfBytes);
			break;
		}
	}

	return true;
}
