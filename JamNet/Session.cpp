#include "pch.h"
#include "Session.h"
#include "RpcManager.h"
#include "PacketBuilder.h"

namespace jam::net
{
	Session::Session()
	{
		m_rpcManager = std::make_unique<RpcManager>();
		m_packetBuilder = std::make_unique<PacketBuilder>();
	}

}


