#pragma once

#include "jamnet/core/executor/MailboxRef.h"
#include "jamnet/runtime/world/lifecycle/WorldTransitionTypes.h"

#include <optional>
#include <memory>

#include "jamnet/runtime/protocol/transport/WireBarrier.h"

namespace jam::net
{
	class ClientWorld;

	struct ClientWorldBinding
	{
		WorldRuntimeRef		world			 = {};
		WireBarrierToken	barrierToken	 = {};
		MailboxRef			mailbox			 = {};
		// Client-only lifetime ownership. The mailbox owner is the opaque wire
		// runtime ID; no client-local World ID is exposed or persisted.
		std::shared_ptr<ClientWorld> runtime = {};
		bool				prepared		  = false;
		bool				packetReady		  = false;
		bool				active			  = false;
		bool				presentationReady = false;
	};

	class ClientMainWorldState
	{
	public:
		bool										Prepare(const ClientWorldPrepare& prepare, const MailboxRef& mailbox, std::shared_ptr<ClientWorld> runtime = {});
		bool										Commit(const ClientWorldCommit& commit);
		bool										MarkPresentationReady(const WorldRuntimeRef& world);
		bool										ApplyAuthoritative(const UserPhysicalWorldState& state);
		void										Cancel(WireBarrierToken token);
		void										Clear();

		const std::optional<ClientWorldBinding>&	Current() const { return m_current; }
		const std::optional<ClientWorldBinding>&	Prepared() const { return m_prepared; }
		WorldStateRevision							Revision() const { return m_revision; }
		const MailboxRef*							ActiveMailbox() const;

	private:
		std::optional<ClientWorldBinding>	m_current;
		std::optional<ClientWorldBinding>	m_prepared;
		WorldStateRevision					m_revision = 0;
	};
}
