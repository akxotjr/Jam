#include "pch.h"
#include "jamnet/runtime/protocol/codec/SocialCodec.h"

#include "jamnet/core/executor/ThreadContext.h"
#include "jamnet/core/net/PacketBuilder.h"
#include "jamnet/runtime/protocol/codec/RuntimePacketCodec.h"
#include "jamnet/runtime/protocol/schema/gen/social_command_generated.h"
#include "jamnet/runtime/protocol/schema/gen/social_message_generated.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"

namespace jam::net::codec
{
	namespace
	{
		size_t EstimateSize(const SocialAddress& address, size_t payloadSize)
		{
			return 256 + address.recipient.name.size() + payloadSize;
		}

		flatbuffers::Offset<fb::fbSocialAddress> EncodeAddress(flatbuffers::FlatBufferBuilder& fbb, const SocialAddress& address)
		{
			const auto name = fbb.CreateString(address.recipient.name);
			const auto recipient = fb::CreatefbSocialRecipient(fbb, static_cast<fb::fbSocialRecipientKind>(address.recipient.kind), address.recipient.id, name);
			
			return fb::CreatefbSocialAddress(fbb, static_cast<fb::fbSocialAudience>(address.audience), address.scopeId, recipient);
		}

		bool DecodeAddress(const fb::fbSocialAddress* wire, SocialAddress& out)
		{
			const auto* recipient = wire ? wire->recipient() : nullptr;

			if (!wire || !recipient
					  || wire->audience() < fb::fbSocialAudience_MIN 
					  || wire->audience() > fb::fbSocialAudience_MAX
					  || recipient->kind() < fb::fbSocialRecipientKind_MIN 
					  || recipient->kind() > fb::fbSocialRecipientKind_MAX)
				return false;

			out = {
				.audience = static_cast<eSocialAudience>(wire->audience()),
				.scopeId = wire->scope_id(),
				.recipient = {
					.kind = static_cast<eSocialRecipientKind>(recipient->kind()),
					.id = recipient->id(),
					.name = recipient->name() ? recipient->name()->str() : std::string{},
				},
			};
			return true;
		}

		template<typename Wire>
		const Wire* Verify(const void* payload, size_t payloadSize)
		{
			if (!payload || payloadSize == 0)
				return nullptr;
			flatbuffers::Verifier verifier(static_cast<const uint8*>(payload), payloadSize);
			const auto* wire = flatbuffers::GetRoot<Wire>(payload);
			return wire && wire->Verify(verifier) ? wire : nullptr;
		}

		Packet Finish(flatbuffers::FlatBufferBuilder& fbb, uint8 id)
		{
			return PacketBuilder::CreateCustomPacket(id, PacketFlags::NONE, eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), static_cast<uint32>(fbb.GetSize()));
		}

		Packet EncodeCommand(flatbuffers::FlatBufferBuilder& fbb, const SocialCommand& command)
		{
			const auto destination = EncodeAddress(fbb, command.destination);
			const auto payload = fbb.CreateVector(reinterpret_cast<const uint8*>(command.payload.data()), command.payload.size());
			
			fb::FinishfbSocialCommandBuffer(fbb, fb::CreatefbSocialCommand(fbb, command.requestId, destination, command.contentType, payload));
			
			return Finish(fbb, CustomPacketId::SOCIAL_COMMAND);
		}

		Packet EncodeMessage(flatbuffers::FlatBufferBuilder& fbb, const SocialMessage& message)
		{
			const auto destination = EncodeAddress(fbb, message.destination);
			const auto payload = fbb.CreateVector(reinterpret_cast<const uint8*>(message.payload.data()), message.payload.size());
			
			fb::FinishfbSocialMessageBuffer(fbb, fb::CreatefbSocialMessage(fbb, message.messageId, message.sender, destination, message.contentType, payload));
			
			return Finish(fbb, CustomPacketId::SOCIAL_EVENT);
		}
	}

	Packet MakeSocialCommandPacket(const SocialCommand& command)
	{
		const size_t estimatedSize = EstimateSize(command.destination, command.payload.size());
		if (estimatedSize > kRuntimePacketScratchInitialSize)
		{
			flatbuffers::FlatBufferBuilder fbb(estimatedSize);
			return EncodeCommand(fbb, command);
		}
		return EncodeCommand(ResetRuntimePacketBuilder(CurrentShardLocalChecked()), command);
	}

	Packet MakeSocialMessagePacket(const SocialMessage& message)
	{
		const size_t estimatedSize = EstimateSize(message.destination, message.payload.size());
		if (estimatedSize > kRuntimePacketScratchInitialSize)
		{
			flatbuffers::FlatBufferBuilder fbb(estimatedSize);
			return EncodeMessage(fbb, message);
		}
		return EncodeMessage(ResetRuntimePacketBuilder(CurrentShardLocalChecked()), message);
	}

	bool DecodeSocialCommand(const void* payload, size_t payloadSize, SocialCommand& out)
	{
		const auto* wire = Verify<fb::fbSocialCommand>(payload, payloadSize);
		const auto* data = wire ? wire->payload() : nullptr;

		SocialCommand decoded;
		
		if (!wire || !data || !DecodeAddress(wire->destination(), decoded.destination))
			return false;
		
		decoded.requestId = wire->request_id();
		decoded.contentType = wire->content_type();
		decoded.payload.resize(data->size());
		if (!decoded.payload.empty())
			std::memcpy(decoded.payload.data(), data->data(), data->size());
		
		out = std::move(decoded);
		
		return true;
	}

	bool DecodeSocialMessage(const void* payload, size_t payloadSize, SocialMessage& out)
	{
		const auto* wire = Verify<fb::fbSocialMessage>(payload, payloadSize);
		const auto* data = wire ? wire->payload() : nullptr;

		SocialMessage decoded;
		
		if (!wire || !data || !DecodeAddress(wire->destination(), decoded.destination))
			return false;
		
		decoded.messageId = wire->message_id();
		decoded.sender = wire->sender();
		decoded.contentType = wire->content_type();
		decoded.payload.resize(data->size());
		if (!decoded.payload.empty())
			std::memcpy(decoded.payload.data(), data->data(), data->size());
		
		out = std::move(decoded);
		
		return true;
	}
}
