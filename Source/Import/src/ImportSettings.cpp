#include "SAssetPipeline/Import/ImportSettings.h"

#include <charconv>
#include <fstream>
#include <sstream>

namespace SnowLeopardEngine::AssetPipeline
{
    namespace
    {
        std::string Trim(std::string text)
        {
            const auto first = text.find_first_not_of(" \t\r\n");
            const auto last  = text.find_last_not_of(" \t\r\n");
            return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
        }

        void SetError(std::string* error, std::string message)
        {
            if (error != nullptr)
                *error = std::move(message);
        }
    } // namespace

    std::optional<ImportSettings> ImportSettings::Load(const std::filesystem::path& sidecar, std::string* error)
    {
        std::ifstream in(sidecar);
        if (!in)
        {
            SetError(error, "cannot open " + sidecar.generic_string());
            return std::nullopt;
        }
        ImportSettings settings;
        std::string    section, line;
        int            number = 0;
        while (std::getline(in, line))
        {
            ++number;
            line = Trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;
            if (line.front() == '[' && line.back() == ']')
            {
                section = line.substr(1, line.size() - 2);
                continue;
            }
            const auto equals = line.find('=');
            if (equals == std::string::npos)
            {
                SetError(error, sidecar.generic_string() + ":" + std::to_string(number) + ": expected key=value");
                return std::nullopt;
            }
            const auto key   = Trim(line.substr(0, equals));
            const auto value = Trim(line.substr(equals + 1));
            if (section == "import")
            {
                if (key == "version")
                    settings.Version = std::atoi(value.c_str());
                else if (key == "importer")
                    settings.Importer = value;
                else if (key == "id")
                {
                    const auto id = AssetId::Parse(value);
                    if (!id)
                    {
                        SetError(error, sidecar.generic_string() + ":" + std::to_string(number) + ": invalid id");
                        return std::nullopt;
                    }
                    settings.Id = *id;
                }
            }
            else if (section == "params")
            {
                settings.Params[key] = value;
            }
        }
        if (settings.Importer.empty() || settings.Id.IsNil())
        {
            SetError(error, sidecar.generic_string() + ": [import] needs importer and id");
            return std::nullopt;
        }
        return settings;
    }

    bool ImportSettings::Save(const std::filesystem::path& sidecar, std::string* error) const
    {
        std::ostringstream text;
        text << "[import]\nversion=" << Version << "\nimporter=" << Importer << "\nid=" << Id.ToString() << "\n";
        if (!Params.empty())
        {
            text << "\n[params]\n";
            for (const auto& [key, value] : Params)
                text << key << "=" << value << "\n";
        }
        std::ofstream out(sidecar, std::ios::binary | std::ios::trunc); // '\n' on every platform
        if (!out)
        {
            SetError(error, "cannot write " + sidecar.generic_string());
            return false;
        }
        out << text.str();
        return bool(out);
    }

    std::string ImportSettings::GetString(const std::string& key, const std::string& fallback) const
    {
        const auto found = Params.find(key);
        return found != Params.end() ? found->second : fallback;
    }

    bool ImportSettings::GetBool(const std::string& key, bool fallback) const
    {
        const auto found = Params.find(key);
        if (found == Params.end())
            return fallback;
        return found->second == "true" || found->second == "1" || found->second == "yes";
    }

    float ImportSettings::GetFloat(const std::string& key, float fallback) const
    {
        const auto found = Params.find(key);
        if (found == Params.end())
            return fallback;
        try
        {
            return std::stof(found->second);
        }
        catch (...)
        {
            return fallback;
        }
    }

    int ImportSettings::GetInt(const std::string& key, int fallback) const
    {
        const auto found = Params.find(key);
        int        value = fallback;
        if (found != Params.end())
            std::from_chars(found->second.data(), found->second.data() + found->second.size(), value);
        return value;
    }
} // namespace SnowLeopardEngine::AssetPipeline
