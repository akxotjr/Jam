#pragma once
#include "IKinematicDriver.h"

namespace jam::px
{
	class KinematicMoveComponent
	{
	public:
		explicit KinematicMoveComponent(std::unique_ptr<IKinematicDriver> driver);

		Transform				Tick(float dt);
		bool					IsDone() const;

		IKinematicDriver*		GetDriver() { return m_driver.get(); }
		const IKinematicDriver* GetDriver() const { return m_driver.get(); }

		template<typename T>
		T*	GetDriverAs() { return dynamic_cast<T*>(m_driver.get()); }

	private:
		std::unique_ptr<IKinematicDriver> m_driver;
	};

}
