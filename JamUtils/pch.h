#pragma once

#define WIN32_LEAN_AND_MEAN


/** Jam Common **/ 
#include <Enums.h>
#include <Macro.h>
#include <Types.h>
#include <Values.h>

/** windows api **/
#include <windows.h>
#include <winnt.h>         // ← SLIST_ENTRY, SLIST_HEADER 등
#include <intrin.h>        // ← InterlockedPushEntrySList 등

/** spdlog **/
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

/** JamUtils **/
#include "Containers.h"
#include "TypeCast.h"
#include "TLS.h"
