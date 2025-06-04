#pragma once
#include <Worker.h>

namespace jam::net
{
	class NetWorker : public utils::thrd::Worker
	{
	public:
		NetWorker();
		virtual ~NetWorker() override;

		virtual void Work() override;

	private:

	};
}


