#pragma once

namespace jam::net
{
	class PacketEncryptor
	{
	public:

		PacketEncryptor() = default;
		~PacketEncryptor() = default;

		void Encrypt();
		void Decrypt();
	};
}

