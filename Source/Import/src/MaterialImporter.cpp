#include "Importers.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace SnowLeopardEngine::AssetPipeline::Detail
{
    void MaterialDefaults(const std::filesystem::path&, std::map<std::string, std::string>&) {}

    // .dzmaterial (cereal JSON): {"DzMaterial": {"Name", "RenderQueue", "Properties": [{"Name", "Type", "Value"}],
    // "TextureFlipY", "TileSize", "EnableInstancing"}}. Texture paths become texture asset ids (dependencies).
    bool ImportMaterial(ImportContext& context, MaterialAsset& asset)
    {
        std::ifstream in(context.Source);
        if (!in)
        {
            context.Error = "cannot open the file";
            return false;
        }
        nlohmann::json json;
        try
        {
            json = nlohmann::json::parse(in);
        }
        catch (const std::exception& e)
        {
            context.Error = std::string("invalid JSON: ") + e.what();
            return false;
        }
        const auto& material = json.contains("DzMaterial") ? json["DzMaterial"] : json;
        if (!material.is_object())
        {
            context.Error = "no DzMaterial object";
            return false;
        }

        asset.Name             = material.value("Name", context.Source.stem().generic_string());
        asset.RenderQueue      = material.value("RenderQueue", std::string("Opaque"));
        asset.TextureFlipY     = material.value("TextureFlipY", false);
        asset.TileSize         = material.value("TileSize", 1);
        asset.EnableInstancing = material.value("EnableInstancing", false);

        for (const auto& item : material.value("Properties", nlohmann::json::array()))
        {
            MaterialProperty property;
            property.Name  = item.value("Name", std::string());
            property.Type  = item.value("Type", std::string());
            property.Value = item.value("Value", std::string());
            if (property.Type == "Texture2D" && !property.Value.empty())
            {
                // Texture paths are written from the working directory of the apps ("Assets/Textures/X.png"), relative
                // to the asset root, or relative to the material: try each.
                const std::filesystem::path value(property.Value);
                std::vector<std::filesystem::path> candidates {context.Source.parent_path() / value, context.Root / value,
                                                               context.Root.parent_path() / value};
                std::optional<AssetId> texture;
                for (const auto& candidate : candidates)
                {
                    if (std::filesystem::exists(candidate) && (texture = context.ResolveId(std::filesystem::absolute(candidate).lexically_normal())))
                        break;
                }
                if (texture)
                {
                    property.Texture = *texture;
                    context.Dependencies.push_back(*texture);
                }
                else
                {
                    context.Warnings.push_back("texture " + property.Value + " (" + property.Name + ") is not an asset of this root");
                }
            }
            asset.Properties.push_back(std::move(property));
        }
        return true;
    }
} // namespace SnowLeopardEngine::AssetPipeline::Detail
