#pragma once


namespace jam::net
{
	//class UdpSession;
	//struct PacketAnalysis;

	//struct ChannelConfig
	//{
	//	bool reliable;
	//	bool ordered;
	//	bool sequenced;
	//	uint32 maxQueueSize;
	//};

	//constexpr std::array<ChannelConfig, 4> CHANNEL_CONFIGS = { {
	//	{false, false, false, 0},
	//	{true, true, false, 100},
	//	{false, false, true, 10},
	//	{true, false, false, 50}
	//} };


	//class ChannelManager
	//{
	//public:
	//	ChannelManager(UdpSession* owner) : m_owner(owner) {}
	//	~ChannelManager() = default;

	//	void Update();

	//	bool ProcessChannelRecv(const PacketAnalysis& analysis, BYTE* payload, uint32 payloadSize);
	//	bool IsChannelReliable(eChannelType channel) const;


	//private:
	//	bool ProcessSequnecedChannel(const PacketAnalysis& analysis);

	//	bool ProcessOrderedChannel(const PacketAnalysis& analysis, BYTE* payload, uint32 payloadSize);
	//	void BufferOrderedPacket(const PacketAnalysis& analysis, BYTE* payload, uint32 payloadSize);
	//	void ProcessBufferedOrderedPackets(eChannelType channel);

	//	void CheckOrderedBufferTimeout(uint64 currentTick);
	//	void ForceProcessOrderedBuffer(eChannelType channel, uint64 currentTick);
	//	

	//	bool IsChannelOrdered(eChannelType channel) const;
	//	bool IsChannelSequenced(eChannelType channel) const;


	//private:
	//	UdpSession* m_owner = nullptr;

	//	// 채널별 상태 관리
	//	std::array<uint16, 4> m_channelLatestSequences{ 0 };     // 시퀀스드 채널용
	//	std::array<uint16, 4> m_channelExpectedSequences{ 1 };   // 순서 보장 채널용

	//	// 순서 보장을 위한 버퍼
	//	std::array<std::map<uint16, std::pair<std::vector<BYTE>, PacketAnalysis>>, 4> m_orderingBuffers;

	//	static constexpr uint64 ORDERED_BUFFER_TIMEOUT = 100;
	//	static constexpr int16 MAX_SEQUENCE_GAP = 100;
	//};
}

