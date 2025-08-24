#include "pch.h"
#include "ShardEndpoint.h"
#include "ShardExecutor.h"
#include "Mailbox.h"

namespace jam::utils::exec
{
	bool ShardEndpoint::Post(const Sptr<Mailbox>& mailbox, job::Job job)
	{
		if (!m_target || !mailbox)
			return false;

		return mailbox->Post(std::move(job));
	}
}
