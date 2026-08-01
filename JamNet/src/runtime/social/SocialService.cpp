#include "pch.h"
#include "jamnet/runtime/social/SocialService.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/runtime/application/ServerNetworkManager.h"
#include "jamnet/runtime/protocol/schema/gen/social_message_generated.h"
#include "jamnet/runtime/protocol/transport/CustomPacketHelper.h"
#include "jamnet/runtime/social/IServerSocialContent.h"

namespace jam::net
{
	namespace
	{
		constexpr uint64 kSocialRouteSeed = 0x534F4349414Cull;

		fb::fbSocialAudience ToWireAudience(eSocialAudience audience)
		{
			return static_cast<fb::fbSocialAudience>(audience);
		}

		class PrincipalSocialDelivery final : public ISocialDelivery
		{
		public:
			PrincipalSocialDelivery(SocialService& service, UserId sender)
				: m_service(service), m_sender(sender)
			{
			}

			void SendTo(UserId userId, const SocialMessage& message) override
			{
				SocialMessage stamped = message;
				stamped.sender = m_sender;
				m_service.SendTo(userId, stamped);
			}

			void SendToWorld(const WorldRuntimeRef& world, const SocialMessage& message) override
			{
				SocialMessage stamped = message;
				stamped.sender = m_sender;
				m_service.SendToWorld(world, stamped);
			}

			void Broadcast(const SocialMessage& message) override
			{
				SocialMessage stamped = message;
				stamped.sender = m_sender;
				m_service.Broadcast(stamped);
			}

		private:
			SocialService&	m_service;
			UserId			m_sender;
		};
	}

	bool SocialService::Initialize(ServerNetworkManager* owner, std::shared_ptr<IServerSocialContent> content)
	{
		if (m_running.load(std::memory_order_acquire))
			return true;
		if (!owner || !content)
			return false;

		m_shard = GLOBAL_EXEC.GetAffinityShard(kSocialRouteSeed);
		if (!m_shard)
			return false;

		m_owner = owner;
		m_content = std::move(content);
		m_running.store(true, std::memory_order_release);
		return true;
	}

	void SocialService::Shutdown()
	{
		m_running.store(false, std::memory_order_release);
	}

	bool SocialService::Submit(const SocialPrincipal& sender, SocialCommand command)
	{
		if (!m_running.load(std::memory_order_acquire) || !m_shard || !m_content)
			return false;

		const auto self = shared_from_this();
		m_shard->Submit(Job([self, sender, command = std::move(command)]() mutable
			{
				if (!self->m_running.load(std::memory_order_acquire))
					return;

				PrincipalSocialDelivery delivery(*self, sender.userId);
				self->m_content->HandleCommand(sender, command, delivery);

			}, eJobPriority::Normal));
		return true;
	}

	void SocialService::NotifyDisconnected(UserId userId)
	{
		if (!m_running.load(std::memory_order_acquire) || !m_shard || !m_content || userId == kInvalidUserId)
			return;

		const auto self = shared_from_this();
		m_shard->Submit(Job([self, userId]()
			{
				if (self->m_running.load(std::memory_order_acquire))
					self->m_content->OnUserDisconnected(userId);
			}, eJobPriority::Control));
	}

	void SocialService::SendTo(UserId userId, const SocialMessage& message)
	{
		if (!m_running.load(std::memory_order_acquire) || !m_owner || userId == kInvalidUserId)
			return;
		m_owner->Send(userId, MakeMessagePacket(message), eProtocolType::TCP);
	}

	void SocialService::SendToWorld(const WorldRuntimeRef& world, const SocialMessage& message)
	{
		if (!m_running.load(std::memory_order_acquire) || !m_owner || !world.IsValid())
			return;
		m_owner->Multicast(world, MakeMessagePacket(message));
	}

	void SocialService::Broadcast(const SocialMessage& message)
	{
		if (!m_running.load(std::memory_order_acquire) || !m_owner)
			return;
		m_owner->Broadcast(MakeMessagePacket(message));
	}

	Packet SocialService::MakeMessagePacket(const SocialMessage& message) const
	{
		flatbuffers::FlatBufferBuilder fbb(128 + message.payload.size());
		const fb::fbSocialAddress destination(ToWireAudience(message.destination.audience), message.destination.scopeId);

		const auto payload = fbb.CreateVector(reinterpret_cast<const uint8*>(message.payload.data()), message.payload.size());
		const auto root = fb::CreatefbSocialMessage(fbb, message.messageId, message.sender, &destination, message.contentType, payload);
		fb::FinishfbSocialMessageBuffer(fbb, root);

		return PacketBuilder::CreateCustomPacket(CustomPacketId::SOCIAL_EVENT, PacketFlags::NONE, eChannel::TCP_DEFAULT, fbb.GetBufferPointer(), fbb.GetSize());
	}
}
