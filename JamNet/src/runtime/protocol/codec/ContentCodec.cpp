#include "pch.h"
#include "jamnet/runtime/protocol/codec/ContentCodec.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/runtime/protocol/codec/RuntimePacketCodec.h"
#include "jamnet/runtime/protocol/schema/gen/content_generated.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"

namespace jam::net::codec
{
	namespace
	{
		constexpr size_t kEnvelopeOverheadEstimate = 128;

		Packet Encode(
			flatbuffers::FlatBufferBuilder& fbb, 
			fb::fbContentEnvelopeKind kind, 
			ClientRequestId requestId, 
			GenericContentOperationCode opCode,
			eGenericContentResponseStatus status, 
			uint32 resultCode, 
			const std::vector<std::byte>& data)
		{
			const auto payload = fbb.CreateVector(reinterpret_cast<const uint8*>(data.data()), data.size());
			fb::FinishfbContentEnvelopeBuffer(fbb, fb::CreatefbContentEnvelope(fbb, kind, requestId, opCode, static_cast<fb::fbContentResponseStatus>(status), resultCode, payload));
			
			return PacketBuilder::CreateCustomPacket(CustomPacketId::CONTENT, PacketFlags::NONE, eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()));
		}

		Packet EncodeRequest(flatbuffers::FlatBufferBuilder& fbb, const GenericContentRequest& request)
		{
			return Encode(fbb, fb::fbContentEnvelopeKind_Request, request.requestId, request.opCode, eGenericContentResponseStatus::None, 0, request.payload);
		}

		Packet EncodeResponse(flatbuffers::FlatBufferBuilder& fbb, const GenericContentResponse& response)
		{
			return Encode(fbb, fb::fbContentEnvelopeKind_Response, response.requestId, response.opCode, response.status, response.resultCode, response.payload);
		}

		const fb::fbContentEnvelope* Verify(const void* payload, size_t payloadSize)
		{
			if (!payload || payloadSize == 0)
				return nullptr;

			flatbuffers::Verifier verifier(static_cast<const uint8*>(payload), payloadSize);
			return fb::VerifyfbContentEnvelopeBuffer(verifier) ? fb::GetfbContentEnvelope(payload) : nullptr;
		}

		bool DecodeCommon(const fb::fbContentEnvelope& wire, std::vector<std::byte>& payload)
		{
			const auto* data = wire.payload();
			if (!data || data->size() > kMaxGenericContentPayloadBytes)
				return false;
			payload.resize(data->size());
			if (!payload.empty())
				std::memcpy(payload.data(), data->data(), data->size());
			
			return true;
		}
	}

	Packet MakeContentRequestPacket(const GenericContentRequest& request)
	{
		const size_t estimatedSize = kEnvelopeOverheadEstimate + request.payload.size();
		if (estimatedSize > kRuntimePacketScratchInitialSize)
		{
			flatbuffers::FlatBufferBuilder fbb(estimatedSize);
			return EncodeRequest(fbb, request);
		}
		return EncodeRequest(ResetRuntimePacketBuilder(CurrentShardLocalChecked()), request);
	}

	Packet MakeContentResponsePacket(const GenericContentResponse& response)
	{
		const size_t estimatedSize = kEnvelopeOverheadEstimate + response.payload.size();
		if (estimatedSize > kRuntimePacketScratchInitialSize)
		{
			flatbuffers::FlatBufferBuilder fbb(estimatedSize);
			return EncodeResponse(fbb, response);
		}
		return EncodeResponse(ResetRuntimePacketBuilder(CurrentShardLocalChecked()), response);
	}

	bool DecodeContentRequest(const void* payload, size_t payloadSize, GenericContentRequest& out)
	{
		const auto* wire = Verify(payload, payloadSize);
		GenericContentRequest decoded;
		
		if (!wire || wire->kind() != fb::fbContentEnvelopeKind_Request || wire->status() != fb::fbContentResponseStatus_None)
			return false;

		decoded.requestId = wire->request_id();
		decoded.opCode = wire->operation_key();
		if (!DecodeCommon(*wire, decoded.payload) || !decoded.IsValid())
			return false;
		out = std::move(decoded);
		return true;
	}

	bool DecodeContentResponse(const void* payload, size_t payloadSize, GenericContentResponse& out)
	{
		const auto* wire = Verify(payload, payloadSize);
		GenericContentResponse decoded;
		if (!wire || wire->kind() != fb::fbContentEnvelopeKind_Response || wire->status() <= fb::fbContentResponseStatus_None || wire->status() > fb::fbContentResponseStatus_MAX)
			return false;

		decoded.requestId = wire->request_id();
		decoded.opCode = wire->operation_key();
		decoded.status = static_cast<eGenericContentResponseStatus>(wire->status());
		decoded.resultCode = wire->result_code();

		if (decoded.requestId == kInvalidClientRequestId || decoded.opCode == kInvalidGenericContentOpCode || !DecodeCommon(*wire, decoded.payload))
			return false;

		out = std::move(decoded);
		return true;
	}
}
