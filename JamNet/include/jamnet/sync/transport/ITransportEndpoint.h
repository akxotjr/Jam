#pragma once
#include "jamnet/core/net/Buffer.h"
#include "jamnet/sync/transport/IRpcEndpoint.h"


namespace jam::net
{
	enum class eTransportMethod : uint8
	{
		Single,			// 1명
		Multicast,		// 월드 대상 전체에게 동일 payload
		FanOut,			// 월드 대상 전체에게 per-target payload
		Broadcast		// 전체 접속 유저에게 동일 payload
	};

	struct TransportInfo
	{
		eTransportMethod method = eTransportMethod::Single;

		// SINGLE에서 사용
		uint64 userId = 0;

		// MULTICAST / FAN_OUT에서 사용
		uint32 worldId = 0;

		// FAN_OUT: userId별로 보내는 payload를 생성하는 팩토리.
		// - nullptr이면 Send(packet)를 그대로 복제해서 보내는 fallback fanout로 동작
		// - 반환 packet이 invalid이면 해당 유저는 스킵
		using PayloadFactory = std::function<Packet(uint64)>;
		PayloadFactory payloadFactory{};
	};


	class ITransportEndpoint : public IRpcEndpoint
	{
	public:
		virtual ~ITransportEndpoint() override = default;

		virtual void Send(const TransportInfo& info, Packet packet) = 0;
		virtual void EnumerateWorldUsers(uint32 worldId, const std::function<void(uint64)>& fn) = 0;
	};
}
