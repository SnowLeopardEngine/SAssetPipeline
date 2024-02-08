#pragma once

#include "SAssetPipeline/Base.h"
#include "SCore/Math.h"

namespace SnowLeopardEngine::AssetPipeline
{
    struct VertexData
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoords;
    };

    struct MeshData
    {
        std::vector<VertexData> Vertices;
        std::vector<uint32_t>   Indices;
    };

    struct RawMeshItem
    {
        std::string Name;
        MeshData    Data;
    };
} // namespace SnowLeopardEngine::AssetPipeline