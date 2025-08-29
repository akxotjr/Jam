#pragma once
#include "concurrentqueue/concurrentqueue.h"
#include <unordered_map>

namespace jam::utils::exec
{
    template <typename Q>
    inline moodycamel::ProducerToken& TlsTokenFor(Q& q)
    {
        static thread_local std::unordered_map<const void*, std::unique_ptr<moodycamel::ProducerToken>> tl_tokens;
        void* key = static_cast<void*>(&q);
        auto it = tl_tokens.find(key);
        if (it == tl_tokens.end())
        {
            auto ins = tl_tokens.emplace(key, std::make_unique<moodycamel::ProducerToken>(q));
            return *ins.first->second;
        }
        return *it->second;
    }
}
