#pragma once

#include <jambase/JamTypes.h>

#include <functional>

namespace jam::net
{
	// Canonical actor identity within one WorldRuntimeRef. The raw layout is
	// interpreted only by ActorDirectory; other systems treat it as opaque.
	struct ActorId
	{
	private:
		uint32 value = 0;
	
	public:
		constexpr ActorId() noexcept = default;
		explicit constexpr ActorId(uint32 v) noexcept : value(v) {}

		constexpr bool		IsValid() const noexcept { return value != 0; }
		constexpr uint32	Value() const noexcept { return value; }
		constexpr bool		operator==(const ActorId&) const noexcept = default;
		
		static constexpr ActorId Invalid() noexcept { return ActorId(0); }
	};
}

namespace std
{
	template<>
	struct hash<jam::net::ActorId>
	{
		size_t operator()(const jam::net::ActorId& id) const noexcept
		{
			return std::hash<uint32>{}(id.Value());
		}
	};
}
