#pragma once

#include "jamnet/runtime/world/actor/ActorArchetypeDatabase.h"
#include "jamnet/runtime/world/actor/ActorId.h"
#include "jamnet/runtime/session/ClientRequestId.h"

#include <jampx/PhysicsTypes.h>

#include <deque>

#include <entt/entt.hpp>

namespace jam::net
{
	/**----------------------------------------------------------- 
		Common Components											
	 -----------------------------------------------------------**/
	 

	/// @brief	Returns the opaque JamPx actor key for an ECS actor.
	///			The raw entt::entity value must never cross this boundary.
	inline ActorId GetActorIdComponent(const entt::registry& world, entt::entity e) noexcept
	{
		const auto* actorId = world.try_get<ActorId>(e);
		return actorId ? *actorId : ActorId::Invalid();
	}

	inline px::ActorId GetPhysicsActorId(const entt::registry& world, entt::entity e) noexcept
	{
		return GetActorIdComponent(world, e).Value();
	}

	struct PhysicsArchetypeRef
	{
		px::PhysicsArchetypeKey		key;
	};

	struct ActorArchetypeRef
	{
		ActorArchetypeKey		key;
	};


	struct ActorBodyType
	{
		px::eBodyType		body = px::eBodyType::Rigid;
	};


	/// @brief	Tag of successful spawned in PhysicsFacade
	struct PhysicsSpawnedTag{};

	/// @brief Client request correlation replicated in lifecycle metadata.
	struct ClientRequestCorrelation
	{
		ClientRequestId			requestId = kInvalidClientRequestId;
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
	struct LocallyHiddenTag {};



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
		ActorId		 targetActorId = ActorId::Invalid();
		px::ActorId resolvedActorId = px::INVALID_ACTOR_ID;
	};


	/**-----------------------------------------------------------
		Server-side Components
	 -----------------------------------------------------------**/

	struct ReplicationStaticTag {};
	struct ReplicationDisabledTag {};

	/// @brief PhyiscsFacade 로 받은 ActiveList 에 포함된 경우에만 부착.
	struct ReplicationActiveTag{};



	// --- helpers ---

	static entt::entity GetLocalEntity(entt::registry& world)
	{
		auto view = world.view<LocalActorTag>();
		return view.empty() ? entt::null : view.front();
	}

} // namespace jam::net
