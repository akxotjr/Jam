#pragma once

#ifdef ENABLE_PROTOBUF
#include <google/protobuf/message.h>
#endif


#ifdef ENABLE_JSON
#include <nlohmann/json.hpp>
#endif


//#define ENABLE_PROTOBUF
//#define ENABLE_JSON


namespace jam::net
{
	enum class eSerializationType : uint8
	{
		BINARY		= 1,
		FLATBUFFER	= 2,
		PROTOBUF	= 3,
		JSON		= 4
	};

    template<typename T>
    struct SerializationType
    {
        static constexpr eSerializationType value = []()
    		{
#ifdef ENABLE_PROTOBUF
                if constexpr (std::is_base_of_v<google::protobuf::Message, T>) 
                    return eSerializationType::PROTOBUF;
#endif

#ifdef ENABLE_JSON
                if constexpr (std::is_base_of_v<nlohmann::json, T>) 
                    return eSerializationType::JSON;
#endif

                if constexpr (std::is_trivially_copyable_v<T>) 
                    return eSerializationType::BINARY;

                return eSerializationType::FLATBUFFER;
            }();
    };

    struct SerializationResult
    {
        xvector<BYTE>       data;       // Serialized binary data
        bool                success;    
    };

    struct DeserializationResult
    {
        const void*         objectPtr;  // Deserialized object pointer (for zero-copy)
        uint32              size;
        bool                success;
    };

	class Serializer
	{
	public:
        template<typename T>
        static SerializationResult Serialize(const T& src);

        // 역직렬화 (인플레이스 - 기존 객체에 복사)
        template<typename T>
        static bool Deserialize(const BYTE* src, uint32 size, OUT T& dst);

        // 역직렬화 (포인터 반환 - 메모리 효율적)
        template<typename T>
        static DeserializationResult DeserializePtr(const BYTE* src, uint32 size);

    private:
        // 타입별 직렬화 구현
        template<typename T>
        static SerializationResult SerializeBinary(const T& src);

        template<typename T>
        static SerializationResult SerializeFlatBuffer(const T& src);

        template<typename T>
        static SerializationResult SerializeProtobuf(const T& src);

        template<typename T>
        static SerializationResult SerializeJson(const T& src);

        // 타입별 역직렬화 구현
        template<typename T>
        static bool DeserializeBinary(const BYTE* src, uint32 size, OUT T& dst);

        template<typename T>
        static DeserializationResult DeserializeFlatBufferPtr(const BYTE* src, uint32 size);

        template<typename T>
        static bool DeserializeProtobuf(const BYTE* src, uint32 size, OUT T& dst);

        template<typename T>
        static bool DeserializeJson(const BYTE* src, uint32 size, OUT T& dst);
	};

	template <typename T>
	SerializationResult Serializer::Serialize(const T& src)
	{
        constexpr auto type = SerializationType<T>::value;

        if constexpr (type == eSerializationType::BINARY) {
            return SerializeBinary(src);
        }
        else if constexpr (type == eSerializationType::FLATBUFFER) {
            return SerializeFlatBuffer(src);
        }
        else if constexpr (type == eSerializationType::PROTOBUF) {
            return SerializeProtobuf(src);
        }
        else if constexpr (type == eSerializationType::JSON) {
            return SerializeJson(src);
        }

        return { {}, false };
	}

    // 메인 역직렬화 함수 (인플레이스)
    template<typename T>
    bool Serializer::Deserialize(const BYTE* src, uint32 size, OUT T& dst)
    {
        constexpr auto type = SerializationType<T>::value;

        if constexpr (type == eSerializationType::BINARY) 
        {
            return DeserializeBinary(src, size, dst);
        }
        else if constexpr (type == eSerializationType::FLATBUFFER)
        {
            // FlatBuffer는 인플레이스 역직렬화를 지원하지 않음
            auto result = DeserializeFlatBufferPtr<T>(src, size);
            if (result.success && result.objectPtr) 
            {
                dst = *reinterpret_cast<const T*>(result.objectPtr);
                return true;
            }
            return false;
        }
        else if constexpr (type == eSerializationType::PROTOBUF) 
        {
            return DeserializeProtobuf(src, size, dst);
        }
        else if constexpr (type == eSerializationType::JSON) 
        {
            return DeserializeJson(src, size, dst);
        }

        return false;
    }

    // 메인 역직렬화 함수 (포인터 반환)
    template<typename T>
    DeserializationResult Serializer::DeserializePtr(const BYTE* src, uint32 size)
    {
        constexpr auto type = SerializationType<T>::value;

        if constexpr (type == eSerializationType::BINARY) 
        {
            if (size == sizeof(T)) 
                return { reinterpret_cast<const T*>(src), size, true };

        	return { .objectPtr= nullptr, .size= 0, .success= false};
        }
        else if constexpr (type == eSerializationType::FLATBUFFER) 
        {
            return DeserializeFlatBufferPtr<T>(src, size);
        }
        else 
        {
            // Protobuf와 JSON은 포인터 반환 방식을 지원하지 않음 (동적 할당 필요)
            return { .objectPtr= nullptr, .size= 0, .success= false};
        }
    }

    // === Binary 직렬화/역직렬화 ===
    template<typename T>
    SerializationResult Serializer::SerializeBinary(const T& src)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Binary serialization requires trivially copyable type");

        xvector<BYTE> data(sizeof(T));
        std::memcpy(data.data(), &src, sizeof(T));

        return { std::move(data), true };
    }

    template<typename T>
    bool Serializer::DeserializeBinary(const BYTE* src, uint32 size, OUT T& dst)
    {
        static_assert(std::is_trivially_copyable_v<T>, "Binary deserialization requires trivially copyable type");

        if (size != sizeof(T))
            return false;

        std::memcpy(&dst, src, sizeof(T));
        return true;
    }

    // === FlatBuffer 직렬화/역직렬화 ===
    template<typename T>
    SerializationResult Serializer::SerializeFlatBuffer(const T& src)
    {
        flatbuffers::FlatBufferBuilder fbb;
        auto offset = T::Pack(fbb, &src);
        T::Finish(fbb, offset);

        xvector<BYTE> data(fbb.GetSize());
        std::memcpy(data.data(), fbb.GetBufferPointer(), fbb.GetSize());

        return { std::move(data), true };
    }

    template<typename T>
    DeserializationResult Serializer::DeserializeFlatBufferPtr(const BYTE* src, uint32 size)
    {
        flatbuffers::Verifier verifier(src, size);
        if (!verifier.VerifyBuffer<T>(nullptr))
            return { nullptr, 0, false};

        const T* obj = flatbuffers::GetRoot<T>(src);
        return { obj, size, true };
    }

    // === Protobuf 직렬화/역직렬화 ===
    template<typename T>
    SerializationResult Serializer::SerializeProtobuf(const T& src)
    {
#ifdef ENABLE_PROTOBUF
        static_assert(std::is_base_of_v<google::protobuf::Message, T>, "Protobuf serialization requires protobuf::Message");

        const uint32 size = src.ByteSizeLong();
        xvector<BYTE> data(size);
        if (!src.SerializeToArray(data.data(), size))
            return { {}, false };

        return { std::move(data), true};
#else
        return { {}, false };
#endif
    }

    template<typename T>
    bool Serializer::DeserializeProtobuf(const BYTE* src, uint32 size, OUT T& dst)
    {
#ifdef ENABLE_PROTOBUF
        static_assert(std::is_base_of_v<google::protobuf::Message, T>, "Protobuf deserialization requires protobuf::Message");

        return dst.ParseFromArray(src, size);
#else
        return false;
#endif
    }

    // === JSON 직렬화/역직렬화 ===
    template<typename T>
    SerializationResult Serializer::SerializeJson(const T& src)
    {
#ifdef ENABLE_JSON
        try 
        {
            nlohmann::json jsonObj = src;
            std::string jsonStr = jsonObj.dump();

            uint32 size = jsonStr.size();
        	xvector<BYTE> data(size);

            std::memcpy(data.data(), jsonStr.c_str(), size);

            return { std::move(data), true };
        }
        catch (...) 
        {
            return { {}, false };
        }
#else
        return { {},false };
#endif
    }

    template<typename T>
    bool Serializer::DeserializeJson(const BYTE* src, uint32 size, OUT T& dst)
    {
#ifdef ENABLE_JSON
        try 
        {
            std::string jsonStr(reinterpret_cast<const char*>(src), size);
            nlohmann::json jsonObj = nlohmann::json::parse(jsonStr);
            dst = jsonObj.get<T>();
            return true;
        }
        catch (...) 
        {
            return false;
        }
#else
        return false;
#endif
    }
}

