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



/** JamUtils **/


#include "Types.h"
#include "Enums.h"
#include "Values.h"
#include "Macro.h"

/** jam::utils::memory **/
#include "Containers.h"
#include "TypeCast.h"
#include "MemoryPool.h"
#include "MemoryManager.h"

/** jam::utils::thread **/
#include "TLS.h"
#include "Lock.h"

/** jam::utils::job **/
#include "Job.h"
#include "JobQueue.h"