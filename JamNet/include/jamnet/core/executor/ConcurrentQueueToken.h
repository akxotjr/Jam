#pragma once

#include "concurrentqueue/moodycamel/concurrentqueue.h"

#include <unordered_map>

namespace jam
{
    namespace detail
    {
        template <typename Q>
        auto& TlsTokenMap()
        {
            using Token = moodycamel::ProducerToken;
            using Map   = std::unordered_map<const void*, Token>;

            thread_local Map tl_tokens;
            return tl_tokens;
        }
    }

    template <typename Q>
    inline moodycamel::ProducerToken& TlsTokenFor(Q& q)
    {
        auto& tl_tokens = detail::TlsTokenMap<Q>();

        const void* key = static_cast<const void*>(&q);

        auto it = tl_tokens.find(key);
        if (it == tl_tokens.end())
        {
            auto [insIt, _] = tl_tokens.emplace(key, moodycamel::ProducerToken{ q });
            return insIt->second;
        }
        return it->second;
    }

    template <typename Q>
    inline bool ClearTlsTokenFor(Q& q)
    {
        auto& tl_tokens = detail::TlsTokenMap<Q>();
        return tl_tokens.erase(static_cast<const void*>(&q)) != 0;
    }

    template <typename Q>
    inline void ClearTlsTokensForQueueType()
    {
        detail::TlsTokenMap<Q>().clear();
    }
}
