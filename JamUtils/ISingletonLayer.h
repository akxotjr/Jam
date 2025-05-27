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
				_instance = std::make_shared<T>();
				_instance->ILayer::Init();
			}
		}
		static void Shutdown()
		{
			if (_instance)
			{
				_instance->ILayer::Shutdown();
				_instance.reset();
			}
		}

		static std::shared_ptr<T> Instance()
		{
			return _instance;
		}

	protected:
		ISingletonLayer() = default;

	private:
		static inline std::shared_ptr<T> _instance;
	};
}

