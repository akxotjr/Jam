#include "pch.h"
#include "jamnet/core/executor/MailboxRef.h"
#include "jamnet/core/executor/Mailbox.h"

namespace jam
{
	bool MailboxRef::IsValid() const
	{
		return mailbox != nullptr
			&& ownerId != kInvalidRuntimeId
			&& generation != 0
			&& generation == GetRuntimeGeneration(ownerId)
			&& mailbox->IsAcceptingPosts();
	}

	bool MailboxRef::TryPost(Job j) const
	{
		if (!IsValid())
			return false;

		return mailbox->Post(std::move(j));
	}
}
