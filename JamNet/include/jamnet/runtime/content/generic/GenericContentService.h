#pragma once

#include "jamnet/runtime/content/generic/GenericContentTypes.h"

#include <atomic>
#include <memory>

namespace jam
{
	class ShardExecutor;
}

namespace jam::net
{
	class IGenericContent;
	class ServerNetworkManager;

	class GenericContentService final : public std::enable_shared_from_this<GenericContentService>
	{
	public:
		bool Initialize(ServerNetworkManager* owner, std::shared_ptr<IGenericContent> content);
		void Stop();

		bool Submit(GenericContentPrincipal principal, GenericContentRequest request);

	private:
		void Complete(UserId userId, ClientRequestId requestId, GenericContentOperationCode operationKey, GenericContentResponse response);

		ServerNetworkManager*			 m_owner = nullptr;
		std::shared_ptr<IGenericContent> m_content;
		std::shared_ptr<ShardExecutor>	 m_shard;
		std::atomic<bool>				 m_running = false;
	};
}
