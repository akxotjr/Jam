#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX


// platform

#include <windows.h>
#include <winnt.h>         //  SLIST_ENTRY, SLIST_HEADER
#include <intrin.h>        //  InterlockedPushEntrySList
#include <WinSock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")


// std

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



// 3rd party

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <entt/entt.hpp>
#include <concurrentqueue/concurrentqueue.h>
#include <concurrentqueue/blockingconcurrentqueue.h>
#include <flatbuffers/flatbuffers.h>

#include "JamNetAPI.h"

