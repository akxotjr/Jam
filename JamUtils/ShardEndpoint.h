#pragma once
#include "Job.h"
#include "ShardSlot.h"

namespace jam::utils::exec
{
	class ShardExecutor;
	class Mailbox;

	class ShardEndpoint
	{
	public:
		ShardEndpoint(Sptr<ShardExecutor> target) : m_target(std::move(target)) {} 

		ShardEndpoint(ShardSlot* slot, eMailboxChannel channel);


		bool Post(const Sptr<Mailbox>& mailbox, job::Job job);

		bool IsSlotMode() const { return m_slot != nullptr; }

		enum class ePostResult
		{
			OK,
			CLOSED,
			DRAINING,
			STALE,
			UNVAILABLE
		};

		ePostResult Post(job::Job job) const;

	private:
		Sptr<ShardExecutor>		m_target;

		ShardSlot*				m_slot = nullptr; // ºñ¼ÒÀ¯
		eMailboxChannel			m_channel = eMailboxChannel::NORMAL;
		uint32_t				m_gen = 0;       // »ý¼º ½ÃÁ¡ gen ½º³À¼¦
	};
}
