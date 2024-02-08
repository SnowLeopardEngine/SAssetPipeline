#pragma once

#include "SAssetPipeline/Base.h"
#include "SCore/Math.h"

namespace SnowLeopardEngine::AssetPipeline
{
    struct Vertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoords;
    };

    struct RawMesh
    {
        std::vector<Vertex>   Vertices;
        std::vector<uint32_t> Indices;
    };
} // namespace SnowLeopardEngine::AssetPipeline