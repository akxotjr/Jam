#include "pch.h"
#include "jamnet/runtime/world/lifecycle/ClientMainWorldState.h"

namespace jam::net
{
	bool ClientMainWorldState::Prepare(const ClientWorldPrepare& prepare, const MailboxRef& mailbox, std::shared_ptr<ClientWorld> runtime)
	{
		if (!prepare.token.IsValid() || !prepare.correlation.world.IsValid() || !mailbox.IsValid()
			|| prepare.correlation.mainRevision != m_revision)
			return false;

		m_prepared = ClientWorldBinding
		{
			.world = prepare.correlation.world,
			.barrierToken = prepare.token,
			.mailbox = mailbox,
			.runtime = std::move(runtime),
			.prepared = true,
			.packetReady = true,
			.active = false,
			.presentationReady = false,
		};
		return m_prepared->world.IsValid();
	}

	bool ClientMainWorldState::Commit(const ClientWorldCommit& commit)
	{
		if (!m_prepared || m_prepared->barrierToken != commit.token
			|| m_prepared->world != commit.correlation.world
			|| commit.correlation.mainRevision <= m_revision)
			return false;

		m_prepared->active = true;
		m_current = std::move(m_prepared);
		m_prepared.reset();
		m_revision = commit.correlation.mainRevision;
		return true;
	}

	bool ClientMainWorldState::MarkPresentationReady(const WorldRuntimeRef& world)
	{
		if (!m_current || m_current->world != world
			|| !m_current->prepared || !m_current->packetReady || !m_current->active)
			return false;

		m_current->presentationReady = true;
		return true;
	}

	bool ClientMainWorldState::ApplyAuthoritative(const UserPhysicalWorldState& state)
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

	void ClientMainWorldState::Cancel(WireBarrierToken token)
	{
		if (m_prepared && m_prepared->barrierToken == token)
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
