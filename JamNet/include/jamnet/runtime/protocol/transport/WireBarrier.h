#pragma once

#include <unordered_map>
#include <vector>

namespace jam::net
{
	enum class eWireBarrierPhase : uint8
	{
		Preparing	= 0,
		Ready		= 1,
		Committing	= 2,
		Applied		= 3,
		Failed		= 4,
		Cancelled	= 5,
	};

	struct WireBarrierToken
	{
		uint64 value = 0;

		bool IsValid() const noexcept { return value != 0; }
		auto operator<=>(const WireBarrierToken&) const = default;
	};

	struct PendingWireBarrier
	{
		uint64				principalId = 0;
		WireBarrierToken	token		= {};
		eWireBarrierPhase	phase		= eWireBarrierPhase::Preparing;
		uint64				startedNs	= 0;
		uint64				deadlineNs	= 0;
	};

	struct WireBarrierMetrics
	{
		uint64	begun				= 0;
		uint64	ready				= 0;
		uint64	applied				= 0;
		uint64	failed				= 0;
		uint64	timedOut			= 0;
		uint64	cancelled			= 0;
		uint64	totalReadyLatencyNs = 0;
	};

	class WireBarrierTable
	{
	public:
		WireBarrierToken					Begin(uint64 principalId, uint64 nowNs, uint64 timeoutNs);
		bool								MarkReady(uint64 principalId, WireBarrierToken token, uint64 nowNs);
		bool								BeginCommit(uint64 principalId, WireBarrierToken token);
		bool								MarkApplied(uint64 principalId, WireBarrierToken token);
		bool								Fail(uint64 principalId, WireBarrierToken token);
		void								CancelPrincipal(uint64 principalId);
		std::vector<PendingWireBarrier>		Expire(uint64 nowNs);
		const PendingWireBarrier*			Find(uint64 principalId, WireBarrierToken token) const;
		const WireBarrierMetrics&			Metrics() const { return m_metrics; }

	private:
		uint64											m_nextToken = 1;
		std::unordered_map<uint64, PendingWireBarrier>	m_pendingByPrincipal;
		std::unordered_map<uint64, PendingWireBarrier>	m_terminalByToken;
		WireBarrierMetrics								m_metrics;
	};
}
