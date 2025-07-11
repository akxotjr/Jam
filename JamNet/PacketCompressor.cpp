#include "pch.h"
#include "PacketCompressor.h"
#include <fstream>

namespace jam::net
{
	PacketCompressor::PacketCompressor(const Sptr<Service>& service)
	{
		m_service = service;
		m_pIpzip = IpzipCreate();
	}

	PacketCompressor::~PacketCompressor()
	{
		IpzipDestroy(m_pIpzip);
	}

	void PacketCompressor::Init(std::filesystem::path filePath)
	{
		std::ifstream inputFile(filePath, std::ios::binary);

		if (!inputFile.is_open())
		{
			std::cout << "Could not open the file.\n";
			return;
		}

		string dict{std::istreambuf_iterator<char>(inputFile), std::istreambuf_iterator<char>()};
		std::cout << m_dictionary << std::endl;

		LZ4_loadDict(m_dictionary, dict.c_str(), sizeof(dict));

		inputFile.close();
	}

	void PacketCompressor::CompressHeader()
	{
		IpzipCompress()
	}

	void PacketCompressor::DecompressHeader()
	{
	}

	void PacketCompressor::CompressPayload(const char* payload, char* compressed)
	{
		auto service = m_service.lock();
		if (service == nullptr) return;

		int32 compressedSize = LZ4_compress_fast_continue(m_dictionary, payload, compressed, payload.size(), compressed.size, 1);
		if (compressedSize < 0)
		{
			std::cout << "Error : failed to compress payload of packet\n";
			return;
		}



	}

	void PacketCompressor::DecompressPayload(const char* payload, char)
	{
		auto service = m_service.lock();
		if (service == nullptr) return;

		int32 decompressedSize = LZ4_decompress_safe_usingDict();
		if (decompressedSize < 0)
		{
			std::cout << "Error : failed to decompress payload of packet\n";
			return;
		}

	}


}
