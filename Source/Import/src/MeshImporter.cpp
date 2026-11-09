#include "Importers.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace SnowLeopardEngine::AssetPipeline::Detail
{
    namespace
    {
        void Grow(Bounds& bounds, const float* p, bool& empty)
        {
            for (int i = 0; i < 3; ++i)
            {
                bounds.Min[i] = empty ? p[i] : std::min(bounds.Min[i], p[i]);
                bounds.Max[i] = empty ? p[i] : std::max(bounds.Max[i], p[i]);
            }
            empty = false;
        }

        struct Builder
        {
            const aiScene* Scene;
            float          Scale;
            bool           Bake;
            MeshAsset*     Mesh;
            bool           Empty = true;

            void Node(const aiNode* node, const aiMatrix4x4& parent)
            {
                const aiMatrix4x4 global = Bake ? parent * node->mTransformation : aiMatrix4x4();
                for (uint32_t i = 0; i < node->mNumMeshes; ++i)
                    Part(Scene->mMeshes[node->mMeshes[i]], global);
                for (uint32_t i = 0; i < node->mNumChildren; ++i)
                    Node(node->mChildren[i], global);
            }

            void Part(const aiMesh* source, const aiMatrix4x4& transform)
            {
                if (!source->HasFaces() || !(source->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
                    return;
                aiMatrix3x3 normalMatrix(transform);
                normalMatrix.Inverse().Transpose();

                SubMesh sub;
                sub.Name         = source->mName.C_Str();
                sub.FirstVertex  = uint32_t(Mesh->Vertices.size());
                sub.VertexCount  = source->mNumVertices;
                sub.FirstIndex   = uint32_t(Mesh->Indices.size());
                sub.MaterialSlot = source->mMaterialIndex;
                bool subEmpty    = true;
                for (uint32_t v = 0; v < source->mNumVertices; ++v)
                {
                    const aiVector3D position = transform * source->mVertices[v];
                    aiVector3D       normal   = source->HasNormals() ? normalMatrix * source->mNormals[v] : aiVector3D(0, 1, 0);
                    normal.NormalizeSafe();
                    MeshVertex vertex {{position.x * Scale, position.y * Scale, position.z * Scale}, {normal.x, normal.y, normal.z}, {0, 0}};
                    if (source->HasTextureCoords(0))
                    {
                        vertex.TexCoord[0] = source->mTextureCoords[0][v].x;
                        vertex.TexCoord[1] = source->mTextureCoords[0][v].y;
                    }
                    Grow(sub.LocalBounds, vertex.Position, subEmpty);
                    Grow(Mesh->LocalBounds, vertex.Position, Empty);
                    Mesh->Vertices.push_back(vertex);
                }
                for (uint32_t f = 0; f < source->mNumFaces; ++f)
                {
                    const aiFace& face = source->mFaces[f];
                    if (face.mNumIndices != 3)
                        continue; // points / lines left by Triangulate
                    Mesh->Indices.insert(Mesh->Indices.end(), face.mIndices, face.mIndices + 3);
                }
                sub.IndexCount = uint32_t(Mesh->Indices.size()) - sub.FirstIndex;
                Mesh->SubMeshes.push_back(std::move(sub));
            }
        };
    } // namespace

    void MeshDefaults(const std::filesystem::path&, std::map<std::string, std::string>& params)
    {
        params["scale"]          = "1";
        params["flipUVs"]        = "true";  // v = 0 at the top: the engine stores textures top row first
        params["smoothNormals"]  = "true";  // for sources without normals
        params["bakeTransforms"] = "true";  // node transforms into the vertices (one rigid mesh)
    }

    bool ImportMesh(ImportContext& context, MeshAsset& asset)
    {
        const auto& settings = *context.Settings;
        unsigned    flags    = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                         aiProcess_ImproveCacheLocality | aiProcess_GlobalScale | aiProcess_ValidateDataStructure;
        flags |= settings.GetBool("smoothNormals", true) ? aiProcess_GenSmoothNormals : aiProcess_GenNormals;
        if (settings.GetBool("flipUVs", true))
            flags |= aiProcess_FlipUVs;

        Assimp::Importer importer;
        const aiScene*   scene = importer.ReadFile(context.Source.generic_string(), flags);
        if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || scene->mRootNode == nullptr)
        {
            context.Error = std::string("Assimp: ") + importer.GetErrorString();
            return false;
        }

        Builder builder {scene, settings.GetFloat("scale", 1.0f), settings.GetBool("bakeTransforms", true), &asset};
        builder.Node(scene->mRootNode, aiMatrix4x4());
        for (uint32_t i = 0; i < scene->mNumMaterials; ++i)
            asset.MaterialSlots.emplace_back(scene->mMaterials[i]->GetName().C_Str());

        if (asset.SubMeshes.empty() || asset.Indices.empty())
        {
            context.Error = "no triangle meshes in the file";
            return false;
        }
        for (uint32_t i = 0; i < scene->mNumMeshes; ++i)
        {
            if (scene->mMeshes[i]->HasBones())
            {
                context.Warnings.push_back("skinned meshes are imported in bind pose (skeletons and animations: next step)");
                break;
            }
        }
        return true;
    }
} // namespace SnowLeopardEngine::AssetPipeline::Detail
