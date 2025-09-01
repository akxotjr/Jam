#pragma once
class Components
{
};

namespace jam::net::ecs
{
	struct SessionRef
	{
		std::weak_ptr<Session> wp;
	};

	struct MailboxRef
	{
		std::shared_ptr<utils::exec::Mailbox> norm;
		std::shared_ptr<utils::exec::Mailbox> ctrl;
	};
}
