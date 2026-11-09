// SAssetPipeline tests: builds an asset root in a temporary folder (a generated PNG pair, an OBJ, a .dzmaterial),
// imports it and checks the cooked assets, the registry, incremental re-import, parameter changes, removal of assets
// whose source is gone, copied sidecars and corrupt files. Every check prints PASS / FAIL; exit code = failures.

#include "SAssetPipeline/Import/AssetImporter.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace SnowLeopardEngine::AssetPipeline;
namespace fs = std::filesystem;

namespace
{
    int g_Failures = 0;

    void Check(bool condition, const std::string& name, const std::string& detail = "")
    {
        std::printf("[%s] %-56s %s\n", condition ? "PASS" : "FAIL", name.c_str(), detail.c_str());
        g_Failures += condition ? 0 : 1;
    }

    void WriteText(const fs::path& file, const std::string& text)
    {
        fs::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    }

    // 4 x 4 black / white checker (pixel-sized squares).
    void WriteChecker(const fs::path& file, uint8_t white = 255)
    {
        fs::create_directories(file.parent_path());
        uint8_t pixels[4 * 4 * 4];
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
            {
                const uint8_t v = ((x + y) % 2) ? white : 0;
                uint8_t*      p = pixels + (y * 4 + x) * 4;
                p[0] = p[1] = p[2] = v;
                p[3]               = 255;
            }
        stbi_write_png(file.generic_string().c_str(), 4, 4, 4, pixels, 16);
    }

    ImportReport Import(const fs::path& root, bool force = false)
    {
        AssetImporter importer(root);
        ImportOptions options;
        options.Force = force;
        return importer.ImportAll(options);
    }

    std::string Counts(const ImportReport& r)
    {
        return std::to_string(r.Imported) + " imported, " + std::to_string(r.UpToDate) + " up to date, " + std::to_string(r.Removed) +
               " removed, " + std::to_string(r.Failed) + " failed";
    }
} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "sap_tests" / "Assets";
    fs::remove_all(root.parent_path());

    WriteChecker(root / "Textures" / "Checker.png");
    WriteChecker(root / "Textures" / "Checker_Roughness.png");
    // Uncompressed copies (sidecars written before the first import keep their settings): exact texels to check mip
    // filtering on.
    WriteChecker(root / "Raw" / "Colour.png");
    WriteChecker(root / "Raw" / "Data.png");
    WriteText(root / "Raw" / "Colour.png.simport",
              "[import]\nimporter=texture\nid=11111111-2222-4333-8444-555555555555\n\n[params]\ncompression=none\nsrgb=true\n");
    WriteText(root / "Raw" / "Data.png.simport",
              "[import]\nimporter=texture\nid=11111111-2222-4333-8444-666666666666\n\n[params]\ncompression=none\nsrgb=false\n");
    // Two objects: two sub-meshes, one quad each.
    WriteText(root / "Models" / "Quads.obj",
              "o First\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nvn 0 0 1\n"
              "f 1/1/1 2/2/1 3/3/1 4/4/1\n"
              "o Second\nv 0 0 2\nv 1 0 2\nv 1 1 2\nv 0 1 2\n"
              "f 5/1/1 6/2/1 7/3/1 8/4/1\n");
    // Paths in the app form ("Assets/..."), relative to the root, and to a missing texture.
    WriteText(root / "Materials" / "Checker.dzmaterial",
              R"({"DzMaterial": {"Name": "Checker", "RenderQueue": "AlphaTest", "TileSize": 2, "Properties": [
                   {"Name": "albedoMap", "Type": "Texture2D", "Value": "Assets/Textures/Checker.png"},
                   {"Name": "roughnessMap", "Type": "Texture2D", "Value": "Textures/Checker_Roughness.png"},
                   {"Name": "roughness", "Type": "Float", "Value": "0.4"},
                   {"Name": "aoMap", "Type": "Texture2D", "Value": "Textures/Missing.png"}]}})");
    // Not importable: no sidecar, not in the registry.
    WriteText(root / "Notes.txt", "hello");

    // ---- first import
    auto report = Import(root);
    Check(report.Imported == 6 && report.Failed == 0, "first import cooks 6 sources", Counts(report));
    Check(report.SidecarsCreated == 4 && fs::exists(root / "Textures" / "Checker.png.simport") && !fs::exists(root / "Notes.txt.simport"),
          "sources without one get a .simport sidecar", std::to_string(report.SidecarsCreated) + " created");

    AssetRegistry registry(root);
    const bool loaded = registry.Load();
    Check(loaded && registry.GetEntries().size() == 6, "registry lists the 6 assets", std::to_string(registry.GetEntries().size()));
    const auto* rawColour = registry.FindBySource("Raw/Colour.png");
    Check(rawColour && rawColour->Id.ToString() == "11111111-2222-4333-8444-555555555555", "an existing sidecar's id is kept");
    const auto* checker   = registry.FindBySource("Textures/Checker.png");
    const auto* roughness = registry.FindBySource("Assets\\Textures\\Checker_Roughness.png");
    const auto* quads     = registry.FindBySource("Models/Quads.obj");
    const auto* material  = registry.FindBySource("Assets/Materials/Checker.dzmaterial");
    Check(checker && roughness && quads && material, "lookup by source path (relative, \"Assets/...\", '\\')");
    if (!checker || !roughness || !quads || !material)
        return std::printf("%d failure(s)\n", g_Failures + 1), g_Failures + 1;

    const auto sidecar = ImportSettings::Load(ImportSettings::SidecarOf(root / "Textures" / "Checker.png"));
    Check(sidecar && sidecar->Id == checker->Id && sidecar->Importer == "texture", "sidecar holds the registry id");

    // ---- textures
    const auto albedo = LoadTexture(registry.GetCookedPath(*checker));
    const auto rough  = LoadTexture(registry.GetCookedPath(*roughness));
    Check(albedo && albedo->Format == TextureFormat::BC7Srgb && rough && rough->Format == TextureFormat::BC7,
          "BC7 by default; colour map is sRGB, \"Roughness\" map is linear");
    Check(albedo && albedo->Width == 4 && albedo->Mips.size() == 3 && albedo->Mips[0].size() == 16 && albedo->Mips[2].size() == 16,
          "full mip chain of BC7 blocks (4, 2, 1: one block each)", albedo ? std::to_string(albedo->Mips.size()) + " mips" : "");
    const auto colour = LoadTexture(registry.GetCookedPath(*rawColour));
    const auto linear = LoadTexture(registry.GetCookedPath(*registry.FindBySource("Raw/Data.png")));
    Check(colour && colour->Format == TextureFormat::RGBA8Srgb && linear && linear->Format == TextureFormat::RGBA8, "compression=none keeps RGBA8");
    const auto& albedo_ = colour;
    if (albedo_ && linear && albedo_->Mips.size() > 1 && linear->Mips.size() > 1)
    {
        // Half white: linear average 0.5 -> sRGB 188; averaging the stored bytes (the old loader) gives 128.
        const int srgbMip = albedo_->Mips[1][0], linearMip = linear->Mips[1][0];
        Check(std::abs(srgbMip - 188) <= 1 && std::abs(linearMip - 128) <= 1, "mips are filtered in linear space",
              "sRGB " + std::to_string(srgbMip) + ", linear " + std::to_string(linearMip));
    }

    // ---- mesh
    const auto mesh = LoadMesh(registry.GetCookedPath(*quads));
    Check(mesh && mesh->SubMeshes.size() == 2 && mesh->Vertices.size() == 8 && mesh->Indices.size() == 12,
          "OBJ: two sub-meshes, triangulated quads",
          mesh ? std::to_string(mesh->SubMeshes.size()) + " parts, " + std::to_string(mesh->Vertices.size()) + " vertices, " +
                     std::to_string(mesh->Indices.size()) + " indices"
               : "");
    if (mesh)
    {
        Check(std::abs(mesh->LocalBounds.Max[2] - 2.0f) < 1e-4f && std::abs(mesh->Vertices[0].Normal[2] - 1.0f) < 1e-4f,
              "bounds and normals come through");
        // flipUVs: v = 0 at the top.
        bool flipped = false;
        for (const auto& v : mesh->Vertices)
            flipped |= v.Position[1] == 0 && std::abs(v.TexCoord[1] - 1.0f) < 1e-4f;
        Check(flipped, "UVs are flipped (textures are stored top row first)");
    }

    // ---- material
    const auto cooked = LoadMaterial(registry.GetCookedPath(*material));
    Check(cooked && cooked->RenderQueue == "AlphaTest" && cooked->TileSize == 2 && cooked->Properties.size() == 4,
          "material fields and properties");
    if (cooked)
    {
        Check(cooked->Properties[0].Texture == checker->Id && cooked->Properties[1].Texture == roughness->Id &&
                  cooked->Properties[3].Texture.IsNil(),
              "texture paths resolve to texture ids; a missing one stays nil");
        Check(material->Dependencies.size() == 2, "material depends on its 2 textures", std::to_string(material->Dependencies.size()));
    }

    // ---- incremental
    report = Import(root);
    Check(report.Imported == 1 && report.UpToDate == 5, "second import: unchanged assets are up to date", Counts(report) + " (the material with a missing texture is retried)");

    WriteChecker(root / "Textures" / "Checker.png", 128);
    report = Import(root);
    registry.Load();
    Check(report.Imported == 2 && registry.FindBySource("Textures/Checker.png")->Id == sidecar->Id,
          "an edited texture is re-imported with the same id", Counts(report));

    auto settings            = *ImportSettings::Load(ImportSettings::SidecarOf(root / "Textures" / "Checker.png"));
    settings.Params["srgb"]        = "false";
    settings.Params["mips"]        = "false";
    settings.Params["compression"] = "none";
    settings.Save(ImportSettings::SidecarOf(root / "Textures" / "Checker.png"));
    report = Import(root);
    registry.Load();
    const auto changed = LoadTexture(registry.GetCookedPath(*registry.FindBySource("Textures/Checker.png")));
    Check(changed && changed->Format == TextureFormat::RGBA8 && changed->Mips.size() == 1, "changed parameters re-import the asset", Counts(report));

    // ---- copies, removal, corrupt files
    fs::copy_file(root / "Textures" / "Checker.png", root / "Textures" / "Copy.png");
    fs::copy_file(root / "Textures" / "Checker.png.simport", root / "Textures" / "Copy.png.simport");
    report = Import(root);
    registry.Load();
    const auto* copy = registry.FindBySource("Textures/Copy.png");
    Check(copy && copy->Id != sidecar->Id && registry.FindBySource("Textures/Checker.png")->Id == sidecar->Id,
          "a copied sidecar gets a new id, the original keeps its own");

    const auto cookedQuads = registry.GetCookedPath(*registry.FindBySource("Models/Quads.obj"));
    fs::remove(root / "Models" / "Quads.obj");
    report = Import(root);
    registry.Load();
    Check(report.Removed == 1 && !registry.FindBySource("Models/Quads.obj") && !fs::exists(cookedQuads),
          "a deleted source removes its asset and cooked file", Counts(report));

    const auto path = registry.GetCookedPath(*registry.FindBySource("Textures/Copy.png"));
    fs::resize_file(path, fs::file_size(path) - 7);
    std::string error;
    Check(!LoadTexture(path, &error) && !error.empty(), "a truncated cooked file is rejected", error);
    Check(!LoadMesh(registry.GetCookedPath(*registry.FindBySource("Textures/Checker.png")), &error), "loading the wrong type is rejected", error);

    report = Import(root, true);
    Check(report.Imported == 6 && report.Failed == 0, "--force re-imports everything", Counts(report));

    fs::remove_all(root.parent_path());
    std::printf("%d failure(s)\n", g_Failures);
    return g_Failures;
}
