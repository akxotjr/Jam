#pragma once

#include <jampx/PhysicsTypes.h>


namespace jam::net
{
	/**----------------------------------------------------------- 
		Common Components											
	 -----------------------------------------------------------**/
	 

	/// @brief	
	struct NetId
	{
	private:
		uint32 v = 0;

		static constexpr uint32 k_levelBit  = 0x80000000u;
		static constexpr uint32 k_valueMask = 0x7FFFFFFFu;

		explicit constexpr NetId(uint32 raw) noexcept : v(raw) {}

	public:
		constexpr NetId() noexcept = default;
		constexpr bool operator==(const NetId&) const noexcept = default;

		constexpr bool   IsValid()   const noexcept { return v != 0; }
		constexpr bool   IsLevel()   const noexcept { return (v & k_levelBit) != 0; }
		constexpr bool   IsRuntime() const noexcept { return (v & k_levelBit) == 0; }
		constexpr uint32 Value()     const noexcept { return v & k_valueMask; }
		constexpr uint32 Raw()       const noexcept { return v; }

		static constexpr NetId Invalid() noexcept { return NetId(0); }

		static constexpr NetId MakeRaw(uint32 raw) noexcept
		{
			return NetId(raw);
		}

		static constexpr NetId MakeRuntime(uint32 seed) noexcept
		{
			const uint32 value = (seed & k_valueMask);
			return (value == 0) ? Invalid() : NetId(value);
		}

		static constexpr NetId MakeLevel(uint32 seed) noexcept
		{
			const uint32 value = (seed & k_valueMask);
			return (value == 0) ? Invalid() : NetId(k_levelBit | value);
		}
	};


	/// @brief	PhysicsFacade(ObjectId) is WorldBase(registry) local Key. 
	///			rule : ObjectId == entt::entity 
	inline px::ObjectId MakeObjectId(entt::entity e) noexcept
	{
		return static_cast<px::ObjectId>(e);
	}

	/// @brief	PrefabKey(in PhyiscsFacade) of NetActor
	struct NetPrefabKey
	{
		px::PrefabKey		key;
	};


	struct NetActorBodyType
	{
		px::eBodyType		body = px::eBodyType::Rigid;
	};


	struct NetTeamPartRole
	{
		uint16				team = 0;
		uint8				part = 0;
		uint8				role = 0;

		static constexpr uint32 Pack(uint16 team, uint8 part, uint8 role) noexcept
		{
			return (static_cast<uint32>(team) & 0xFFFFu)
				| ((static_cast<uint32>(part) & 0xFFu) << 16)
				| ((static_cast<uint32>(role) & 0xFFu) << 24);
		}

		constexpr uint32 Packed() const noexcept
		{
			return Pack(team, part, role);
		}

		static constexpr NetTeamPartRole FromPacked(uint32 packed) noexcept
		{
			NetTeamPartRole out{};
			out.team = static_cast<uint16>(packed & 0xFFFFu);
			out.part = static_cast<uint8>((packed >> 16) & 0xFFu);
			out.role = static_cast<uint8>((packed >> 24) & 0xFFu);
			return out;
		}
	};

	/// @brief	Tag of successful spawned in PhysicsFacade
	struct PhysicsSpawnedTag{};

	/// @brief	Client predict spawn tag. not ensured by server
	struct NetPendingSpawnTag {};

	/// @brief	Client predict spwan identifier
	struct NetSpawnRequestId
	{
		uint32					requestId = 0;
	};

	/// @brief 새로 생성된 액터 표시
	struct NewlyCreatedTag {};



	/// @note Ownership vs Control
	///		소유권(Ownership) != 조종권(Control)
	///		- Ownership: 누가 이 액터를 생성하였는가? (권한, Despawn 권한)
	///		- Control  : 누가 이 액터를 조종하는가? (입력 권한)
	///		=> 소유권자는 조종권을 가질 수도 있고, 아닐 수도 있다.
	///		=> 즉, 조종권을 가지려면 소유해야된다. 

	/// @brief 액터 소유권
	/// @details 기본적으로 소유권 변경 불가. 
	struct OwnershipTag
	{
		uint64					userId = 0;		// = 0: 서버 소유. > 0: userId 소유자가 소유
	};

	/// @brief 액터 조종권
	/// @details 조종권 변경 가능
	struct ControlTag
	{
		uint64					userId = 0;		// = 0: 조종자 없음. > 0: userId 소유자가 조종권 소유 
	};


	/**-----------------------------------------------------------
		Client-side Components
	 -----------------------------------------------------------**/


	/// @brief Tag of local character. Local character is unique in WorldBase.
	struct LocalActorTag {};

	/// @brief Tag of remote actor. Remote actors are the remaining actors excluding a local actor.
	struct RemoteActorTag {};

	/// @brief Actor exists locally but is currently hidden because it left this client's AOI.
	struct OutOfAoiTag {};

	/// @brief Actor is locally hidden after predicted expiry and waits for authoritative destroy.
	struct PredictedDespawnTag {};



	/**-----------------------------------------------------------
		State Components (Authority / Proxy / Replay)
	 -----------------------------------------------------------**/

	template <typename TState>
	struct AuthorityState
	{
		TState state = {};
	};

	template <typename TState>
	struct ProxyState
	{
		TState state = {};
	};

	/// @brief rigid state from server snapshot
	using RigidAuthorityState = AuthorityState<px::RigidState>;

	/// @brief character state from server snapshot
	using CharAuthorityState = AuthorityState<px::CharacterState>;

	/// @brief rigid state from PhysicsFacade main scene readback
	using RigidProxyState = ProxyState<px::RigidState>;

	/// @brief character state from PhysicsFacade main scene readback
	using CharProxyState = ProxyState<px::CharacterState>;

	template <typename TState>
	struct ReplayHistorySample
	{
		uint64 tick  = 0;
		TState state = {};
	};

	template <typename TState>
	struct ReplayHistoryBuffer
	{
		std::deque<ReplayHistorySample<TState>> samples;
		uint32 maxSamples = 128;

		void Push(uint64 tick, const TState& state)
		{
			samples.push_back(ReplayHistorySample<TState>{.tick = tick, .state = state });
			while (samples.size() > maxSamples)
				samples.pop_front();
		}

		/// @brief tick 이하에서 가장 최신 샘플 조회
		const ReplayHistorySample<TState>* FindLatestLE(uint64 tick) const
		{
			for (auto it = samples.rbegin(); it != samples.rend(); ++it)
			{
				if (it->tick <= tick)
					return &(*it);
			}
			return nullptr;
		}
	};

	using RigidReplayHistory = ReplayHistoryBuffer<px::RigidState>;
	using CharReplayHistory  = ReplayHistoryBuffer<px::CharacterState>;

	/// @brief 후보군 태그 (capability 통과)
	struct ReplayCandidateTag {};

	/// @brief 현재 correction/replay 대상 태그
	struct ReplayRelevantTag {};

	/// @brief retention / hysteresis 런타임 상태
	struct ReplayRetention
	{
		uint32 minHoldTicks   = 3;
		uint32 remainingTicks = 0;
	};


	struct TargetInfo
	{
		NetId		 targetNetId = NetId::Invalid();
		px::ObjectId targetObjId = px::INVALID_OBJ_ID;
	};


	/**-----------------------------------------------------------
		Server-side Components
	 -----------------------------------------------------------**/

	struct ReplicationStaticTag {};

	/// @brief PhyiscsFacade 로 받은 ActiveList 에 포함된 경우에만 부착.
	struct ReplicationActiveTag{};



	// --- helpers ---

	static entt::entity GetLocalEntity(entt::registry& world)
	{
		auto view = world.view<LocalActorTag>();
		return view.empty() ? entt::null : view.front();
	}

} // namespace jam::net



namespace std
{
	template<>
	struct hash<jam::net::NetId>
	{
		size_t operator()(const jam::net::NetId& id) const noexcept
		{
			return std::hash<uint32>{}(id.Raw());
		}
	};
} // namespace std
