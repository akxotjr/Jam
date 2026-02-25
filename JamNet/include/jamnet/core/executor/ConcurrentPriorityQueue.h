#pragma once

#include <algorithm>
#include <atomic>
#include <memory>
#include <span>

namespace jam
{
    //template<class PriorityT>
    //struct DefaultPriorityLess
    //{
    //    constexpr bool operator()(const PriorityT& a, const PriorityT& b) const noexcept
    //    {
    //        return a < b;
    //    }
    //};

    //template<
    //    class ItemT,
    //    class PriorityT,
    //    class PriorityOfT,
    //    class PriorityLessT = DefaultPriorityLess<PriorityT>
    //>
    //class ConcurrentPriorityQueue
    //{
    //private:
    //    struct NodeKey
    //    {
    //        PriorityT priority{};
    //        uint64 seq{};

    //        bool operator<(const NodeKey& other) const noexcept
    //        {
    //            PriorityLessT less{};
    //            if (less(priority, other.priority)) return true;
    //            if (less(other.priority, priority)) return false;
    //            return seq < other.seq;
    //        }
    //    };

    //    struct Node : NodeKey
    //    {
    //        ItemT value{};

    //        Node() = default;

    //        Node(const NodeKey& key) noexcept
    //            : NodeKey(key)
    //        {
    //        }

    //        Node(PriorityT prio, uint64 seq, ItemT&& val)
    //        {
    //            this->priority = prio;
    //            this->seq = seq;
    //            value = std::move(val);
    //        }
    //    };

    //    using SkipListT = folly::ConcurrentSkipList<Node>;
    //    using AccessorT = SkipListT::Accessor;

    //public:
    //    explicit ConcurrentPriorityQueue(uint32 maxHeight = 24)
    //        : m_skipList(SkipListT::createInstance(static_cast<int>(maxHeight)))
    //        , m_seqGen(1)
    //    {
    //    }

    //    void Enqueue(PriorityT priority, ItemT&& item)
    //    {
    //        const uint64 seq = m_seqGen.fetch_add(1, std::memory_order_relaxed);
    //        Node n{ priority, seq, std::move(item) };

    //        AccessorT accessor(m_skipList);
    //        accessor.insert(n);
    //    }

    //    void Enqueue(ItemT item)
    //    {
    //        PriorityOfT of{};
    //        Enqueue(of(item), std::move(item));
    //    }

    //    void EnqueueBulk(std::span<ItemT> items)
    //    {
    //        AccessorT accessor(m_skipList);

    //        PriorityOfT of{};
    //        PriorityLessT less{};

    //        if (accessor.size() > kThreshold)
    //        {
    //            std::stable_sort(items.begin(), items.end(),
    //                [&](const ItemT& a, const ItemT& b) noexcept
    //                {
    //                    const PriorityT pa = of(a);
    //                    const PriorityT pb = of(b);
    //                    return less(pa, pb);
    //                });
    //        }

    //        for (auto& item : items)
    //        {
    //            const uint64 seq = m_seqGen.fetch_add(1, std::memory_order_relaxed);
    //            Node n{ of(item), seq, std::move(item) };
    //            accessor.insert(n);
    //        }
    //    }

    //    bool TryDequeue(ItemT& out)
    //    {
    //        AccessorT accessor(m_skipList);
    //        auto it = accessor.begin();
    //        if (it == accessor.end()) return false;

    //        out = std::move(const_cast<Node&>(*it).value);

    //        const NodeKey key{ it->priority, it->seq };
    //        accessor.erase(Node{ key });

    //        return true;
    //    }

    //    size_t TryDequeueBulk(ItemT* outs, size_t maxCount)
    //    {
    //        if (!outs || maxCount == 0) return 0;

    //        AccessorT accessor(m_skipList);
    //        size_t count = 0;

    //        auto it = accessor.begin();
    //        while (it != accessor.end() && count < maxCount)
    //        {
    //            const NodeKey key{ it->priority, it->seq };
    //            outs[count++] = std::move(const_cast<Node&>(*it).value);

    //            ++it;
    //            accessor.erase(Node{ key });
    //        }

    //        return count;
    //    }

    //    bool Empty() const
    //    {
    //        AccessorT accessor(m_skipList);
    //        return accessor.empty();
    //    }

    //    size_t Size() const
    //    {
    //        AccessorT accessor(m_skipList);
    //        return accessor.size();
    //    }

    //private:
    //    shared_ptr<SkipListT>   m_skipList;
    //    atomic<uint64>          m_seqGen;

    //    static constexpr size_t kThreshold = 24;
    //};
}