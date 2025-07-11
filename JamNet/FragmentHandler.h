#pragma once

namespace jam::net
{
	struct FragmentHeader
	{
		uint32	fragmentId;    // 동일 메시지를 구성하는 고유 ID
		uint16	index;         // 현재 조각의 인덱스
		uint16	totalCount;    // 전체 조각 수
		bool	isLast;          // 최종 조각 여부 (optional)
	};


	class FragmentHandler
	{
	public:
		FragmentHandler() = default;
		~FragmentHandler() = default;

	private:

	};
}
