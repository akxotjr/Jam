#include "pch.h"
#include "jamnet/runtime/content/generic/GenericContentService.h"

#include "jamnet/core/executor/GlobalExecutor.h"
#include "jamnet/core/net/Session.h"
#include "jamnet/runtime/content/generic/IGenericContent.h"
#include "jamnet/runtime/protocol/codec/ContentCodec.h"
#include "jamnet/runtime/session/RuntimeShardRouting.h"

namespace jam::net
{
	namespace
	{
		constexpr uint64 kContentRouteSeed = 0x434F4E54454E54ull;
	}

	bool GenericContentService::Initialize(ServerNetworkManager* owner, std::shared_ptr<IGenericContent> content)
	{
		if (m_running.load(std::memory_order_acquire))
			return true;
		if (!owner || !content)
			return false;

		m_shard = GLOBAL_EXEC.GetAffinityShard(kContentRouteSeed);
		if (!m_shard)
			return false;
		
		m_owner = owner;
		m_content = std::move(content);
		m_running.store(true, std::memory_order_release);
		
		return true;
	}

	void GenericContentService::Shutdown()
	{
		m_running.store(false, std::memory_order_release);
	}

	bool GenericContentService::Submit(GenericContentPrincipal principal, GenericContentRequest request)
	{
		if (!m_running.load(std::memory_order_acquire) || !m_shard || !m_content || principal.accountId == kInvalidAccountId || principal.userId == kInvalidUserId || !request.IsValid())
			return false;

		const auto self = shared_from_this();
		m_shard->Submit(Job([self, principal, request = std::move(request)]() mutable
			{
				if (!self->m_running.load(std::memory_order_acquire))
					return;
			
				const ClientRequestId requestId = request.requestId;
				const GenericContentOperationCode opCode = request.opCode;

				auto completed = std::make_shared<std::atomic_bool>(false);
				const std::weak_ptr<GenericContentService> weak = self;

				self->m_content->HandleRequest(principal, request,
					[weak, completed, userId = principal.userId, requestId, opCode](GenericContentResponse response) mutable
					{
						if (completed->exchange(true, std::memory_order_acq_rel))
							return;
					
						if (const auto service = weak.lock(); service && service->m_shard)
						{
							service->m_shard->Submit(Job([service, userId, requestId, opCode, response = std::move(response)]() mutable
								{
									service->Complete(userId, requestId, opCode, std::move(response));
								}, eJobPriority::Control));
						}
					});
			}, eJobPriority::Normal));
		return true;
	}

	void GenericContentService::Complete(UserId userId, ClientRequestId requestId, GenericContentOperationCode opCode, GenericContentResponse response)
	{
		if (!m_running.load(std::memory_order_acquire) || !m_owner || userId == kInvalidUserId)
			return;

		response.requestId = requestId;
		response.opCode = opCode;
		
		if (response.status <= eGenericContentResponseStatus::None || response.status > eGenericContentResponseStatus::InternalError)
			response.status = eGenericContentResponseStatus::InternalError;

		if (response.payload.size() > kMaxGenericContentPayloadBytes)
		{
			response.status = eGenericContentResponseStatus::InternalError;
			response.resultCode = 0;
			response.payload.clear();
		}
		
		SendToUser(userId, codec::MakeContentResponsePacket(response), eProtocolType::TCP);
	}
}
