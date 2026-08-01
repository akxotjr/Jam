#pragma once

#include "jamnet/runtime/session/ClientSession.h"
#include "jamnet/runtime/session/UserContext.h"
#include "jamnet/runtime/world/lifecycle/ClientMainWorldState.h"


namespace jam::net
{
	struct ClientPrincipalState
	{
		AccountId				accountId	= kInvalidAccountId;
		UserId					userId		= kInvalidUserId;
		ClientTcpSession*		tcp		= nullptr;
		ClientUdpSession*		udp		= nullptr;
		ClientMainWorldState	mainWorld	= {};
	};
}
