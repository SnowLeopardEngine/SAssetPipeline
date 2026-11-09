#pragma once

// Internal: the importers behind AssetImporter.

#include "SAssetPipeline/Assets.h"
#include "SAssetPipeline/Import/ImportSettings.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace SnowLeopardEngine::AssetPipeline::Detail
{
    struct ImportContext
    {
        std::filesystem::path Root;   // asset root
        std::filesystem::path Source; // absolute
        const ImportSettings* Settings = nullptr;
        // Id of another source file (absolute path), if it is an asset of this root.
        std::function<std::optional<AssetId>(const std::filesystem::path&)> ResolveId;

        std::vector<AssetId> Dependencies; // filled by the importer
        std::vector<std::string> Warnings;
        std::string          Error;
    };

    struct ImporterInfo
    {
        const char* Name;
        AssetType   Type;
        uint32_t    Version; // bump to re-import every asset of this importer
        void (*DefaultParams)(const std::filesystem::path& source, std::map<std::string, std::string>& params);
    };

    // nullptr: not an importable file.
    const ImporterInfo* FindImporterForSource(const std::filesystem::path& source);
    const ImporterInfo* FindImporter(const std::string& name);

    bool ImportTexture(ImportContext& context, TextureAsset& asset);
    bool ImportMesh(ImportContext& context, MeshAsset& asset);
    bool ImportMaterial(ImportContext& context, MaterialAsset& asset);

    void TextureDefaults(const std::filesystem::path& source, std::map<std::string, std::string>& params);
    void MeshDefaults(const std::filesystem::path& source, std::map<std::string, std::string>& params);
    void MaterialDefaults(const std::filesystem::path& source, std::map<std::string, std::string>& params);
} // namespace SnowLeopardEngine::AssetPipeline::Detail
