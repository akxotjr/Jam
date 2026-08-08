#include "pch.h"
#include "jamnet/runtime/world/lifecycle/ClientMainWorldState.h"

namespace jam::net
{
	bool ClientMainWorldState::Prepare(const ClientWorldPrepare& prepare, const MailboxRef& mailbox, std::shared_ptr<ClientWorld> worldObject)
	{
		if (!prepare.token.IsValid() || !prepare.correlation.world.IsValid() || !mailbox.IsValid())
			return false;

		const bool validRevision = prepare.kind == eWorldSyncKind::WorldResync
			? prepare.correlation.mainRevision >= m_revision
			: prepare.correlation.mainRevision == m_revision;
		if (!validRevision)
			return false;

		m_prepared = ClientWorldBinding
		{
			.world = prepare.correlation.world,
			.syncToken = prepare.token,
			.kind = prepare.kind,
			.mailbox = mailbox,
			.worldObject = std::move(worldObject),
			.prepared = true,
			.packetReady = true,
			.active = false,
			.presentationReady = false,
		};
		return m_prepared->world.IsValid();
	}

	bool ClientMainWorldState::Commit(const ClientWorldCommit& commit)
	{
		if (!m_prepared || m_prepared->syncToken != commit.token
			|| m_prepared->world != commit.correlation.world)
			return false;

		const bool validRevision = m_prepared->kind == eWorldSyncKind::WorldResync
			? commit.correlation.mainRevision >= m_revision
			: commit.correlation.mainRevision > m_revision;
		if (!validRevision)
			return false;

		m_prepared->active = true;
		m_current = std::move(m_prepared);
		m_prepared.reset();
		m_revision = commit.correlation.mainRevision;
		return true;
	}

	bool ClientMainWorldState::MarkPresentationReady(const WorldRef& world)
	{
		if (!m_current || m_current->world != world
			|| !m_current->prepared || !m_current->packetReady || !m_current->active)
			return false;

		m_current->presentationReady = true;
		return true;
	}

	bool ClientMainWorldState::ApplyAuthoritative(const UserWorldState& state)
	{
		if (!state.IsValid() || state.revision < m_revision)
			return false;
		if (state.main)
			return state.revision == m_revision && m_current && m_current->world == *state.main;
		if (state.revision == m_revision)
			return !m_current;

		m_prepared.reset();
		m_current.reset();
		m_revision = state.revision;
		return true;
	}

	void ClientMainWorldState::Cancel(WorldSyncToken token)
	{
		if (m_prepared && m_prepared->syncToken == token)
			m_prepared.reset();
	}

	void ClientMainWorldState::Clear()
	{
		m_prepared.reset();
		m_current.reset();
		m_revision = 0;
	}

	const MailboxRef* ClientMainWorldState::ActiveMailbox() const
	{
		return m_current && m_current->active && m_current->packetReady
			? &m_current->mailbox : nullptr;
	}
}
