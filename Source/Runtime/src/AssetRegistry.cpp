#include "SAssetPipeline/AssetRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace SnowLeopardEngine::AssetPipeline
{
    namespace
    {
        constexpr int kRegistryVersion = 1;

        void SetError(std::string* error, std::string message)
        {
            if (error != nullptr)
                *error = std::move(message);
        }
    } // namespace

    std::string AssetRegistry::Normalize(std::string_view source) const
    {
        std::string path(source);
        std::replace(path.begin(), path.end(), '\\', '/');
        while (path.rfind("./", 0) == 0)
            path.erase(0, 2);
        // "Assets/Textures/X.png" with root ".../Assets" -> "Textures/X.png"
        const auto rootName = m_Root.filename().generic_string();
        if (!rootName.empty() && path.size() > rootName.size() && path.compare(0, rootName.size(), rootName) == 0 &&
            path[rootName.size()] == '/')
        {
            path.erase(0, rootName.size() + 1);
        }
        // An absolute path inside the root.
        std::error_code code;
        if (std::filesystem::path(path).is_absolute())
        {
            const auto relative = std::filesystem::path(path).lexically_relative(m_Root);
            if (!relative.empty() && *relative.begin() != "..")
                path = relative.generic_string();
        }
        return path;
    }

    bool AssetRegistry::Load(std::string* error)
    {
        m_Entries.clear();
        m_BySource.clear();
        std::ifstream in(GetFile());
        if (!in)
        {
            return true; // nothing imported yet
        }
        try
        {
            const auto json = nlohmann::json::parse(in);
            if (json.value("version", 0) != kRegistryVersion)
            {
                SetError(error, GetFile().generic_string() + ": unsupported registry version");
                return false;
            }
            for (const auto& item : json.at("assets"))
            {
                RegistryEntry entry;
                const auto    id   = AssetId::Parse(item.at("id").get<std::string>());
                const auto    type = AssetTypeFromString(item.at("type").get<std::string>());
                if (!id || !type)
                    continue;
                entry.Id     = *id;
                entry.Type   = *type;
                entry.Source = item.at("source").get<std::string>();
                entry.Cooked = item.at("cooked").get<std::string>();
                for (const auto& dependency : item.value("dependencies", nlohmann::json::array()))
                {
                    if (const auto dependencyId = AssetId::Parse(dependency.get<std::string>()))
                        entry.Dependencies.push_back(*dependencyId);
                }
                Upsert(std::move(entry));
            }
        }
        catch (const std::exception& e)
        {
            SetError(error, GetFile().generic_string() + ": " + e.what());
            return false;
        }
        return true;
    }

    bool AssetRegistry::Save(std::string* error) const
    {
        // Sorted by source path: a stable file that diffs well.
        std::vector<const RegistryEntry*> entries;
        for (const auto& [id, entry] : m_Entries)
            entries.push_back(&entry);
        std::sort(entries.begin(), entries.end(), [](const auto* a, const auto* b) { return a->Source < b->Source; });

        nlohmann::json assets = nlohmann::json::array();
        for (const auto* entry : entries)
        {
            nlohmann::json dependencies = nlohmann::json::array();
            for (const auto& dependency : entry->Dependencies)
                dependencies.push_back(dependency.ToString());
            assets.push_back({{"id", entry->Id.ToString()},
                              {"type", ToString(entry->Type)},
                              {"source", entry->Source},
                              {"cooked", entry->Cooked},
                              {"dependencies", dependencies}});
        }
        const nlohmann::json json {{"version", kRegistryVersion}, {"assets", assets}};

        std::error_code code;
        std::filesystem::create_directories(GetFile().parent_path(), code);
        std::ofstream out(GetFile(), std::ios::trunc);
        if (!out)
        {
            SetError(error, "cannot write " + GetFile().generic_string());
            return false;
        }
        out << json.dump(2) << '\n';
        return bool(out);
    }

    const RegistryEntry* AssetRegistry::Find(const AssetId& id) const
    {
        const auto found = m_Entries.find(id);
        return found != m_Entries.end() ? &found->second : nullptr;
    }

    const RegistryEntry* AssetRegistry::FindBySource(std::string_view source) const
    {
        const auto found = m_BySource.find(Normalize(source));
        return found != m_BySource.end() ? Find(found->second) : nullptr;
    }

    void AssetRegistry::Upsert(RegistryEntry entry)
    {
        if (const auto* previous = Find(entry.Id); previous != nullptr && previous->Source != entry.Source)
        {
            m_BySource.erase(previous->Source);
        }
        m_BySource[entry.Source] = entry.Id;
        m_Entries[entry.Id]      = std::move(entry);
    }

    void AssetRegistry::Remove(const AssetId& id)
    {
        if (const auto* entry = Find(id))
        {
            m_BySource.erase(entry->Source);
            m_Entries.erase(id);
        }
    }
} // namespace SnowLeopardEngine::AssetPipeline
