#pragma once
#include "Job.h"


namespace jam::utils::exec
{
	class ShardExecutor;
	class Mailbox;

	class ShardEndpoint
	{
	public:
		ShardEndpoint(Sptr<ShardExecutor> target) : m_target(std::move(target)) {} 

		bool Post(const Sptr<Mailbox>& mailbox, job::Job job);


	private:
		Sptr<ShardExecutor>		m_target;
	};
}
