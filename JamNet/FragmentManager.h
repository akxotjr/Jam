#pragma once

namespace jam::net
{
	// 1500(UDP MTU) - 20(IP Header) - 8(UDP Header) = 1472 bytes. 
	constexpr uint16 JAMNET_MTU = 1400;	// 1472 - margin = 1400 bytes;


	constexpr uint16 MAX_FRAGMENT_SIZE = JAMNET_MTU;
	constexpr uint16 MAX_FRAGMENT_PAYLOAD_SIZE = MAX_FRAGMENT_SIZE - PacketHeader::GetFullSize() - sizeof(AckHeader);

	//constexpr uint16 MAX_PACKET_SIZE = 65535;
	constexpr uint8 MAX_FRAGMENTS = 255;

	constexpr uint64 REASSEMBLY_TIMEOUT_TICK = 100;


	struct FragmentReassembly
	{
        uint64			        lastRecvTime_ns;
        uint8			        totalCount;
        xvector<bool>	        recvFragments;
        xvector<xvector<BYTE>>  fragmentData;  // 각 Fragment 데이터를 별도 저장

        PacketHeader	        originalHeader;
        bool			        headerSaved = false;

        void Init(uint8 count)
        {
            totalCount = count;
            recvFragments.resize(count, false);
            fragmentData.resize(count);
            headerSaved = false;
        }

        bool WriteFragment(uint8 index, BYTE* data, uint32 size)
        {
            if (index >= totalCount)
                return false;

            // Fragment별로 별도 저장 (패딩 없음)
            fragmentData[index].resize(size);
            memcpy(fragmentData[index].data(), data, size);
            recvFragments[index] = true;

            return true;
        }

        bool IsComplete() const
        {
            return ranges::all_of(recvFragments, [](bool received) { return received; });
        }

        xvector<BYTE> GetReassembledData()
        {
            if (!IsComplete())
                return {};

            uint32 totalSize = 0;
            for (const auto& frag : fragmentData)
                totalSize += frag.size();

            xvector<BYTE> result(totalSize);
            uint32 offset = 0;

            for (const auto& frag : fragmentData)
            {
                memcpy(result.data() + offset, frag.data(), frag.size());
                offset += frag.size();
            }

            return result;
        }
	};

	class FragmentManager
	{
	public:
		FragmentManager(UdpSession* owner) : m_owner(owner) {}
		~FragmentManager() = default;


		xvector<Sptr<SendBuffer>>		Fragmentize(const Sptr<SendBuffer>& buf, const PacketAnalysis& analysis);
		std::pair<bool, xvector<BYTE>>	OnRecvFragment(BYTE* buf, uint16 size);

	private:
		void							CleanupStaleReassemblies();

	private:
		USE_LOCK

		UdpSession*							m_owner = nullptr;
		xumap<uint16, FragmentReassembly>	m_reassemblies;
	};
}
