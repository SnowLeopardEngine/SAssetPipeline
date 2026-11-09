#include "SAssetPipeline/Assets.h"

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <type_traits>

namespace SnowLeopardEngine::AssetPipeline
{
    namespace
    {
        // File: magic, container version, type, type version, id, compression, payload size (uncompressed), payload.
        constexpr char     kMagic[4]         = {'S', 'L', 'E', 'A'};
        constexpr uint32_t kContainerVersion = 1;
        constexpr uint32_t kCompressionNone  = 0;
        constexpr uint32_t kCompressionZstd  = 1;
        constexpr int      kZstdLevel        = 6;

        // Payload versions: bump when a type's layout changes (old files are then re-imported).
        constexpr uint32_t kTextureVersion  = 1;
        constexpr uint32_t kMeshVersion     = 1;
        constexpr uint32_t kMaterialVersion = 1;

        // Little-endian, unpadded. Vectors and strings are a uint64 count followed by the elements.
        class Writer
        {
        public:
            template<typename T>
            void Pod(const T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                const auto* bytes = reinterpret_cast<const char*>(&value);
                m_Data.insert(m_Data.end(), bytes, bytes + sizeof(T));
            }
            template<typename T>
            void PodVector(const std::vector<T>& values)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                Pod(uint64_t(values.size()));
                const auto* bytes = reinterpret_cast<const char*>(values.data());
                m_Data.insert(m_Data.end(), bytes, bytes + values.size() * sizeof(T));
            }
            void String(const std::string& text)
            {
                Pod(uint64_t(text.size()));
                m_Data.insert(m_Data.end(), text.begin(), text.end());
            }
            const std::vector<char>& Data() const { return m_Data; }

        private:
            std::vector<char> m_Data;
        };

        class Reader
        {
        public:
            explicit Reader(std::vector<char> data) : m_Data(std::move(data)) {}

            template<typename T>
            bool Pod(T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                if (m_Offset + sizeof(T) > m_Data.size())
                    return Fail();
                std::memcpy(&value, m_Data.data() + m_Offset, sizeof(T));
                m_Offset += sizeof(T);
                return true;
            }
            template<typename T>
            bool PodVector(std::vector<T>& values)
            {
                uint64_t count = 0;
                if (!Pod(count) || count > (m_Data.size() - m_Offset) / sizeof(T))
                    return Fail();
                values.resize(size_t(count));
                std::memcpy(values.data(), m_Data.data() + m_Offset, size_t(count) * sizeof(T));
                m_Offset += size_t(count) * sizeof(T);
                return true;
            }
            bool String(std::string& text)
            {
                uint64_t count = 0;
                if (!Pod(count) || count > m_Data.size() - m_Offset)
                    return Fail();
                text.assign(m_Data.data() + m_Offset, size_t(count));
                m_Offset += size_t(count);
                return true;
            }
            bool Ok() const { return m_Ok; }
            bool AtEnd() const { return m_Offset == m_Data.size(); }

        private:
            bool Fail()
            {
                m_Ok = false;
                return false;
            }

            std::vector<char> m_Data;
            size_t            m_Offset = 0;
            bool              m_Ok     = true;
        };

        void SetError(std::string* error, std::string message)
        {
            if (error != nullptr)
                *error = std::move(message);
        }

        bool WriteFile(const std::filesystem::path& file, AssetType type, uint32_t typeVersion, const AssetId& id, const Writer& payload, std::string* error)
        {
            const auto&       data = payload.Data();
            std::vector<char> compressed(ZSTD_compressBound(data.size()));
            const size_t      size = ZSTD_compress(compressed.data(), compressed.size(), data.data(), data.size(), kZstdLevel);
            if (ZSTD_isError(size))
            {
                SetError(error, std::string("zstd: ") + ZSTD_getErrorName(size));
                return false;
            }
            compressed.resize(size);

            Writer header;
            for (const char c : kMagic)
                header.Pod(c);
            header.Pod(kContainerVersion);
            header.Pod(uint32_t(type));
            header.Pod(typeVersion);
            header.Pod(id.Bytes);
            header.Pod(kCompressionZstd);
            header.Pod(uint64_t(data.size()));

            std::error_code code;
            if (file.has_parent_path())
                std::filesystem::create_directories(file.parent_path(), code);
            // Write next to the target, then rename: a crash never leaves a truncated asset behind.
            auto temporary = file;
            temporary += ".tmp";
            {
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                out.write(header.Data().data(), std::streamsize(header.Data().size()));
                out.write(compressed.data(), std::streamsize(compressed.size()));
                if (!out)
                {
                    SetError(error, "cannot write " + temporary.generic_string());
                    return false;
                }
            }
            std::filesystem::rename(temporary, file, code);
            if (code)
            {
                SetError(error, "cannot replace " + file.generic_string() + ": " + code.message());
                return false;
            }
            return true;
        }

        // Reads the header; with `payload`, also the (decompressed) payload.
        std::optional<Reader> OpenFile(const std::filesystem::path& file, AssetHeader& header, bool payload, std::string* error)
        {
            std::ifstream in(file, std::ios::binary | std::ios::ate);
            if (!in)
            {
                SetError(error, "cannot open " + file.generic_string());
                return std::nullopt;
            }
            const auto fileSize = size_t(in.tellg());
            in.seekg(0);
            constexpr size_t kHeaderSize = 4 + 4 + 4 + 4 + 16 + 4 + 8;
            std::vector<char> head(std::min(fileSize, kHeaderSize));
            in.read(head.data(), std::streamsize(head.size()));

            Reader   reader(std::move(head));
            char     magic[4] {};
            uint32_t container = 0, type = 0, compression = 0;
            uint64_t size      = 0;
            for (char& c : magic)
                reader.Pod(c);
            reader.Pod(container);
            reader.Pod(type);
            reader.Pod(header.TypeVersion);
            reader.Pod(header.Id.Bytes);
            reader.Pod(compression);
            reader.Pod(size);
            if (!reader.Ok() || std::memcmp(magic, kMagic, 4) != 0)
            {
                SetError(error, file.generic_string() + " is not a cooked asset");
                return std::nullopt;
            }
            if (container != kContainerVersion)
            {
                SetError(error, file.generic_string() + ": container version " + std::to_string(container) + " is not supported");
                return std::nullopt;
            }
            header.Type = AssetType(type);
            if (!payload)
                return Reader({});

            std::vector<char> stored(fileSize - kHeaderSize);
            in.read(stored.data(), std::streamsize(stored.size()));
            if (compression == kCompressionNone)
                return Reader(std::move(stored));
            if (compression != kCompressionZstd || size > (uint64_t(1) << 34))
            {
                SetError(error, file.generic_string() + ": unknown compression or size");
                return std::nullopt;
            }
            std::vector<char> data(static_cast<size_t>(size));
            const size_t      result = ZSTD_decompress(data.data(), data.size(), stored.data(), stored.size());
            if (ZSTD_isError(result) || result != data.size())
            {
                SetError(error, file.generic_string() + " is truncated or corrupt (zstd: " +
                                    (ZSTD_isError(result) ? ZSTD_getErrorName(result) : "size mismatch") + ")");
                return std::nullopt;
            }
            return Reader(std::move(data));
        }

        std::optional<Reader> OpenTyped(const std::filesystem::path& file, AssetType type, uint32_t version, std::string* error)
        {
            AssetHeader header;
            auto        reader = OpenFile(file, header, false, error);
            if (!reader)
                return std::nullopt;
            if (header.Type != type || header.TypeVersion != version)
            {
                SetError(error, file.generic_string() + ": expected " + ToString(type) + " v" + std::to_string(version) + ", found " +
                                    ToString(header.Type) + " v" + std::to_string(header.TypeVersion));
                return std::nullopt;
            }
            return OpenFile(file, header, true, error);
        }

        template<typename T>
        std::optional<T> Finish(Reader& reader, T&& asset, const std::filesystem::path& file, std::string* error)
        {
            if (!reader.Ok() || !reader.AtEnd())
            {
                SetError(error, file.generic_string() + " is truncated or corrupt");
                return std::nullopt;
            }
            return std::forward<T>(asset);
        }
    } // namespace

    const char* ToString(AssetType type)
    {
        switch (type)
        {
            case AssetType::Texture:
                return "Texture";
            case AssetType::Mesh:
                return "Mesh";
            case AssetType::Material:
                return "Material";
            default:
                return "Unknown";
        }
    }

    std::optional<AssetType> AssetTypeFromString(std::string_view text)
    {
        for (const auto type : {AssetType::Texture, AssetType::Mesh, AssetType::Material})
        {
            if (text == ToString(type))
                return type;
        }
        return std::nullopt;
    }

    const char* CookedExtension(AssetType type)
    {
        switch (type)
        {
            case AssetType::Texture:
                return ".sletex";
            case AssetType::Mesh:
                return ".slemesh";
            case AssetType::Material:
                return ".slemat";
            default:
                return ".sleasset";
        }
    }

    const char* ToString(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::RGBA8:
                return "RGBA8";
            case TextureFormat::RGBA8Srgb:
                return "RGBA8 sRGB";
            case TextureFormat::RGBA16F:
                return "RGBA16F";
            case TextureFormat::BC7:
                return "BC7";
            case TextureFormat::BC7Srgb:
                return "BC7 sRGB";
        }
        return "unknown";
    }

    bool IsBlockCompressed(TextureFormat format) { return format == TextureFormat::BC7 || format == TextureFormat::BC7Srgb; }

    bool IsSrgb(TextureFormat format) { return format == TextureFormat::RGBA8Srgb || format == TextureFormat::BC7Srgb; }

    uint64_t LevelSize(TextureFormat format, uint32_t width, uint32_t height)
    {
        if (IsBlockCompressed(format))
            return uint64_t((width + 3) / 4) * ((height + 3) / 4) * 16;
        return uint64_t(width) * height * (format == TextureFormat::RGBA16F ? 8 : 4);
    }

    // ------------------------------------------------------------------------------------------------ textures

    bool SaveTexture(const std::filesystem::path& file, const AssetId& id, const TextureAsset& asset, std::string* error)
    {
        Writer writer;
        writer.Pod(asset.Width);
        writer.Pod(asset.Height);
        writer.Pod(uint32_t(asset.Format));
        writer.Pod(uint64_t(asset.Mips.size()));
        for (const auto& mip : asset.Mips)
            writer.PodVector(mip);
        return WriteFile(file, AssetType::Texture, kTextureVersion, id, writer, error);
    }

    std::optional<TextureAsset> LoadTexture(const std::filesystem::path& file, std::string* error)
    {
        auto reader = OpenTyped(file, AssetType::Texture, kTextureVersion, error);
        if (!reader)
            return std::nullopt;
        TextureAsset asset;
        uint32_t     format = 0;
        uint64_t     mips   = 0;
        reader->Pod(asset.Width);
        reader->Pod(asset.Height);
        reader->Pod(format);
        reader->Pod(mips);
        asset.Format = TextureFormat(format);
        if (!reader->Ok() || mips == 0 || mips > 32 || format > uint32_t(TextureFormat::BC7Srgb))
        {
            SetError(error, file.generic_string() + ": invalid texture header");
            return std::nullopt;
        }
        asset.Mips.resize(size_t(mips));
        for (auto& mip : asset.Mips)
            reader->PodVector(mip);
        // Each level must hold exactly its texels.
        uint32_t w = asset.Width, h = asset.Height;
        for (const auto& mip : asset.Mips)
        {
            if (mip.size() != LevelSize(asset.Format, w, h))
            {
                SetError(error, file.generic_string() + ": mip size does not match its extent");
                return std::nullopt;
            }
            w = std::max(w / 2, 1u);
            h = std::max(h / 2, 1u);
        }
        return Finish(*reader, std::move(asset), file, error);
    }

    // ------------------------------------------------------------------------------------------------ meshes

    bool SaveMesh(const std::filesystem::path& file, const AssetId& id, const MeshAsset& asset, std::string* error)
    {
        Writer writer;
        writer.PodVector(asset.Vertices);
        writer.PodVector(asset.Indices);
        writer.Pod(uint64_t(asset.SubMeshes.size()));
        for (const auto& sub : asset.SubMeshes)
        {
            writer.String(sub.Name);
            writer.Pod(sub.FirstVertex);
            writer.Pod(sub.VertexCount);
            writer.Pod(sub.FirstIndex);
            writer.Pod(sub.IndexCount);
            writer.Pod(sub.MaterialSlot);
            writer.Pod(sub.LocalBounds);
        }
        writer.Pod(uint64_t(asset.MaterialSlots.size()));
        for (const auto& slot : asset.MaterialSlots)
            writer.String(slot);
        writer.Pod(asset.LocalBounds);
        return WriteFile(file, AssetType::Mesh, kMeshVersion, id, writer, error);
    }

    std::optional<MeshAsset> LoadMesh(const std::filesystem::path& file, std::string* error)
    {
        auto reader = OpenTyped(file, AssetType::Mesh, kMeshVersion, error);
        if (!reader)
            return std::nullopt;
        MeshAsset asset;
        reader->PodVector(asset.Vertices);
        reader->PodVector(asset.Indices);
        uint64_t count = 0;
        reader->Pod(count);
        for (uint64_t i = 0; i < count && reader->Ok(); ++i)
        {
            SubMesh sub;
            reader->String(sub.Name);
            reader->Pod(sub.FirstVertex);
            reader->Pod(sub.VertexCount);
            reader->Pod(sub.FirstIndex);
            reader->Pod(sub.IndexCount);
            reader->Pod(sub.MaterialSlot);
            reader->Pod(sub.LocalBounds);
            asset.SubMeshes.push_back(std::move(sub));
        }
        count = 0;
        reader->Pod(count);
        for (uint64_t i = 0; i < count && reader->Ok(); ++i)
            reader->String(asset.MaterialSlots.emplace_back());
        reader->Pod(asset.LocalBounds);
        // Sub-meshes must stay inside the buffers (the engine copies these ranges).
        for (const auto& sub : asset.SubMeshes)
        {
            if (uint64_t(sub.FirstVertex) + sub.VertexCount > asset.Vertices.size() ||
                uint64_t(sub.FirstIndex) + sub.IndexCount > asset.Indices.size())
            {
                SetError(error, file.generic_string() + ": sub-mesh " + sub.Name + " is out of range");
                return std::nullopt;
            }
        }
        return Finish(*reader, std::move(asset), file, error);
    }

    // ------------------------------------------------------------------------------------------------ materials

    bool SaveMaterial(const std::filesystem::path& file, const AssetId& id, const MaterialAsset& asset, std::string* error)
    {
        Writer writer;
        writer.String(asset.Name);
        writer.String(asset.Shader);
        writer.String(asset.RenderQueue);
        writer.Pod(uint8_t(asset.TextureFlipY));
        writer.Pod(int32_t(asset.TileSize));
        writer.Pod(uint8_t(asset.EnableInstancing));
        writer.Pod(uint64_t(asset.Properties.size()));
        for (const auto& property : asset.Properties)
        {
            writer.String(property.Name);
            writer.String(property.Type);
            writer.String(property.Value);
            writer.Pod(property.Texture.Bytes);
        }
        return WriteFile(file, AssetType::Material, kMaterialVersion, id, writer, error);
    }

    std::optional<MaterialAsset> LoadMaterial(const std::filesystem::path& file, std::string* error)
    {
        auto reader = OpenTyped(file, AssetType::Material, kMaterialVersion, error);
        if (!reader)
            return std::nullopt;
        MaterialAsset asset;
        uint8_t       flip = 0, instancing = 0;
        int32_t       tile = 1;
        reader->String(asset.Name);
        reader->String(asset.Shader);
        reader->String(asset.RenderQueue);
        reader->Pod(flip);
        reader->Pod(tile);
        reader->Pod(instancing);
        asset.TextureFlipY     = flip != 0;
        asset.TileSize         = tile;
        asset.EnableInstancing = instancing != 0;
        uint64_t count         = 0;
        reader->Pod(count);
        for (uint64_t i = 0; i < count && reader->Ok(); ++i)
        {
            MaterialProperty property;
            reader->String(property.Name);
            reader->String(property.Type);
            reader->String(property.Value);
            reader->Pod(property.Texture.Bytes);
            asset.Properties.push_back(std::move(property));
        }
        return Finish(*reader, std::move(asset), file, error);
    }

    std::optional<AssetHeader> ReadHeader(const std::filesystem::path& file, std::string* error)
    {
        AssetHeader header;
        if (!OpenFile(file, header, false, error))
            return std::nullopt;
        return header;
    }
} // namespace SnowLeopardEngine::AssetPipeline
