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

        // ������ȭ (���÷��̽� - ���� ��ü�� ����)
        template<typename T>
        static bool Deserialize(const BYTE* src, uint32 size, OUT T& dst);

        // ������ȭ (������ ��ȯ - �޸� ȿ����)
        template<typename T>
        static DeserializationResult DeserializePtr(const BYTE* src, uint32 size);

    private:
        // Ÿ�Ժ� ����ȭ ����
        template<typename T>
        static SerializationResult SerializeBinary(const T& src);

        template<typename T>
        static SerializationResult SerializeFlatBuffer(const T& src);

        template<typename T>
        static SerializationResult SerializeProtobuf(const T& src);

        template<typename T>
        static SerializationResult SerializeJson(const T& src);

        // Ÿ�Ժ� ������ȭ ����
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

    // ���� ������ȭ �Լ� (���÷��̽�)
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
            // FlatBuffer�� ���÷��̽� ������ȭ�� �������� ����
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

    // ���� ������ȭ �Լ� (������ ��ȯ)
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
            // Protobuf�� JSON�� ������ ��ȯ ����� �������� ���� (���� �Ҵ� �ʿ�)
            return { .objectPtr= nullptr, .size= 0, .success= false};
        }
    }

    // === Binary ����ȭ/������ȭ ===
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

    // === FlatBuffer ����ȭ/������ȭ ===
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

    // === Protobuf ����ȭ/������ȭ ===
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

    // === JSON ����ȭ/������ȭ ===
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

