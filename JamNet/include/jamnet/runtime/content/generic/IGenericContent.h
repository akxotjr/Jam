#pragma once

#include "jamnet/runtime/content/generic/GenericContentTypes.h"

#include <functional>

namespace jam::net
{
	using GenericContentRequestCompleted = std::function<void(GenericContentResponse)>;

	class IGenericContent
	{
	public:
		virtual ~IGenericContent() = default;

		// completed may be called asynchronously from any thread.
		virtual void HandleRequest(const GenericContentPrincipal& principal, const GenericContentRequest& request, GenericContentRequestCompleted completed) = 0;
	};
}
