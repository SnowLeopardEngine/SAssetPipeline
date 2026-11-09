// sap: the SnowLeopardEngine asset pipeline command line.
//
//   sap import <asset root> [--force] [--quiet]   import new / changed sources, remove assets whose source is gone
//   sap list <asset root>                         the registry: id, type, source
//   sap info <cooked file>                        what a cooked asset holds
//
// Exit code: 0 on success, 1 when an import failed, 2 on bad usage.

#include "SAssetPipeline/Import/AssetImporter.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace SnowLeopardEngine::AssetPipeline;

namespace
{
    int Usage()
    {
        std::fprintf(stderr,
                     "usage:\n"
                     "  sap import <asset root> [--force] [--quiet]\n"
                     "  sap list <asset root>\n"
                     "  sap info <cooked file>\n");
        return 2;
    }

    int Import(const std::filesystem::path& root, bool force, bool quiet)
    {
        const auto    start = std::chrono::steady_clock::now();
        AssetImporter importer(root);
        ImportOptions options;
        options.Force = force;
        options.Log   = [quiet](std::string_view message) {
            // Warnings and errors always; progress unless --quiet.
            if (!quiet || message.starts_with("warning") || message.starts_with("error"))
                std::cout << "[sap] " << message << '\n';
        };
        const auto report  = importer.ImportAll(options);
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("[sap] %s: %u imported, %u up to date, %u removed, %u failed, %u new sidecars (%.2f s)\n",
                    importer.GetRegistry().GetRoot().generic_string().c_str(),
                    report.Imported,
                    report.UpToDate,
                    report.Removed,
                    report.Failed,
                    report.SidecarsCreated,
                    seconds);
        for (const auto& error : report.Errors)
            std::fprintf(stderr, "[sap] error: %s\n", error.c_str());
        return report.Ok() ? 0 : 1;
    }

    int List(const std::filesystem::path& root)
    {
        AssetRegistry registry(std::filesystem::absolute(root).lexically_normal());
        std::string   error;
        if (!registry.Load(&error))
        {
            std::fprintf(stderr, "[sap] %s\n", error.c_str());
            return 1;
        }
        for (const auto& [id, entry] : registry.GetEntries())
            std::printf("%s  %-8s  %s\n", id.ToString().c_str(), ToString(entry.Type), entry.Source.c_str());
        return 0;
    }

    int Info(const std::filesystem::path& file)
    {
        std::string error;
        const auto  header = ReadHeader(file, &error);
        if (!header)
        {
            std::fprintf(stderr, "[sap] %s\n", error.c_str());
            return 1;
        }
        std::printf("%s v%u, id %s\n", ToString(header->Type), header->TypeVersion, header->Id.ToString().c_str());
        switch (header->Type)
        {
            case AssetType::Texture:
                if (const auto texture = LoadTexture(file, &error))
                    std::printf("  %u x %u, format %u, %zu mips\n", texture->Width, texture->Height, uint32_t(texture->Format), texture->Mips.size());
                break;
            case AssetType::Mesh:
                if (const auto mesh = LoadMesh(file, &error))
                {
                    std::printf("  %zu vertices, %zu indices, %zu sub-meshes, %zu material slots\n",
                                mesh->Vertices.size(), mesh->Indices.size(), mesh->SubMeshes.size(), mesh->MaterialSlots.size());
                    for (const auto& sub : mesh->SubMeshes)
                        std::printf("    %s: %u vertices, %u indices, slot %u\n", sub.Name.c_str(), sub.VertexCount, sub.IndexCount, sub.MaterialSlot);
                }
                break;
            case AssetType::Material:
                if (const auto material = LoadMaterial(file, &error))
                {
                    std::printf("  %s, queue %s, %zu properties\n", material->Name.c_str(), material->RenderQueue.c_str(), material->Properties.size());
                    for (const auto& property : material->Properties)
                        std::printf("    %s (%s) = %s%s\n", property.Name.c_str(), property.Type.c_str(), property.Value.c_str(),
                                    property.Texture.IsNil() ? "" : (" -> " + property.Texture.ToString()).c_str());
                }
                break;
            default:
                break;
        }
        if (!error.empty())
        {
            std::fprintf(stderr, "[sap] %s\n", error.c_str());
            return 1;
        }
        return 0;
    }
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() < 2)
        return Usage();
    const auto& command = args[0];
    if (command == "import")
    {
        bool force = false, quiet = false;
        for (size_t i = 2; i < args.size(); ++i)
        {
            if (args[i] == "--force")
                force = true;
            else if (args[i] == "--quiet")
                quiet = true;
            else
                return Usage();
        }
        return Import(args[1], force, quiet);
    }
    if (command == "list")
        return List(args[1]);
    if (command == "info")
        return Info(args[1]);
    return Usage();
}
