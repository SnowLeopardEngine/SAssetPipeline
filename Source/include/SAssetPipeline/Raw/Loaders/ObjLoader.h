#pragma once

#include "SAssetPipeline/Base.h"
#include "SAssetPipeline/Raw/Assets/RawModelAsset.h"

namespace SnowLeopardEngine::AssetPipeline
{
    class ObjLoader
    {
    public:
        static bool Load(const std::filesystem::path& modelFilePath, RawModelAsset& outModelAsset);
    };
} // namespace SnowLeopardEngine::AssetPipeline
