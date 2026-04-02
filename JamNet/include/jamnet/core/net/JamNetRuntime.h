#pragma once


namespace jam::net
{
	struct RuntimeConfig
	{
		uint32					clockTick = 30;
		GlobalExecutorConfig	geConfig  = {};
	};

	class JamNetRuntime
	{
	public:
		explicit JamNetRuntime(const RuntimeConfig& config = {});
		~JamNetRuntime();

		JamNetRuntime(const JamNetRuntime&) = delete;
		JamNetRuntime& operator=(const JamNetRuntime&) = delete;

		bool			IsInitialized() const noexcept { return m_bInitialized; }

	private:
		void			Init();
		void			Shutdown();

	private:
		RuntimeConfig	m_config;
		bool			m_bInitialized = false;
	};
}