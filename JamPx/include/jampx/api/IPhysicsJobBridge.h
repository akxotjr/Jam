#pragma once


#include <functional>
#include <cstdint>

namespace jam::px
{
	class IPhysicsJobBridge
	{
	public:
		virtual ~IPhysicsJobBridge() = default;

		virtual void	SubmitJob(std::function<void()> fn) = 0;
		virtual void	NotifyComplete(uint64_t awaitKey) = 0;
		virtual bool	IsInFiberContext() const = 0;
	};

}