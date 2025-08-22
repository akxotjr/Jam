#include "pch.h"
#include "ChannelManager.h"

#include "Clock.h"


namespace jam::net
{
	void ChannelManager::Update()
	{
		uint64 currentTick = Clock::Instance().GetCurrentTick();

		CheckOrderedBufferTimeout(currentTick);

		// todo: update netstat
	}

	bool ChannelManager::ProcessChannelRecv(const PacketAnalysis& analysis, BYTE* payload, uint32 payloadSize)
	{
		switch (eChannelType channel = analysis.GetChannel())
		{
		case eChannelType::UNRELIABLE_UNORDERED:
			return true;
		case eChannelType::RELIABLE_ORDERED:
			return ProcessOrderedChannel(analysis, payload, payloadSize);
		case eChannelType::UNRELIABLE_SEQUENCED:
			return ProcessSequnecedChannel(analysis);
		case eChannelType::RELIABLE_UNORDERED:
			return true;
		}
		return false;
	}

	bool ChannelManager::IsChannelReliable(eChannelType channel) const
	{
		return CHANNEL_CONFIGS[E2U(channel)].reliable;
	}

	bool ChannelManager::IsChannelOrdered(eChannelType channel) const
	{
		return CHANNEL_CONFIGS[E2U(channel)].ordered;
	}

	bool ChannelManager::IsChannelSequenced(eChannelType channel) const
	{
		return CHANNEL_CONFIGS[E2U(channel)].sequenced;
	}

	bool ChannelManager::ProcessSequnecedChannel(const PacketAnalysis& analysis)
	{
		eChannelType channel = analysis.GetChannel();

		uint16 currentSeq = analysis.GetSequence();
		uint16& latestSeq = m_channelLatestSequences[E2U(channel)];

		// 시퀀스 번호 래핑 고려한 비교
		if (bool isNewer = static_cast<int16>(currentSeq - latestSeq) > 0)
		{
			latestSeq = currentSeq;
			return true;
		}

		return false; // 오래된 패킷
	}

	bool ChannelManager::ProcessOrderedChannel(const PacketAnalysis& analysis, BYTE* payload, uint32 payloadSize)
	{
		eChannelType channel = analysis.GetChannel();
		uint16 currentSeq = analysis.GetSequence();
		uint16& expectedSeq = m_channelExpectedSequences[E2U(channel)];

		if (currentSeq == expectedSeq)
		{
			expectedSeq++;

			ProcessBufferedOrderedPackets(channel);
			return true;
		}
		else if (static_cast<uint16>(currentSeq - expectedSeq) > 0)
		{
			BufferOrderedPacket(analysis, payload, payloadSize);
			return false;
		}
		else
		{
			return false;
		}
	}

	void ChannelManager::BufferOrderedPacket(const PacketAnalysis& analysis, BYTE* payload, uint32 payloadSize)
	{
		eChannelType channel = analysis.GetChannel();
		uint8 idx = E2U(channel);
		uint16 seq = analysis.GetSequence();

		std::vector<BYTE> temp(payload, payload + payloadSize);
		m_orderingBuffers[idx][seq] = std::make_pair(std::move(temp), analysis);

		const uint32 maxBufferSize = CHANNEL_CONFIGS[idx].maxQueueSize;
		if (m_orderingBuffers[idx].size() > maxBufferSize)
		{
			m_orderingBuffers[idx].erase(m_orderingBuffers[idx].begin());
		}
	}

	void ChannelManager::ProcessBufferedOrderedPackets(eChannelType channel)
	{
		uint8 idx = E2U(channel);
		uint16& expectedSeq = m_channelExpectedSequences[idx];
		auto& buf = m_orderingBuffers[idx];

		while (true)
		{
			auto it = buf.find(expectedSeq);
			if (it == buf.end())
				break;

			const auto& [payload, analysis] = it->second;
			m_owner->ProcessBufferedPacket(analysis, const_cast<BYTE*>(payload.data()), payload.size());

			buf.erase(it);
			expectedSeq++;
		}
	}

	void ChannelManager::CheckOrderedBufferTimeout(uint64 currentTick)
	{
		for (uint8 i = 0; i < 4; i++)
		{
			eChannelType channel = U2E(eChannelType, i);

			if (!IsChannelOrdered(channel))
				continue;

			auto& buffer = m_orderingBuffers[i];
			if (buffer.empty())
				continue;

			auto oldestPkt = buffer.begin();
			uint64 elapsedTime = currentTick - oldestPkt->second.second.timestamp;

			if (elapsedTime >= ORDERED_BUFFER_TIMEOUT)
				ForceProcessOrderedBuffer(channel, currentTick);
		}
	}

	void ChannelManager::ForceProcessOrderedBuffer(eChannelType channel, uint64 currentTick)
	{
		uint8 idx = E2U(channel);
		auto& buffer = m_orderingBuffers[idx];
		uint16& expectedSeq = m_channelExpectedSequences[idx];

		uint64 processedCount = 0;
		constexpr uint64 maxProcessPerUpdate = 10;

		while (!buffer.empty() && processedCount < maxProcessPerUpdate)
		{
			auto nextPkt = buffer.begin();
			uint16 nextSeq = nextPkt->first;

			if (static_cast<int16>(nextSeq - expectedSeq) > MAX_SEQUENCE_GAP)
			{
				expectedSeq = nextSeq + 1;
			}

			const auto& [payload, analysis] = nextPkt->second;
			m_owner->ProcessBufferedPacket(analysis, const_cast<BYTE*>(payload.data()), payload.size());

			buffer.erase(nextPkt);
			processedCount++;

			if (nextSeq >= expectedSeq)
			{
				expectedSeq = nextSeq + 1;
			}
		}
	}
}
