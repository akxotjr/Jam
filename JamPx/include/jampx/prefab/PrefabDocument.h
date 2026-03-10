#pragma once
#include <filesystem>
#include <optional>

#include <nlohmann/json.hpp>

#include "jampx/prefab/PhysicsPrefabIO.h"

namespace jam::px
{

    struct PrefabDocument
    {
	    std::filesystem::path        path;
        json                         doc;
        bool                         dirty = false;

        uint64                       revision = 0;              // doc가 "의미적으로" 변경될 때마다 증가
        uint64                       builtRevision = ~0ull;     // cachedAsset이 만들어진 revision

	    std::optional<PhysicsPrefabAsset> cachedAsset;  // doc->asset 빌드 결과 캐시(미리보기/PIE 등에 사용)

        void Touch() noexcept
        {
            dirty = true;
            ++revision;
        }

        void ClearRuntimeCache() noexcept
        {
            cachedAsset.reset();
            builtRevision = ~0ull;
        }
    };



    // 열기/저장/빌드 API
    static PrefabDocument OpenPrefab(const std::filesystem::path& path)
    {
        PrefabDocument d{};
        d.path          = path;
        d.doc           = PhysicsPrefabIO::LoadPrefabJsonFromFile(path.string());
        d.dirty         = false;
        d.revision      = 0;
        d.builtRevision = ~0ull;

        d.cachedAsset.reset();

        return d;
    }

    static void SavePrefab(PrefabDocument& d)
    {
        PhysicsPrefabIO::SavePrefabJsonToFile(d.path.string(), d.doc);
        d.dirty = false;
    }

    // doc->asset은 무겁기 때문에 필요할 때만 빌드 + revision으로 캐시
    static const PhysicsPrefabAsset& GetOrBuildAsset(PrefabDocument& d)
    {
        if (!d.cachedAsset.has_value() || d.builtRevision != d.revision)
        {
            // 파일 IO 없이 doc에서 곧바로 asset 빌드
            d.cachedAsset   = PhysicsPrefabIO::LoadPrefabAssetFromJson(d.doc);
            d.builtRevision = d.revision;
        }
        return *d.cachedAsset;
    }





    class PrefabEditor
    {
    public:
        PrefabEditor(PrefabDocument& doc) : m_doc(doc) {}
        ~PrefabEditor()
        {
            if (m_changed) m_doc.Touch();
        }

        // 전체 문서 직접 수정(쓰기 의도)
        json& Doc() noexcept
        {
            m_changed = true;
            return m_doc.doc;
        }

        // 읽기 전용 접근(없으면 nullptr)
        const json* TryGetPtr(const json::json_pointer& ptr) const noexcept
        {
            return m_doc.doc.contains(ptr) ? &m_doc.doc.at(ptr) : nullptr;
        }

        // ptr 위치를 "반드시 존재" 시키고 그 노드를 반환. (중간 노드도 자동 생성)
        json& Ensure(const json::json_pointer& ptr)
        {
            m_changed = true;
            return m_doc.doc[ptr];
        }

        // ptr 위치를 "반드시 객체로" 보장하고 그 객체를 반환
        json& EnsureObject(const json::json_pointer& ptr)
        {
            json& n = Ensure(ptr);
            if (!n.is_object()) n = json::object();
            return n;
        }

        // ptr 위치에 값을 설정
        template<class T>
        void Set(const json::json_pointer& ptr, T&& value)
        {
            m_changed = true;
            m_doc.doc[ptr] = std::forward<T>(value);
        }

        // ptr 위치를 삭제(있을 때만). 성공 여부 반환
        bool Erase(const json::json_pointer& ptr)
        {
            if (!m_doc.doc.contains(ptr)) return false;
            m_changed = true;
            m_doc.doc.erase(ptr.to_string());
            return true;
        }

        bool Changed() const noexcept { return m_changed; }

    private:
        PrefabDocument&     m_doc;
        bool                m_changed = false;
    };


    // ----------------------------
    // Prefab Level Document
    // ----------------------------
    struct PrefabLevelDocument
    {
	    std::filesystem::path     path;
        json                 doc;
        bool                 dirty = false;

        uint64               revision = 0;

        void Touch() noexcept
        {
            dirty = true;
            ++revision;
        }
    };



    static PrefabLevelDocument OpenPrefabLevel(const std::filesystem::path& path)
    {
        PrefabLevelDocument d{};
        d.path      = path;
        d.doc       = PhysicsPrefabIO::LoadLevelJsonFromFile(path.string());
        d.dirty     = false;
        d.revision  = 0;
        return d;
    }

    static void SavePrefabLevel(PrefabLevelDocument& d)
    {
        PhysicsPrefabIO::SaveLevelJsonToFile(d.path.string(), d.doc);
        d.dirty = false;
    }

    class PrefabLevelEditor
    {
    public:
        PrefabLevelEditor(PrefabLevelDocument& doc) : m_doc(doc) {}
        ~PrefabLevelEditor()
        {
            if (m_changed) m_doc.Touch();
        }

        json& Doc() noexcept
        {
            m_changed = true;
            return m_doc.doc;
        }

        const json* TryGetPtr(const json::json_pointer& ptr) const noexcept
        {
            return m_doc.doc.contains(ptr) ? &m_doc.doc.at(ptr) : nullptr;
        }

        json& Ensure(const json::json_pointer& ptr)
        {
            m_changed = true;
            return m_doc.doc[ptr];
        }

        json& EnsureObject(const json::json_pointer& ptr)
        {
            json& n = Ensure(ptr);
            if (!n.is_object()) n = json::object();
            return n;
        }

        template<class T>
        void Set(const json::json_pointer& ptr, T&& value)
        {
            m_changed = true;
            m_doc.doc[ptr] = std::forward<T>(value);
        }

        bool Erase(const json::json_pointer& ptr)
        {
            if (!m_doc.doc.contains(ptr)) return false;
            m_changed = true;
            m_doc.doc.erase(ptr.to_string());
            return true;
        }

        bool Changed() const noexcept { return m_changed; }

    private:
        PrefabLevelDocument&    m_doc;
        bool                    m_changed = false;
    };



} // namespace jam::px
