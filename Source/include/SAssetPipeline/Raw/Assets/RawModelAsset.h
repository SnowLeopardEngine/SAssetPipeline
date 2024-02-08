#pragma once

#include "SAssetPipeline/Raw/Assets/RawMesh.h"

namespace SnowLeopardEngine::AssetPipeline
{
    class RawModelAsset
    {
    public:
        RawModelAsset() = default;
        RawModelAsset(const std::vector<RawMesh> meshes) : m_Meshes(meshes) {}

        void AddMesh(const RawMesh& mesh) { m_Meshes.emplace_back(mesh); }

    private:
        std::vector<RawMesh> m_Meshes;
    };
} // namespace SnowLeopardEngine::AssetPipeline
