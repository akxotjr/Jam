#pragma once


enum class eProtocolType : uint8
{
	TCP = 0,
	UDP = 1
};

enum class ePeerType
{
	Client,
	Server,

	None
};

enum class eSessionState : uint8
{
	Connect = 0,
	Disconnect,
};