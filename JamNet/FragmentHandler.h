#pragma once

namespace jam::net
{

	constexpr uint16 MAX_FRAGMENT_SIZE = 1000;
	constexpr uint16 MAX_PACKET_SIZE = 65535;
	constexpr uint8 MAX_FRAGMENTS = 255;

	constexpr uint64 REASSEMBLY_TIMEOUT_TICK = 100;

	struct FragmentId
	{
		uint32 packetId;
		uint16 sessionId;

		bool operator==(const FragmentId& other) const
		{
			return packetId == other.packetId && sessionId == other.sessionId;
		}
	};

	struct FragmentIdHash
	{
		size_t operator()(const FragmentId& id) const
		{
			return std::hash<uint32>()(id.packetId) ^ std::hash<uint16>()(id.sessionId);
		}
	};

	struct FragmentHeader
	{
		uint32	packetId; 
		uint8	totalCount;
		uint8	fragmentIndex;
		uint16	totalSize;
		uint16	fragmentSize;
	};

	struct FragmentReassembly
	{
		uint64			lastRecvTime;
		uint16			totalSize;
		uint8			totalCount;
		xvector<bool>	recvFragments;
		xvector<BYTE>	buffer;
	};

	class FragmentHandler
	{
	public:
		FragmentHandler() = default;
		~FragmentHandler() = default;

		xvector<Sptr<SendBuffer>> Fragmentize(BYTE* data, uint16 size);
		std::pair<bool, xvector<BYTE>> OnRecvFragment(uint16 sessionId, BYTE* data, uint16 size);

	private:
		void CleanupStaleReassemblies();

	private:
		USE_LOCK
		uint32 m_nextPacketId = 1;
		xumap<FragmentId, FragmentReassembly, FragmentIdHash> m_reassemblies;
	};
}
