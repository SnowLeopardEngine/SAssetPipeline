#pragma once

#include "SAssetPipeline/AssetRegistry.h"
#include "SAssetPipeline/Import/ImportSettings.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace SnowLeopardEngine::AssetPipeline
{
    struct ImportOptions
    {
        bool Force = false; // re-import even when nothing changed
        std::function<void(std::string_view)> Log; // progress / warnings (null: quiet)
    };

    struct ImportReport
    {
        uint32_t                 Imported        = 0;
        uint32_t                 UpToDate        = 0;
        uint32_t                 Failed          = 0;
        uint32_t                 Removed         = 0; // assets whose source is gone
        uint32_t                 SidecarsCreated = 0;
        std::vector<std::string> Errors;

        bool Ok() const { return Failed == 0; }
    };

    // Imports the source files of an asset root into cooked assets (`<root>/.imported/`):
    //   - every importable file gets a `.simport` sidecar (its id and importer parameters) on first sight;
    //   - a file is re-imported only when its content, its parameters, the importer version or a dependency changed
    //     (hashes in `.imported/import-db.json`), or with Force;
    //   - assets whose source file disappeared are removed from the registry and the cooked folder.
    // Importers: textures (png, jpg, tga, bmp; hdr as float), meshes (Assimp: obj, fbx, gltf, glb, dae), materials
    // (.dzmaterial, texture paths resolved to texture ids).
    class AssetImporter
    {
    public:
        explicit AssetImporter(std::filesystem::path root);

        ImportReport ImportAll(const ImportOptions& options = {});
        // One source file (absolute, or relative to the root); its sidecar is created if missing.
        bool Import(const std::filesystem::path& source, const ImportOptions& options = {}, std::string* error = nullptr);

        const AssetRegistry& GetRegistry() const { return m_Registry; }

        // The importer of a source file ("texture", "mesh", "material"), or empty if it is not imported.
        static std::string ImporterFor(const std::filesystem::path& source);

    private:
        std::filesystem::path m_Root;
        AssetRegistry         m_Registry;
    };
} // namespace SnowLeopardEngine::AssetPipeline
