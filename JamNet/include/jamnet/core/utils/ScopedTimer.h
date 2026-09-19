#pragma once

#include <jambase/JamTypes.h>
#include <jambase/Logger.h>
#include "jamnet/core/utils/Clock.h"

namespace jam
{
	class ScopedTimer
	{
	public:
		ScopedTimer(std::string_view name, eTimeUnit unit = eTimeUnit::Us, Duration threshold = Duration::Zero())
			: m_name(name)
			, m_unit(unit)
			, m_threshold(threshold)
		{
			m_start = NOW();
		}

		~ScopedTimer()
		{
			const Duration elapsed = NOW() - m_start;

			if (elapsed < m_threshold)
				return;

			if (m_unit == eTimeUnit::Ns)
				JAM_LOG_TRACE("[{}] start= {}ns, elapsed= {}ns", m_name, m_start.Ns(), elapsed.Ns());
			else if (m_unit == eTimeUnit::Us)
				JAM_LOG_TRACE("[{}] start= {}us, elapsed= {}us", m_name, m_start.Us(), elapsed.Us());
			else if (m_unit == eTimeUnit::Ms)
				JAM_LOG_TRACE("[{}] start= {}ms, elapsed= {}ms", m_name, m_start.Ms(), elapsed.Ms());
			else if (m_unit == eTimeUnit::Sec)
				JAM_LOG_TRACE("[{}] start= {}sec, elapsed= {}sec", m_name, m_start.Sec(), elapsed.Sec());
		 }

	private:
		std::string		m_name;
		eTimeUnit		m_unit		= eTimeUnit::Us;
		Timestamp		m_start		= Timestamp::Zero();
		Duration		m_threshold = Duration::Zero();
	};

} // namespace jam


#define SCOPED_TIMER()     jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Us);

#define SCOPED_TIMER_NS()  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Ns);
#define SCOPED_TIMER_US()  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Us);
#define SCOPED_TIMER_MS()  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Ms);
#define SCOPED_TIMER_SEC() jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Sec);

#define SCOPED_TIMER_IF_NS(threshold)  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Ns, threshold);
#define SCOPED_TIMER_IF_US(threshold)  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Us, threshold);
#define SCOPED_TIMER_IF_MS(threshold)  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Ms, threshold);
#define SCOPED_TIMER_IF_SEC(threshold) jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Sec, threshold);
