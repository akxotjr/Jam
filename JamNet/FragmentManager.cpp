#include "pch.h"
#include "FragmentManager.h"
#include "BufferReader.h"
#include "Clock.h"

namespace jam::net
{
	xvector<Sptr<SendBuffer>> FragmentManager::Fragmentize(const Sptr<SendBuffer>& buf, const PacketAnalysis& analysis)
	{
        xvector<Sptr<SendBuffer>> fragments;

        if (!buf || analysis.payloadSize == 0)
            return fragments;

        uint8 totalCount = (size + MAX_FRAGMENT_PAYLOAD_SIZE - 1) / MAX_FRAGMENT_PAYLOAD_SIZE;
        if (totalCount > MAX_FRAGMENTS)
            return fragments;

        PacketHeader originHeader = analysis.header;
        BYTE* payload = analysis.GetPayloadPtr(buf->Buffer());
        uint16 baseSeq = m_owner->GetReliableTransportManager()->AllocateSequenceRange();

        for (uint8 i = 0; i < totalCount; i++)
        {
            uint32 offset = i * MAX_FRAGMENT_PAYLOAD_SIZE;
            uint32 payloadSize = min(MAX_FRAGMENT_PAYLOAD_SIZE, size - offset);

            auto fragment = PacketBuilder::CreatePacket(
                U2E(ePacketType, originHeader.GetType()),
                originHeader.GetId(),
                originHeader.GetFlags() | PacketFlags::FRAGMENTED,
                payload + offset,
                payloadSize,
                baseSeq + i,
                i,
                totalCount
            );

            fragments.push_back(fragment);
        }

        return fragments;
	}

	std::pair<bool, xvector<BYTE>> FragmentManager::OnRecvFragment(BYTE* buf, uint16 size)
	{
        std::pair<bool, xvector<BYTE>> result = { false, {} };

        PacketAnalysis analysis = PacketBuilder::AnalyzePacket(buf, size);

        if (!analysis.isValid || !analysis.IsFragmented())
            return result;

        uint8 fragIndex = analysis.GetFragmentIndex();
        uint8 fragTotal = analysis.GetTotalFragments();
        uint16 sequence = analysis.GetSequence();

        if (fragTotal == 0 || fragIndex >= fragTotal)
        {
            std::cout << "[FragmentManager] Invalid fragment info" << std::endl;
            return result;
        }

        BYTE* payload = analysis.GetPayloadPtr(buf);
        uint32 payloadSize = analysis.payloadSize;

        WRITE_LOCK;

        CleanupStaleReassemblies();

        // Fragment 그룹 키 계산 (첫 번째 Fragment의 sequence)
        uint16 groupKey = sequence - fragIndex;
        auto& reassembly = m_reassemblies[groupKey];

        // 첫 Fragment이거나 아직 초기화되지 않은 경우 초기화
        if (!reassembly.headerSaved)
        {
            reassembly.Init(fragTotal);

            // 원본 헤더 정보 저장 (Fragment 플래그 제거)
            reassembly.originalHeader = analysis.header;
            reassembly.originalHeader.SetFlags(reassembly.originalHeader.GetFlags() & ~PacketFlags::FRAGMENTED);
            reassembly.headerSaved = true;
        }

        // Fragment 개수 일치 확인
        if (reassembly.totalCount != fragTotal)
        {
            std::cout << "[FragmentManager] Fragment count mismatch. Expected: "
                << (int)reassembly.totalCount << ", Got: " << (int)fragTotal << std::endl;
            m_reassemblies.erase(groupKey);
            return result;
        }

        // 중복 Fragment 체크
        if (reassembly.recvFragments[fragIndex])
        {
            std::cout << "[FragmentManager] Duplicate fragment received: " << (int)fragIndex << std::endl;
            // 중복이지만 에러는 아니므로 그냥 반환
            return result;
        }

        // Fragment 데이터 저장
        if (!reassembly.WriteFragment(fragIndex, payload, payloadSize))
        {
            std::cout << "[FragmentManager] Failed to write fragment " << (int)fragIndex << std::endl;
            return result;
        }

        // 마지막 수신 시간 갱신
        reassembly.lastRecvTime = Clock::Instance().GetCurrentTick();

        // 모든 Fragment가 수신되었는지 확인
        if (reassembly.IsComplete())
        {
            // 재조립된 데이터 가져오기
            result.first = true;
            result.second = reassembly.GetReassembledData();

            // 재조립 정보 삭제
            m_reassemblies.erase(groupKey);

            std::cout << "[FragmentManager] Fragment reassembly completed. Total size: "
                << result.second.size() << " bytes" << std::endl;
        }
        //else
        //{
        //    // 진행 상황 로깅
        //    uint32 receivedCount = 0;
        //    for (bool received : reassembly.recvFragments)
        //    {
        //        if (received) receivedCount++;
        //    }

        //    std::cout << "[FragmentManager] Fragment " << (int)fragIndex
        //        << " received. Progress: " << receivedCount
        //        << "/" << (int)fragTotal << std::endl;
        //}

        return result;
	}

	void FragmentManager::CleanupStaleReassemblies()
	{
        uint64 currentTick = Clock::Instance().GetCurrentTick();

        xvector<uint16> staleSequences;
        for (const auto& [sequence, reassembly] : m_reassemblies)
        {
            if (currentTick - reassembly.lastRecvTime > REASSEMBLY_TIMEOUT_TICK)
                staleSequences.push_back(sequence);
        }

        for (const auto& sequence : staleSequences)
            m_reassemblies.erase(sequence);
	}
}
