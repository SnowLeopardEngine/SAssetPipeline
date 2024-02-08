#include "SAssetPipeline/Raw/Loaders/ObjLoader.h"
#include "SCore/Macro.h"

#include <tiny_obj_loader.h>

namespace SnowLeopardEngine::AssetPipeline
{
    static tinyobj::ObjReaderConfig s_ReaderConfig;
    static tinyobj::ObjReader       s_Reader;

    bool ObjLoader::Load(const std::filesystem::path& modelFilePath, RawModelAsset& outModelAsset)
    {
        if (!s_Reader.ParseFromFile(modelFilePath.generic_string(), s_ReaderConfig))
        {
            if (!s_Reader.Error().empty())
            {
                SNOW_LEOPARD_CORE_ERROR(s_Reader.Error());
            }
            return false;
        }

        if (!s_Reader.Warning().empty())
        {
            SNOW_LEOPARD_CORE_WARN(s_Reader.Warning());
        }

        auto& attrib    = s_Reader.GetAttrib();
        auto& shapes    = s_Reader.GetShapes();
        auto& materials = s_Reader.GetMaterials();

        // Loop over shapes
        for (size_t s = 0; s < shapes.size(); s++)
        {
            // Create a mesh item
            RawMeshItem item;
            item.Name = shapes[s].name;

            // Loop over faces(polygon)
            size_t index_offset = 0;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++)
            {
                auto fv = size_t(shapes[s].mesh.num_face_vertices[f]);

                // Loop over vertices in the face.
                for (size_t v = 0; v < fv; v++)
                {
                    VertexData vertexData = {};

                    // access to vertex
                    tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                    tinyobj::real_t  vx  = attrib.vertices[3 * size_t(idx.vertex_index) + 0];
                    tinyobj::real_t  vy  = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
                    tinyobj::real_t  vz  = attrib.vertices[3 * size_t(idx.vertex_index) + 2];
                    vertexData.Position  = {vx, vy, vz};

                    // Check if `normal_index` is zero or positive. negative = no normal data
                    if (idx.normal_index >= 0)
                    {
                        tinyobj::real_t nx = attrib.normals[3 * size_t(idx.normal_index) + 0];
                        tinyobj::real_t ny = attrib.normals[3 * size_t(idx.normal_index) + 1];
                        tinyobj::real_t nz = attrib.normals[3 * size_t(idx.normal_index) + 2];
                        vertexData.Normal  = {nx, ny, nz};
                    }

                    // Check if `texcoord_index` is zero or positive. negative = no texcoord data
                    if (idx.texcoord_index >= 0)
                    {
                        tinyobj::real_t tx   = attrib.texcoords[2 * size_t(idx.texcoord_index) + 0];
                        tinyobj::real_t ty   = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
                        vertexData.TexCoords = {tx, ty};
                    }

                    // Optional: vertex colors
                    // tinyobj::real_t red   = attrib.colors[3*size_t(idx.vertex_index)+0];
                    // tinyobj::real_t green = attrib.colors[3*size_t(idx.vertex_index)+1];
                    // tinyobj::real_t blue  = attrib.colors[3*size_t(idx.vertex_index)+2];

                    item.Data.Vertices.push_back(vertexData);
                }
                index_offset += fv;

                // per-face material, ignored here
                // shapes[s].mesh.material_ids[f];
            }
            for (int i = 0; i < item.Data.Vertices.size(); i++)
            {
                item.Data.Indices.push_back(i);
            }

            outModelAsset.AddMeshItem(item);
        }

        return true;
    }
} // namespace SnowLeopardEngine::AssetPipeline
