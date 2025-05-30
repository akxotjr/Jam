#pragma once
#include "ILayer.h"

namespace jam
{
	template<typename T>
	class ISingletonLayer : public ILayer
	{
	public:
		static void Init()
		{
			if (!_instance)
			{
				_instance = std::make_unique<T>();
				_instance->ILayer::Init();
			}
		}
		static void Shutdown()
		{
			if (_instance)
			{
				_instance->ILayer::Shutdown();
			}
		}

		static T* Instance()
		{
			return _instance.get();
		}

	protected:
		ISingletonLayer() = default;

	private:
		static inline std::unique_ptr<T> _instance;
	};
}

