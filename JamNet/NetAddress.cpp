#include "pch.h"
#include "NetAddress.h"

namespace jam::net
{
	NetAddress::NetAddress(SOCKADDR_IN sockAddr) : m_sockAddr(sockAddr)
	{
	}

	NetAddress::NetAddress(string ip, uint16 port)
	{
		::memset(&m_sockAddr, 0, sizeof(m_sockAddr));
		m_sockAddr.sin_family = AF_INET;
		m_sockAddr.sin_addr = Ip2Address(ip.c_str());
		m_sockAddr.sin_port = ::htons(port);
	}

	string NetAddress::GetIpAddress() const
	{
		CHAR buffer[100];
		if (::inet_ntop(AF_INET, &m_sockAddr.sin_addr, buffer, len32(buffer)) == nullptr)
		{
			return "";
		}
		return string(buffer);
	}

	bool NetAddress::IsValid() const
	{
		return !GetIpAddress().empty() && GetPort() != 0;
	}

	IN_ADDR NetAddress::Ip2Address(const CHAR* ip)
	{
		//IN_ADDR address = {};
		//if (::InetPtonW(AF_INET, ip, &address) != 1)
		//{
		//	// 실패 시 로그 남기고 0.0.0.0 반환
		//	//::wprintf(L"InetPtonW failed for IP: %s\n", ip);
		//	address.s_addr = INADDR_ANY;
		//}

		IN_ADDR address = {};
		if (::inet_pton(AF_INET, ip, &address) != 1)
		{
			address.s_addr = INADDR_ANY;
		}

		return address;
	}
}
