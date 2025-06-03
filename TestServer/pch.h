#pragma once


/** JamUtils **/
#ifdef _DEBUG
#pragma comment(lib, "JamUtils\\libjamutils.lib")
#pragma comment(lib, "JamNet\\libjamnet.lib")
#else
#pragma comment(lib, "JamUtils\\libjamutils.lib")
#pragma comment(lib, "JamNet\\libjamnet.lib")
#endif
//
//#include "JamUtilsPCH.h"
#include "JamNetPCH.h"

using namespace jam::utils;
using namespace jam::net;