#pragma once

#include "SAssetPipeline/AssetId.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace SnowLeopardEngine::AssetPipeline
{
    // `<source file>.simport`, next to the source and committed with it: the asset's stable id, its importer and the
    // importer's parameters. Only what a person decides lives here; hashes and timestamps go to the import database,
    // so the file does not change on every import.
    //
    //   [import]
    //   version=1
    //   importer=texture
    //   id=6f1c...
    //
    //   [params]
    //   srgb=true
    struct ImportSettings
    {
        int                                Version = 1;
        std::string                        Importer;
        AssetId                            Id;
        std::map<std::string, std::string> Params; // sorted: stable files, stable hashes

        static std::filesystem::path SidecarOf(const std::filesystem::path& source)
        {
            auto sidecar = source;
            sidecar += ".simport";
            return sidecar;
        }

        static std::optional<ImportSettings> Load(const std::filesystem::path& sidecar, std::string* error = nullptr);
        bool                                 Save(const std::filesystem::path& sidecar, std::string* error = nullptr) const;

        std::string GetString(const std::string& key, const std::string& fallback) const;
        bool        GetBool(const std::string& key, bool fallback) const;
        float       GetFloat(const std::string& key, float fallback) const;
        int         GetInt(const std::string& key, int fallback) const;
    };
} // namespace SnowLeopardEngine::AssetPipeline
