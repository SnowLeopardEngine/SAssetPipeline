#pragma once

#include "SAssetPipeline/Assets.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace SnowLeopardEngine::AssetPipeline
{
    // Where cooked assets live, relative to the asset root (gitignored; rebuilt by an import).
    inline constexpr const char* kImportedFolder = ".imported";
    inline constexpr const char* kRegistryFile   = "registry.json";

    struct RegistryEntry
    {
        AssetId              Id;
        AssetType            Type = AssetType::Unknown;
        std::string          Source; // relative to the asset root, '/' separated ("Textures/Grass.png")
        std::string          Cooked; // relative to the asset root (".imported/<id>.sletex")
        std::vector<AssetId> Dependencies; // assets this one refers to (a material's textures)
    };

    // The imported assets of one asset root: id -> entry, source path -> id. Written by the importer, read by the
    // runtime (`<root>/.imported/registry.json`).
    class AssetRegistry
    {
    public:
        explicit AssetRegistry(std::filesystem::path root = {}) : m_Root(std::move(root)) {}

        const std::filesystem::path& GetRoot() const { return m_Root; }
        std::filesystem::path        GetFile() const { return m_Root / kImportedFolder / kRegistryFile; }

        // A missing registry is an empty one (nothing imported yet): Load returns true and the registry stays empty.
        bool Load(std::string* error = nullptr);
        bool Save(std::string* error = nullptr) const;

        const RegistryEntry* Find(const AssetId& id) const;
        // `source` relative to the root; also accepts paths that start with the root folder's name ("Assets/...",
        // the form the engine's scenes and materials use) and '\' separators.
        const RegistryEntry* FindBySource(std::string_view source) const;
        std::filesystem::path GetCookedPath(const RegistryEntry& entry) const { return m_Root / entry.Cooked; }

        void Upsert(RegistryEntry entry);
        void Remove(const AssetId& id);

        const std::unordered_map<AssetId, RegistryEntry>& GetEntries() const { return m_Entries; }

        // Source paths as stored: relative to the root, '/' separators.
        std::string Normalize(std::string_view source) const;

    private:
        std::filesystem::path                        m_Root;
        std::unordered_map<AssetId, RegistryEntry>   m_Entries;
        std::unordered_map<std::string, AssetId>     m_BySource;
    };
} // namespace SnowLeopardEngine::AssetPipeline
