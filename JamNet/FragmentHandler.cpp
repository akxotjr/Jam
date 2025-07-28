#include "pch.h"
#include "FragmentHandler.h"

#include "BufferReader.h"
#include "Clock.h"

namespace jam::net
{
	xvector<Sptr<SendBuffer>> FragmentHandler::Fragmentize(BYTE* data, uint16 size)
	{
		xvector<Sptr<SendBuffer>> fragments;

		if (size == 0)
			return fragments;

        if (size <= MAX_FRAGMENT_SIZE)
        {
            Sptr<SendBuffer> buffer = SendBufferManager::Instance().Open(size);
            memcpy(buffer->Buffer(), data, size);
            buffer->Close(size);
            fragments.push_back(buffer);
            return fragments;
        }

        // 조각의 수 계산
        uint8 totalCount = (size + MAX_FRAGMENT_SIZE - 1) / MAX_FRAGMENT_SIZE;
        if (totalCount > MAX_FRAGMENTS)
        {
            std::cout << "[FragmentHandler] Packet is too large to fragment. Size: " << size << std::endl;
            return fragments;
        }

        // 고유한 패킷 ID 생성
        uint32 packetId = m_nextPacketId++;

        // 각 조각 생성
        for (uint8 i = 0; i < totalCount; i++)
        {
            uint16 fragmentOffset = i * MAX_FRAGMENT_SIZE;
            uint16 fragmentSize = std::min(MAX_FRAGMENT_SIZE, static_cast<uint16>(size - fragmentOffset));

            // 헤더 포함 전체 조각 크기
            uint16 totalFragmentSize = sizeof(FragmentHeader) + fragmentSize;

            Sptr<SendBuffer> buffer = SendBufferManager::Instance().Open(totalFragmentSize);
            BufferWriter bw(buffer->Buffer(), buffer->AllocSize());

            // 조각 헤더 작성
            FragmentHeader* header = bw.Reserve<FragmentHeader>();
            header->packetId = packetId;
            header->totalCount = totalCount;
            header->fragmentIndex = i;
            header->totalSize = size;
            header->fragmentSize = fragmentSize;

            // 조각 데이터 복사
            bw.Write(data + fragmentOffset, fragmentSize);

            buffer->Close(totalFragmentSize);
            fragments.push_back(buffer);
        }

        return fragments;
	}

	std::pair<bool, xvector<BYTE>> FragmentHandler::OnRecvFragment(uint16 sessionId, BYTE* data, uint16 size)
	{
        // 결과 기본값 설정 (재조립 실패, 빈 데이터)
        std::pair<bool, xvector<BYTE>> result = { false, {} };

        // 크기가 헤더보다 작으면 조각이 아님
        if (size < sizeof(FragmentHeader))
            return result;

        BufferReader br(data, size);
        FragmentHeader header;
        br.Read(&header);

        // 헤더 검증
        if (header.totalCount == 0 || header.fragmentIndex >= header.totalCount ||
            header.totalSize == 0 || header.fragmentSize == 0 ||
            size < sizeof(FragmentHeader) + header.fragmentSize)
        {
            std::cout << "[FragmentHandler] Invalid fragment header" << std::endl;
            return result;
        }

        // 단일 조각인 경우 바로 반환
        if (header.totalCount == 1 && header.fragmentIndex == 0)
        {
            xvector<BYTE> completeData(header.fragmentSize);
            memcpy(completeData.data(), data + sizeof(FragmentHeader), header.fragmentSize);
            return { true, completeData };
        }

        // 조각 식별자 생성
        FragmentId fragmentId = { header.packetId, sessionId };

        WRITE_LOCK

        // 오래된 미완성 조각들 정리
        CleanupStaleReassemblies();

        // 해당 패킷의 재조립 정보 찾기 또는 생성
        auto& reassembly = m_reassemblies[fragmentId];

        // 첫 조각이면 재조립 정보 초기화
        if (header.fragmentIndex == 0 && reassembly.buffer.empty())
        {
            reassembly.totalSize = header.totalSize;
            reassembly.totalCount = header.totalCount;
            reassembly.recvFragments.resize(header.totalCount, false);
            reassembly.buffer.resize(header.totalSize);
        }

        // 재조립 정보 검증
        if (reassembly.totalCount != header.totalCount || reassembly.totalSize != header.totalSize)
        {
            std::cout << "[FragmentHandler] Fragment mismatch" << std::endl;
            m_reassemblies.erase(fragmentId);
            return result;
        }

        // 조각 데이터 복사
        uint16 fragmentOffset = header.fragmentIndex * MAX_FRAGMENT_SIZE;
        if (fragmentOffset + header.fragmentSize <= header.totalSize)
        {
            memcpy(reassembly.buffer.data() + fragmentOffset, data + sizeof(FragmentHeader), header.fragmentSize);
            reassembly.recvFragments[header.fragmentIndex] = true;
        }

        // 마지막 수신 시간 갱신
        reassembly.lastRecvTime = Clock::Instance().GetCurrentTick();

        // 모든 조각이 수신되었는지 확인
        bool completed = std::all_of(reassembly.recvFragments.begin(),
            reassembly.recvFragments.end(),
            [](bool received) { return received; });

        if (completed)
        {
            // 완성된 데이터 반환
            result.first = true;
            result.second = std::move(reassembly.buffer);

            // 재조립 정보 삭제
            m_reassemblies.erase(fragmentId);
        }

        return result;
	}

	void FragmentHandler::CleanupStaleReassemblies()
	{
        uint64 currentTick = Clock::Instance().GetCurrentTick();

        xvector<FragmentId> staleIds;
        for (const auto& [id, reassembly] : m_reassemblies)
        {
            if (currentTick - reassembly.lastRecvTime > REASSEMBLY_TIMEOUT_TICK)
                staleIds.push_back(id);
        }

        for (const auto& id : staleIds)
            m_reassemblies.erase(id);
	}
}
