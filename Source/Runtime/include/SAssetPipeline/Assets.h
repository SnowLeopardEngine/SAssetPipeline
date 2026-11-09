#pragma once

#include "SAssetPipeline/AssetId.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace SnowLeopardEngine::AssetPipeline
{
    // Cooked assets: what the importers produce and the engine loads. Each is one binary file
    // (`<asset root>/.imported/<id>.<extension>`): a header (magic, container version, type, type version, id) and the
    // payload below. Layouts match the engine's runtime types, so loading is a read and an upload.
    enum class AssetType : uint32_t
    {
        Unknown  = 0,
        Texture  = 1,
        Mesh     = 2,
        Material = 3,
    };

    const char*               ToString(AssetType type);
    std::optional<AssetType>  AssetTypeFromString(std::string_view text);
    const char*               CookedExtension(AssetType type); // ".sletex", ".slemesh", ".slemat"

    // ---- textures

    // Pixel encodings. "Srgb" marks colour whose values are sRGB-encoded (albedo, emissive, UI); the engine decodes it
    // where it samples it. The importer filters mips on linear values either way.
    enum class TextureFormat : uint32_t
    {
        RGBA8     = 0, // 8-bit linear data (roughness, metalness, masks, normal maps)
        RGBA8Srgb = 1, // 8-bit colour
        RGBA16F   = 2, // HDR (environment maps): half floats
        BC7       = 3, // BC7 blocks (4 x 4 texels in 16 bytes) of linear data
        BC7Srgb   = 4, // BC7 blocks of colour
    };

    const char* ToString(TextureFormat format);
    bool        IsBlockCompressed(TextureFormat format);
    bool        IsSrgb(TextureFormat format);
    // Bytes of one level of `width` x `height` texels (blocks round up).
    uint64_t    LevelSize(TextureFormat format, uint32_t width, uint32_t height);

    // Rows top to bottom (the image file's order), mips from full size down to 1 x 1 (or a single level).
    struct TextureAsset
    {
        uint32_t                          Width  = 0;
        uint32_t                          Height = 0;
        TextureFormat                     Format = TextureFormat::RGBA8;
        std::vector<std::vector<uint8_t>> Mips;
    };

    // ---- meshes

    // The engine's MeshVertexData (32 bytes).
    struct MeshVertex
    {
        float Position[3];
        float Normal[3];
        float TexCoord[2];
    };
    static_assert(sizeof(MeshVertex) == 32);

    struct Bounds
    {
        float Min[3] = {0, 0, 0};
        float Max[3] = {0, 0, 0};
    };

    // A part of the mesh with its own material slot (an Assimp mesh). Indices are relative to FirstVertex.
    struct SubMesh
    {
        std::string Name;
        uint32_t    FirstVertex  = 0;
        uint32_t    VertexCount  = 0;
        uint32_t    FirstIndex   = 0;
        uint32_t    IndexCount   = 0;
        uint32_t    MaterialSlot = 0;
        Bounds      LocalBounds;
    };

    struct MeshAsset
    {
        std::vector<MeshVertex>  Vertices;
        std::vector<uint32_t>    Indices;
        std::vector<SubMesh>     SubMeshes;
        std::vector<std::string> MaterialSlots; // names from the source file
        Bounds                   LocalBounds;
    };

    // ---- materials

    // A .dzmaterial, with texture paths resolved to texture assets.
    struct MaterialProperty
    {
        std::string Name;
        std::string Type;  // .dzmaterial types: Color, Vector, Float, Int, Texture2D
        std::string Value; // the source text (a Texture2D keeps its source path, for tools)
        AssetId     Texture; // Texture2D: the imported texture (nil: none / not found)
    };

    struct MaterialAsset
    {
        std::string                   Name;
        std::string                   Shader      = "Standard/Standard";
        std::string                   RenderQueue = "Opaque";
        bool                          TextureFlipY = false;
        int                           TileSize     = 1;
        bool                          EnableInstancing = false;
        std::vector<MaterialProperty> Properties;
    };

    // ---- files

    struct AssetHeader
    {
        AssetType Type        = AssetType::Unknown;
        uint32_t  TypeVersion = 0;
        AssetId   Id;
    };

    // Writers return false and set `error` on failure; readers return nullopt.
    bool SaveTexture(const std::filesystem::path& file, const AssetId& id, const TextureAsset& asset, std::string* error = nullptr);
    bool SaveMesh(const std::filesystem::path& file, const AssetId& id, const MeshAsset& asset, std::string* error = nullptr);
    bool SaveMaterial(const std::filesystem::path& file, const AssetId& id, const MaterialAsset& asset, std::string* error = nullptr);

    std::optional<AssetHeader>   ReadHeader(const std::filesystem::path& file, std::string* error = nullptr);
    std::optional<TextureAsset>  LoadTexture(const std::filesystem::path& file, std::string* error = nullptr);
    std::optional<MeshAsset>     LoadMesh(const std::filesystem::path& file, std::string* error = nullptr);
    std::optional<MaterialAsset> LoadMaterial(const std::filesystem::path& file, std::string* error = nullptr);
} // namespace SnowLeopardEngine::AssetPipeline
