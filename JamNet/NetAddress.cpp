#include "pch.h"
#include "NetAddress.h"

namespace jam::net
{
	NetAddress::NetAddress(SOCKADDR_IN sockAddr) : _sockAddr(sockAddr)
	{
	}

	NetAddress::NetAddress(wstring ip, uint16 port)
	{
		::memset(&_sockAddr, 0, sizeof(_sockAddr));
		_sockAddr.sin_family = AF_INET;
		_sockAddr.sin_addr = Ip2Address(ip.c_str());
		_sockAddr.sin_port = ::htons(port);
	}

	wstring NetAddress::GetIpAddress() const
	{
		WCHAR buffer[100];
		if (::InetNtopW(AF_INET, &_sockAddr.sin_addr, buffer, len32(buffer)) == nullptr)
		{
			return L"";
		}
		return wstring(buffer);
	}

	bool NetAddress::IsValid() const
	{
		return !GetIpAddress().empty() && GetPort() != 0;
	}

	IN_ADDR NetAddress::Ip2Address(const WCHAR* ip)
	{
		IN_ADDR address = {};
		if (::InetPtonW(AF_INET, ip, &address) != 1)
		{
			// 실패 시 로그 남기고 0.0.0.0 반환
			//::wprintf(L"InetPtonW failed for IP: %s\n", ip);
			address.S_un.S_addr = INADDR_ANY;
		}
		return address;
	}
}
