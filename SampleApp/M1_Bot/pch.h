#pragma once

// Prevent windows.h from pulling in winsock.h and defining min/max macros
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// Winsock2 must be included before windows.h
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#pragma comment(lib, "ws2_32.lib")

#include <windows.h>
#include <winnt.h>         //  SLIST_ENTRY, SLIST_HEADER
#include <intrin.h>        //  InterlockedPushEntrySList

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
#include <random>
#include <future>
#include <conio.h>
#include <filesystem>


// 3rd party

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <entt/entt.hpp>
#include <concurrentqueue/moodycamel/concurrentqueue.h>
#include <concurrentqueue/moodycamel/blockingconcurrentqueue.h>
#include <flatbuffers/flatbuffers.h>

#include "jamnet/JamNet.h"
