#pragma once

namespace jam::net
{
	class NetAddress
	{
	public:
		NetAddress() = default;
		NetAddress(SOCKADDR_IN sockAddr);
		NetAddress(string ip, uint16 port);

		SOCKADDR_IN&		GetSockAddr() { return m_sockAddr; }
		const SOCKADDR_IN&	GetSockAddr() const { return m_sockAddr; }
		string				GetIpAddress() const;
		uint16				GetPort() const { return ::ntohs(m_sockAddr.sin_port); }

		bool				IsValid() const;

	public:
		static IN_ADDR		Ip2Address(const CHAR* ip);

		bool				operator==(const NetAddress& other) const
		{
			return m_sockAddr.sin_addr.s_addr == other.m_sockAddr.sin_addr.s_addr && m_sockAddr.sin_port == other.m_sockAddr.sin_port;
		}


	public:
		SOCKADDR_IN			m_sockAddr = {};
	};



}

namespace std
{
	template <>
	struct hash<jam::net::NetAddress>
	{
		size_t operator()(const jam::net::NetAddress& addr) const
		{
			return hash<uint32_t>()(addr.GetSockAddr().sin_addr.S_un.S_addr) ^
				(hash<uint16_t>()(addr.GetSockAddr().sin_port) << 1);
		}
	};
}

