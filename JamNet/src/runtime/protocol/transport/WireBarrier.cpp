#include "pch.h"
#include "jamnet/runtime/protocol/transport/WireBarrier.h"

namespace jam::net
{
	WireBarrierToken WireBarrierTable::Begin(uint64 principalId, uint64 nowNs, uint64 timeoutNs)
	{
		CancelPrincipal(principalId);
		WireBarrierToken token{ m_nextToken++ };
		m_pendingByPrincipal[principalId] = PendingWireBarrier
		{
			.principalId = principalId,
			.token		 = token,
			.startedNs	 = nowNs,
			.deadlineNs  = nowNs + timeoutNs,
		};
		++m_metrics.begun;
		return token;
	}

	bool WireBarrierTable::MarkReady(uint64 principalId, WireBarrierToken token, uint64 nowNs)
	{
		auto it = m_pendingByPrincipal.find(principalId);
		if (it == m_pendingByPrincipal.end() || it->second.token != token || it->second.phase != eWireBarrierPhase::Preparing)
			return false;
		it->second.phase = eWireBarrierPhase::Ready;
		++m_metrics.ready;
		m_metrics.totalReadyLatencyNs += nowNs >= it->second.startedNs ? nowNs - it->second.startedNs : 0;
		return true;
	}

	bool WireBarrierTable::BeginCommit(uint64 principalId, WireBarrierToken token)
	{
		auto it = m_pendingByPrincipal.find(principalId);
		if (it == m_pendingByPrincipal.end() || it->second.token != token || it->second.phase != eWireBarrierPhase::Ready)
			return false;
		it->second.phase = eWireBarrierPhase::Committing;
		return true;
	}

	bool WireBarrierTable::MarkApplied(uint64 principalId, WireBarrierToken token)
	{
		auto it = m_pendingByPrincipal.find(principalId);
		if (it == m_pendingByPrincipal.end() || it->second.token != token || it->second.phase != eWireBarrierPhase::Committing)
			return false;
		it->second.phase = eWireBarrierPhase::Applied;
		m_terminalByToken[token.value] = it->second;
		m_pendingByPrincipal.erase(it);
		++m_metrics.applied;
		return true;
	}

	bool WireBarrierTable::Fail(uint64 principalId, WireBarrierToken token)
	{
		auto it = m_pendingByPrincipal.find(principalId);
		if (it == m_pendingByPrincipal.end() || it->second.token != token)
			return false;

		it->second.phase = eWireBarrierPhase::Failed;
		m_terminalByToken[token.value] = it->second;
		m_pendingByPrincipal.erase(it);
		++m_metrics.failed;
		return true;
	}

	void WireBarrierTable::CancelPrincipal(uint64 principalId)
	{
		auto it = m_pendingByPrincipal.find(principalId);
		if (it == m_pendingByPrincipal.end())
			return;
		it->second.phase = eWireBarrierPhase::Cancelled;
		m_terminalByToken[it->second.token.value] = it->second;
		m_pendingByPrincipal.erase(it);
		++m_metrics.cancelled;
	}

	std::vector<PendingWireBarrier> WireBarrierTable::Expire(uint64 nowNs)
	{
		std::vector<PendingWireBarrier> expired;
		for (auto it = m_pendingByPrincipal.begin(); it != m_pendingByPrincipal.end();)
		{
			if (it->second.deadlineNs > nowNs)
			{
				++it;
				continue;
			}
			it->second.phase = eWireBarrierPhase::Failed;
			expired.push_back(it->second);
			m_terminalByToken[it->second.token.value] = it->second;
			++m_metrics.timedOut;
			it = m_pendingByPrincipal.erase(it);
		}
		return expired;
	}

	const PendingWireBarrier* WireBarrierTable::Find(uint64 principalId, WireBarrierToken token) const
	{
		auto it = m_pendingByPrincipal.find(principalId);
		return it != m_pendingByPrincipal.end() && it->second.token == token ? &it->second : nullptr;
	}
}
