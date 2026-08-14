#include "pch.h"
#include "CharacterContent.h"

#include <Protocol/Cpp/character_content_generated.h>

namespace m1
{
	namespace
	{
		jam::net::GenericContentResponse MakeResponse(const jam::net::GenericContentRequest& request, jam::net::eGenericContentResponseStatus status)
		{
			return {
				.requestId	= request.requestId,
				.opCode		= request.opCode,
				.status		= status,
			};
		}

		std::vector<std::byte> CopyPayload(const flatbuffers::FlatBufferBuilder& builder)
		{
			const auto* begin = reinterpret_cast<const std::byte*>(builder.GetBufferPointer());
			return { begin, begin + builder.GetSize() };
		}

		flatbuffers::Offset<fb::fbCharacterSummary> MakeCharacterSummary(flatbuffers::FlatBufferBuilder& builder, const CharacterRecord& character)
		{
			return fb::CreatefbCharacterSummary(
				builder,
				character.characterId,
				builder.CreateString(character.name),
				character.actorArchetypeKey.v);
		}
	}

	CharacterContent::CharacterContent(std::shared_ptr<CharacterStore> characters, std::shared_ptr<CharacterSessionStore> sessions)
		: m_characters(std::move(characters))
		, m_sessions(std::move(sessions))
	{
	}

	void CharacterContent::HandleRequest(const jam::net::GenericContentPrincipal& principal, const jam::net::GenericContentRequest& request, jam::net::GenericContentRequestCompleted completed)
	{
		if (!completed)
			return;

		switch (request.opCode)
		{
		case kCharacterListOperation:
			completed(HandleList(principal, request));
			return;
		case kCharacterSelectOperation:
			completed(HandleSelect(principal, request));
			return;
		default:
			completed(MakeResponse(request, jam::net::eGenericContentResponseStatus::InvalidRequest));
			return;
		}
	}

	jam::net::GenericContentResponse CharacterContent::HandleList(const jam::net::GenericContentPrincipal& principal, const jam::net::GenericContentRequest& request) const
	{
		if (!m_characters || principal.accountId == jam::net::kInvalidAccountId || !request.payload.empty())
			return MakeResponse(request, jam::net::eGenericContentResponseStatus::InvalidRequest);

		flatbuffers::FlatBufferBuilder builder(256);
		std::vector<flatbuffers::Offset<fb::fbCharacterSummary>> summaries;
		const auto characters = m_characters->FindByAccount(principal.accountId);
		summaries.reserve(characters.size());

		for (const CharacterRecord& character : characters)
			summaries.push_back(MakeCharacterSummary(builder, character));

		const auto root = fb::CreatefbCharacterListResponse(builder, builder.CreateVector(summaries));
		builder.Finish(root);

		auto response = MakeResponse(request, jam::net::eGenericContentResponseStatus::Succeeded);
		response.payload = CopyPayload(builder);
		return response;
	}

	jam::net::GenericContentResponse CharacterContent::HandleSelect(const jam::net::GenericContentPrincipal& principal, const jam::net::GenericContentRequest& request) const
	{
		if (!m_characters || !m_sessions
			|| principal.accountId == jam::net::kInvalidAccountId
			|| principal.userId == jam::net::kInvalidUserId
			|| request.payload.empty())
		{
			return MakeResponse(request, jam::net::eGenericContentResponseStatus::InvalidRequest);
		}

		flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(request.payload.data()), request.payload.size());
		if (!verifier.VerifyBuffer<fb::fbCharacterSelectRequest>(nullptr))
			return MakeResponse(request, jam::net::eGenericContentResponseStatus::InvalidRequest);

		const auto* wire = flatbuffers::GetRoot<fb::fbCharacterSelectRequest>(request.payload.data());
		const auto character = m_characters->FindOwnedCharacter(principal.accountId, wire->character_id());
		
		if (!character)
			return MakeResponse(request, jam::net::eGenericContentResponseStatus::Rejected);
		if (!m_sessions->SelectCharacter(principal.accountId, principal.userId, *character))
			return MakeResponse(request, jam::net::eGenericContentResponseStatus::Rejected);

		flatbuffers::FlatBufferBuilder builder(256);
		const auto root = fb::CreatefbCharacterSelectResponse(builder, MakeCharacterSummary(builder, *character));
		builder.Finish(root);

		auto response = MakeResponse(request, jam::net::eGenericContentResponseStatus::Succeeded);
		response.payload = CopyPayload(builder);

		return response;
	}
}
