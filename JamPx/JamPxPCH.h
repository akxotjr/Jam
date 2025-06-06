#pragma once

/** JamUtils **/
#ifdef _DEBUG
#pragma comment(lib, "JamUtils\\libjamutils.lib")
#else
#pragma comment(lib, "JamUtils\\libjamutils.lib")
#endif

#include "JamUtilsPCH.h"

/** PhysX 5.5 **/
#include <physx/PxPhysicsAPI.h>
using namespace physx;



/** entt **/
#include <entt/entt.hpp>