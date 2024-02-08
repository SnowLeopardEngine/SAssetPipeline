#pragma once

#include "SAssetPipeline/Raw/Assets/RawMesh.h"

namespace SnowLeopardEngine::AssetPipeline
{
    class RawModelAsset
    {
    public:
        RawModelAsset() = default;
        RawModelAsset(const std::vector<RawMeshItem> meshItems) : m_MeshItems(meshItems) {}

        void AddMeshItem(const RawMeshItem& meshItem) { m_MeshItems.emplace_back(meshItem); }

    private:
        std::vector<RawMeshItem> m_MeshItems;
    };
} // namespace SnowLeopardEngine::AssetPipeline
