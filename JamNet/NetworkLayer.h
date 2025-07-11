#pragma once

namespace jam::net
{
	class NetworkLayer
	{
	public:
	protected:
		Sptr<Service> m_service = nullptr;
	};


	class ClientNetworkLayer : public NetworkLayer
	{
		
	};

	class ServerNetworkLayer : public NetworkLayer
	{
		
	};
}

