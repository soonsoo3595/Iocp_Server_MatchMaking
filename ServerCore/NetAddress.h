#pragma once

/*--------------
	NetAddress
---------------*/

/// <summary>
/// 클라이언트의 IP나 이런걸 알고 싶을 때 함수를 매번 부르기보다 래핑해서 사용하기 위해
/// </summary>
class NetAddress
{
public:
	NetAddress() = default;
	NetAddress(SOCKADDR_IN sockAddr);
	NetAddress(wstring ip, uint16 port);

	SOCKADDR_IN&	GetSockAddr() { return _sockAddr; }
	wstring			GetIpAddress();
	uint16			GetPort() { return ::ntohs(_sockAddr.sin_port); }

public:
	// 헬퍼
	static IN_ADDR	Ip2Address(const WCHAR* ip);

private:
	SOCKADDR_IN		_sockAddr = {};
};

