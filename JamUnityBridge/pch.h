#ifndef PCH_H
#define PCH_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <WinSock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <intrin.h>

#pragma comment(lib, "Mswsock.lib")
#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

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
#include <jambase/Fnv1a.h>
#include <jambase/Logger.h>
#include <jambase/JamAssert.h>


#include "jamnet/core/utils/TimeUnits.h"
#include "jamnet/core/utils/Clock.h"

#endif
