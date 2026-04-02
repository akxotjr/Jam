#pragma once
#include "concurrentqueue/concurrentqueue.h"

namespace jam
{
    template <typename Q>
    inline moodycamel::ProducerToken& TlsTokenFor(Q& q)
    {
        using Token = moodycamel::ProducerToken;
        using Map   = std::unordered_map<const void*, Token>;

        thread_local Map tl_tokens;

        const void* key = static_cast<const void*>(&q);

        auto it = tl_tokens.find(key);
        if (it == tl_tokens.end())
        {
            auto [insIt, _] = tl_tokens.emplace(key, Token{ q });
            return insIt->second;
        }
        return it->second;
    }
}
