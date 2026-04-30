#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#ifndef JAMNET_USE_MIMALLOC
#define JAMNET_USE_MIMALLOC 1
#endif

#if JAMNET_USE_MIMALLOC
#pragma comment(lib, "Advapi32.lib")
#include <mimalloc.h>
#include <mimalloc-override.h>
#endif

// platform
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <intrin.h>

#pragma comment(lib, "Mswsock.lib")
#pragma comment(lib, "ws2_32.lib")

// stl

#include <iostream>
#include <algorithm>
#include <vector>
#include <queue>
#include <stack>
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <mutex>
#include <atomic>
#include <thread>
#include <string>
#include <string_view>
#include <format>
#include <limits>
#include <mutex>
#include <future>


// 3rd party

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <entt/entt.hpp>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>
#include <flatbuffers/flatbuffers.h>
#include <xxhash.h>

#include <jambase/JamMacro.h>
#include <jambase/JamTypes.h>
#include <jambase/Logger.h>
#include <jambase/JamAssert.h>


#include "jamnet/core/utils/TimeUnits.h"
#include "jamnet/core/utils/Clock.h"
