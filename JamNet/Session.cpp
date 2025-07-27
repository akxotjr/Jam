#include "pch.h"
#include "Session.h"

namespace jam::net
{
	Session::Session()
	{
		m_rpcManager = std::make_unique<RpcManager>();
		m_packetHandler = std::make_unique<PacketHandler>();
	}

}


