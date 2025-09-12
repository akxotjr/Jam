#pragma once

/** base **/
#include <iostream>


/** windows api **/
#include <windows.h>
#include <winnt.h>         // <- SLIST_ENTRY, SLIST_HEADER
#include <intrin.h>        // <- InterlockedPushEntrySList

/** spdlog **/
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>


/** entt **/
#include <entt/entt.hpp>

/** JamUtils **/

#include "JamTypes.h"
#include "JamEnums.h"
#include "JamValues.h"
#include "JamMacro.h"
#include "TimeUnits.h"

/** jam::utils::memory **/
#include "Containers.h"
#include "TypeCast.h"
#include "MemoryPool.h"
#include "MemoryManager.h"
#include "ObjectPool.h"

#include "Logger.h"

/** jam::utils::thread **/
#include "TLS.h"
#include "Lock.h"
#include "DeadLockProfiler.h"
#include "Worker.h"
#include "WorkerPool.h"
#include "FiberScheduler.h"

/** jam::utils::job **/
#include "Job.h"
#include "JobQueue.h"