#pragma once


namespace jam::utils::exec
{
	struct ShardExecutorConfig
	{
		int32 index = 0;
		int32 batchBudget = 32;	// ���Ǵ� 1ȸ ó����
		int32 idleSleep = 50;	// �����Ѱ��Ҷ� ���
	};

	class ShardExecutor
	{
	public:
		ShardExecutor(const ShardExecutorConfig& config = {}, );
		~ShardExecutor();

		void Attach();
		void Start();
		void Stop();

	private:
		void Loop();

	private:
		ShardExecutorConfig m_config;
		thrd::FiberScheduler& m_fibers;
		Atomic<bool> running{ false };
		std::thread m_thread;
		xvector<ShardEndpoint*> m_endpoints;
	};

}
