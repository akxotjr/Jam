#pragma once

#define WIN32_LEAN_AND_MEAN

/**------------**/
/**  JamUtils  **/
/**------------**/
#ifdef _DEBUG
#pragma comment(lib, "JamUtils\\libjamutils.lib")
#else
#pragma comment(lib, "JamUtils\\libjamutils.lib")
#endif

#include "JamUtilsPCH.h"

/**----------**/
/**  JamNet  **/
/**----------**/

/** Windows api **/
#include <WinSock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "JamNetEnums.h"

/** Packet compress **/
#include "ipzip.h"
#include "lz4.h"

/** Flatbuffer **/
#include "flatbuffers/flatbuffers.h"

/** JamNet Core **/
#include "IocpCore.h"
#include "IocpEvent.h"
#include "SocketUtils.h"
#include "NetAddress.h"
#include "RecvBuffer.h"
#include "SendBuffer.h"
#include "Service.h"
#include "Session.h"
#include "TcpSession.h"
#include "UdpSession.h"
