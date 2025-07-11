#pragma once

namespace jam::net
{
	struct FragmentHeader
	{
		uint32	fragmentId;    // ���� �޽����� �����ϴ� ���� ID
		uint16	index;         // ���� ������ �ε���
		uint16	totalCount;    // ��ü ���� ��
		bool	isLast;          // ���� ���� ���� (optional)
	};


	class FragmentHandler
	{
	public:
		FragmentHandler() = default;
		~FragmentHandler() = default;

	private:

	};
}
