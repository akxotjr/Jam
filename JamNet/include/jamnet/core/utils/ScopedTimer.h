#pragma once

#include <jambase/JamTypes.h>
#include <jambase/Logger.h>
#include "jamnet/core/utils/Clock.h"

namespace jam
{
	class ScopedTimer
	{
	public:
		ScopedTimer(std::string_view name, eTimeUnit unit = eTimeUnit::Us)
		{
			m_name = name;
			m_unit = unit;

			if (m_unit == eTimeUnit::Ns)
				m_start = NOW_NS();
			else if (m_unit == eTimeUnit::Us)
				m_start = NOW_US();
			else if (m_unit == eTimeUnit::Ms)
				m_start = NOW_MS();
			else if (m_unit == eTimeUnit::Sec)
				m_start = NOW_SEC();
		}

		~ScopedTimer()
		{
			uint64 now;
			if (m_unit == eTimeUnit::Ns)
			{
				now = NOW_NS();
				JAMNET_LOG_TRACE("[{}] start= {}ns, elapsed= {}ns", m_name, m_start, now - m_start);
			}
			else if (m_unit == eTimeUnit::Us)
			{
				now = NOW_US();
				JAMNET_LOG_TRACE("[{}] start= {}us, elapsed= {}us", m_name, m_start, now - m_start);
			}
			else if (m_unit == eTimeUnit::Ms)
			{
				now = NOW_MS();
				JAMNET_LOG_TRACE("[{}] start= {}ms, elapsed= {}ms", m_name, m_start, now - m_start);
			}
			else if (m_unit == eTimeUnit::Sec)
			{
				now = NOW_SEC();
				JAMNET_LOG_TRACE("[{}] start= {}sec, elapsed= {}sec", m_name, m_start, now - m_start);
			}
		}

	private:
		std::string		m_name;
		eTimeUnit		m_unit	= eTimeUnit::Us;
		uint64			m_start = 0;
	};

} // namespace jam


#define SCOPED_TIMER()     jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Us);

#define SCOPED_TIMER_NS()  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Ns);
#define SCOPED_TIMER_US()  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Us);
#define SCOPED_TIMER_MS()  jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Ms);
#define SCOPED_TIMER_SEC() jam::ScopedTimer timer(__FUNCTION__, jam::eTimeUnit::Sec);
