#pragma once
#include <jamnet/core/executor/RuntimeId.h>
#include "jamnet/core/executor/Job.h"


namespace jam
{
	class Mailbox;

	struct MailboxRef
	{
		Mailbox*	mailbox		= nullptr;
		RuntimeId	ownerId		= kInvalidRuntimeId;
		uint32		generation	= 0;

		bool IsValid() const;
		bool TryPost(Job j) const;
	};



} // namespace jam
