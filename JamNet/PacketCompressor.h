#pragma once
#include <filesystem>


namespace jam::net
{
	/**
	 * @brief Compress Header and Payload.
	 *
	 * Packet Sturct
	 * ---------------------------------------------------------------------
	 * | UDP Header(Kernel lv) - 8byte | Packet Header (User lv) | Payload |
	 * ---------------------------------------------------------------------
	 *
	 * - Packet Header compressed by ipzip
	 * - Payload compressed by lz4
	 *
	 * 
	 * 
	 */
	class PacketCompressor
	{
	public:
		PacketCompressor(const Sptr<Service>& service);
		~PacketCompressor();

		void Init(std::filesystem::path filePath);

		void CompressHeader();
		void DecompressHeader();

		void CompressPayload(const char* payload);
		void DecompressPayload(const char* payload);

		

	private:
		Wptr<Service>	m_service;
		Ipzip*			m_pIpzip = nullptr;
		LZ4_stream_t*	m_dictionary = nullptr;
	};
}

