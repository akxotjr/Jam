#pragma once

#include "jamnet/core/net/IAuthenticator.h"
#include "jamnet/core/net/IocpEvent.h"
#include "jamnet/core/net/Session.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>


namespace jam::net
{
	enum class eAdmissionPhase : uint8
	{
		Active,
		Promoting,
		Closing,
	};

	enum class eTcpAdmissionState : uint8
	{
		AwaitingBind,
		Authenticating,
		AuthRetryWaiting,
	};

	struct AdmissionKey
	{
		uint64			admissionId	= 0;
		eProtocolType	protocol	= eProtocolType::NONE;
		EndpointId		endpointId	= kInvalidEndpointId;

		bool operator==(const AdmissionKey&) const = default;
	};

	struct AdmissionKeyHasher
	{
		size_t operator()(const AdmissionKey& key) const noexcept
		{
			const size_t admissionHash = std::hash<uint64>()(key.admissionId);
			const size_t protocolHash = std::hash<uint8>()(static_cast<uint8>(key.protocol));
			const size_t endpointHash = std::hash<uint64>()(key.endpointId);
			const size_t endpointSeed = admissionHash ^ (protocolHash + 0x9e3779b97f4a7c15ull + (admissionHash << 6) + (admissionHash >> 2));
			return endpointSeed ^ (endpointHash + 0x9e3779b97f4a7c15ull + (endpointSeed << 6) + (endpointSeed >> 2));
		}
	};

	struct TcpAdmissionData
	{
		eTcpAdmissionState				state			= eTcpAdmissionState::AwaitingBind;
		std::optional<AuthCredential>	authCredential	= {};
		uint32							authRetryCount	= 0;
		uint64							authDeadlineNs	= 0_ns;
	};

	struct UdpAdmissionData
	{
		uint64	transactionId = 0;
	};

	using AdmissionProtocolData = std::variant<TcpAdmissionData, UdpAdmissionData>;

	struct AdmissionEntry
	{
		eAdmissionPhase				phase		= eAdmissionPhase::Active;
		std::unique_ptr<Session>	session;
		AdmissionProtocolData		protocolData;
	};

	using AdmissionTable = std::unordered_map<AdmissionKey, AdmissionEntry, AdmissionKeyHasher>;




	struct AuthCompletedEvent final : IocpEvent
	{
		AdmissionKey	key		= {};
		AuthResult		result	= {};

		AuthCompletedEvent() : IocpEvent(eEventType::AdmissionAuthCompleted) {}
	};

	struct AuthRetryEvent final : IocpEvent
	{
		AdmissionKey	key = {};

		AuthRetryEvent() : IocpEvent(eEventType::AdmissionAuthRetry) {}
	};

	struct AuthTimeoutEvent final : IocpEvent
	{
		AdmissionKey	key = {};

		AuthTimeoutEvent() : IocpEvent(eEventType::AdmissionAuthTimeout) {}
	};



	struct AdmissionTcpReleaseEvent final : IocpEvent
	{
		AdmissionKey	key		  = {};
		Session*		expected  = nullptr;

		AdmissionTcpReleaseEvent() : IocpEvent(eEventType::AdmissionTcpRelease) {}
	};





	class AdmissionContext final : public IocpObject
	{
	public:
		explicit AdmissionContext(IocpCore* iocpCore) : m_iocpCore(iocpCore) {}

		bool			SetAuthenticator(std::shared_ptr<IAuthenticator> authenticator);
		AdmissionKey	AddEntry(std::unique_ptr<Session> session);
		
		void			RequestTcpRelease(const AdmissionKey& key, Session* expected);

		void			OnTcpPacket(const AdmissionKey& key, Packet packet);
		void			OnUdpPacket(const AdmissionKey& key, Packet packet);

		void			Dispatch(IocpEvent* event, int32 bytes) override;
		HANDLE			GetHandle() override { return INVALID_HANDLE_VALUE; }

	private:
		void			SubmitAuthentication(const AdmissionKey& key, AuthCredential credential);

		void			PostAuthCompleted(const AdmissionKey& key, const AuthResult& result);
		void			PostAuthRetry(const AdmissionKey& key);
		void			PostAuthTimeout(const AdmissionKey& key);

		void			ScheduleAuthRetry(const AdmissionKey& key, TcpAdmissionData& tcp);
		void			ScheduleAuthTimeout(const AdmissionKey& key);
		
		void			OnAuthCompleted(const AuthCompletedEvent& event);
		void			OnAuthRetry(const AuthRetryEvent& event);
		void			OnAuthTimeout(const AuthTimeoutEvent& event);
		
		void			RejectTcp(AdmissionEntry& entry);
		
		void			ReleaseTcpEntry(const AdmissionTcpReleaseEvent& event);
		

		void			PromoteTcpToShard(const AdmissionKey& key, AdmissionEntry& entry, uint64 principalId);
		void			PromoteUdpToShard(const AdmissionKey& key, AdmissionEntry& entry, uint64 accountId, RuntimeId userId, uint64 transactionId);

		AdmissionEntry*	FindAdmissionEntry(const AdmissionKey& key);

	private:
		IocpCore*						m_iocpCore = nullptr;
		std::shared_ptr<IAuthenticator> m_authenticator;
		AdmissionTable					m_entries;
		uint64							m_nextAdmissionId = 1;
	};
}
