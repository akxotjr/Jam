#include "pch.h"
#include "jamnet/core/net/NetAddress.h"

namespace jam::net
{
	NetAddress::NetAddress(SOCKADDR_IN sockAddr) : m_sockAddr(sockAddr)
	{
	}

	NetAddress::NetAddress(SOCKADDR* sockAddr) : m_sockAddr(*reinterpret_cast<SOCKADDR_IN*>(sockAddr))
	{
	}

	NetAddress::NetAddress(const std::string& ip, uint16 port)
	{
		::memset(&m_sockAddr, 0, sizeof(m_sockAddr));
		m_sockAddr.sin_family = AF_INET;
		m_sockAddr.sin_addr   = Ip2Address(ip.c_str());
		m_sockAddr.sin_port   = ::htons(port);
	}

	std::string NetAddress::GetIpAddress() const
	{
		CHAR buffer[100];
		if (::inet_ntop(AF_INET, &m_sockAddr.sin_addr, buffer, len32(buffer)) == nullptr)
		{
			return "";
		}
		return std::string(buffer);
	}

	bool NetAddress::IsValid() const
	{
		return !GetIpAddress().empty() && GetPort() != 0;
	}

	IN_ADDR NetAddress::Ip2Address(const CHAR* ip)
	{
		IN_ADDR address = {};
		if (::inet_pton(AF_INET, ip, &address) != 1)
		{
			address.s_addr = INADDR_ANY;
		}

		return address;
	}
}
