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

        // ������ �� ���
        uint8 totalCount = (size + MAX_FRAGMENT_SIZE - 1) / MAX_FRAGMENT_SIZE;
        if (totalCount > MAX_FRAGMENTS)
        {
            std::cout << "[FragmentHandler] Packet is too large to fragment. Size: " << size << std::endl;
            return fragments;
        }

        // ������ ��Ŷ ID ����
        uint32 packetId = m_nextPacketId++;

        // �� ���� ����
        for (uint8 i = 0; i < totalCount; i++)
        {
            uint16 fragmentOffset = i * MAX_FRAGMENT_SIZE;
            uint16 fragmentSize = std::min(MAX_FRAGMENT_SIZE, static_cast<uint16>(size - fragmentOffset));

            // ��� ���� ��ü ���� ũ��
            uint16 totalFragmentSize = sizeof(FragmentHeader) + fragmentSize;

            Sptr<SendBuffer> buffer = SendBufferManager::Instance().Open(totalFragmentSize);
            BufferWriter bw(buffer->Buffer(), buffer->AllocSize());

            // ���� ��� �ۼ�
            FragmentHeader* header = bw.Reserve<FragmentHeader>();
            header->packetId = packetId;
            header->totalCount = totalCount;
            header->fragmentIndex = i;
            header->totalSize = size;
            header->fragmentSize = fragmentSize;

            // ���� ������ ����
            bw.Write(data + fragmentOffset, fragmentSize);

            buffer->Close(totalFragmentSize);
            fragments.push_back(buffer);
        }

        return fragments;
	}

	std::pair<bool, xvector<BYTE>> FragmentHandler::OnRecvFragment(uint16 sessionId, BYTE* data, uint16 size)
	{
        // ��� �⺻�� ���� (������ ����, �� ������)
        std::pair<bool, xvector<BYTE>> result = { false, {} };

        // ũ�Ⱑ ������� ������ ������ �ƴ�
        if (size < sizeof(FragmentHeader))
            return result;

        BufferReader br(data, size);
        FragmentHeader header;
        br.Read(&header);

        // ��� ����
        if (header.totalCount == 0 || header.fragmentIndex >= header.totalCount ||
            header.totalSize == 0 || header.fragmentSize == 0 ||
            size < sizeof(FragmentHeader) + header.fragmentSize)
        {
            std::cout << "[FragmentHandler] Invalid fragment header" << std::endl;
            return result;
        }

        // ���� ������ ��� �ٷ� ��ȯ
        if (header.totalCount == 1 && header.fragmentIndex == 0)
        {
            xvector<BYTE> completeData(header.fragmentSize);
            memcpy(completeData.data(), data + sizeof(FragmentHeader), header.fragmentSize);
            return { true, completeData };
        }

        // ���� �ĺ��� ����
        FragmentId fragmentId = { header.packetId, sessionId };

        WRITE_LOCK

        // ������ �̿ϼ� ������ ����
        CleanupStaleReassemblies();

        // �ش� ��Ŷ�� ������ ���� ã�� �Ǵ� ����
        auto& reassembly = m_reassemblies[fragmentId];

        // ù �����̸� ������ ���� �ʱ�ȭ
        if (header.fragmentIndex == 0 && reassembly.buffer.empty())
        {
            reassembly.totalSize = header.totalSize;
            reassembly.totalCount = header.totalCount;
            reassembly.recvFragments.resize(header.totalCount, false);
            reassembly.buffer.resize(header.totalSize);
        }

        // ������ ���� ����
        if (reassembly.totalCount != header.totalCount || reassembly.totalSize != header.totalSize)
        {
            std::cout << "[FragmentHandler] Fragment mismatch" << std::endl;
            m_reassemblies.erase(fragmentId);
            return result;
        }

        // ���� ������ ����
        uint16 fragmentOffset = header.fragmentIndex * MAX_FRAGMENT_SIZE;
        if (fragmentOffset + header.fragmentSize <= header.totalSize)
        {
            memcpy(reassembly.buffer.data() + fragmentOffset, data + sizeof(FragmentHeader), header.fragmentSize);
            reassembly.recvFragments[header.fragmentIndex] = true;
        }

        // ������ ���� �ð� ����
        reassembly.lastRecvTime = Clock::Instance().GetCurrentTick();

        // ��� ������ ���ŵǾ����� Ȯ��
        bool completed = std::all_of(reassembly.recvFragments.begin(),
            reassembly.recvFragments.end(),
            [](bool received) { return received; });

        if (completed)
        {
            // �ϼ��� ������ ��ȯ
            result.first = true;
            result.second = std::move(reassembly.buffer);

            // ������ ���� ����
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
