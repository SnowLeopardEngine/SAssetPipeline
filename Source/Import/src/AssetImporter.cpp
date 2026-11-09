#include "SAssetPipeline/Import/AssetImporter.h"
#include "Importers.h"

#include <nlohmann/json.hpp>
#include <xxhash.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace SnowLeopardEngine::AssetPipeline
{
    using namespace Detail;

    namespace Detail
    {
        namespace
        {
            const ImporterInfo kImporters[] = {
                {"texture", AssetType::Texture, 3, &TextureDefaults},
                {"mesh", AssetType::Mesh, 2, &MeshDefaults},
                {"material", AssetType::Material, 2, &MaterialDefaults},
            };
        } // namespace

        const ImporterInfo* FindImporter(const std::string& name)
        {
            for (const auto& importer : kImporters)
            {
                if (name == importer.Name)
                    return &importer;
            }
            return nullptr;
        }

        const ImporterInfo* FindImporterForSource(const std::filesystem::path& source)
        {
            std::string extension = source.extension().generic_string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            for (const char* e : {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr"})
            {
                if (extension == e)
                    return FindImporter("texture");
            }
            for (const char* e : {".obj", ".fbx", ".gltf", ".glb", ".dae"})
            {
                if (extension == e)
                    return FindImporter("mesh");
            }
            if (extension == ".dzmaterial")
                return FindImporter("material");
            return nullptr;
        }
    } // namespace Detail

    namespace
    {
        constexpr int kDatabaseVersion = 1;

        // What the last import of a source was made from.
        struct Record
        {
            std::string Id;
            std::string Importer;
            uint32_t    ImporterVersion = 0;
            uint64_t    SourceHash      = 0;
            uint64_t    ParamsHash      = 0;
            uint32_t    Unresolved      = 0; // references that were not assets yet (re-import until they are)
        };

        class ImportDatabase
        {
        public:
            explicit ImportDatabase(std::filesystem::path file) : m_File(std::move(file)) {}

            void Load()
            {
                m_Records.clear();
                std::ifstream in(m_File);
                if (!in)
                    return;
                try
                {
                    const auto json = nlohmann::json::parse(in);
                    if (json.value("version", 0) != kDatabaseVersion)
                        return; // an old layout: everything is imported again
                    for (const auto& [source, item] : json.at("records").items())
                    {
                        Record record;
                        record.Id              = item.value("id", std::string());
                        record.Importer        = item.value("importer", std::string());
                        record.ImporterVersion = item.value("importerVersion", 0u);
                        record.SourceHash      = item.value("sourceHash", uint64_t(0));
                        record.ParamsHash      = item.value("paramsHash", uint64_t(0));
                        record.Unresolved      = item.value("unresolved", 0u);
                        m_Records[source]      = record;
                    }
                }
                catch (...)
                {
                    m_Records.clear(); // corrupt: start over
                }
            }

            void Save() const
            {
                nlohmann::json records = nlohmann::json::object();
                for (const auto& [source, record] : m_Records)
                {
                    records[source] = {{"id", record.Id},
                                       {"importer", record.Importer},
                                       {"importerVersion", record.ImporterVersion},
                                       {"sourceHash", record.SourceHash},
                                       {"paramsHash", record.ParamsHash},
                                       {"unresolved", record.Unresolved}};
                }
                std::error_code code;
                std::filesystem::create_directories(m_File.parent_path(), code);
                std::ofstream out(m_File, std::ios::trunc);
                out << nlohmann::json {{"version", kDatabaseVersion}, {"records", records}}.dump(2) << '\n';
            }

            const Record* Find(const std::string& source) const
            {
                const auto found = m_Records.find(source);
                return found != m_Records.end() ? &found->second : nullptr;
            }
            void Set(const std::string& source, Record record) { m_Records[source] = std::move(record); }
            void Erase(const std::string& source) { m_Records.erase(source); }

        private:
            std::filesystem::path                   m_File;
            std::unordered_map<std::string, Record> m_Records;
        };

        uint64_t HashFile(const std::filesystem::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            if (!in)
                return 0;
            XXH3_state_t* state = XXH3_createState();
            XXH3_64bits_reset(state);
            std::vector<char> buffer(1 << 16);
            while (in)
            {
                in.read(buffer.data(), std::streamsize(buffer.size()));
                XXH3_64bits_update(state, buffer.data(), size_t(in.gcount()));
            }
            const uint64_t hash = XXH3_64bits_digest(state);
            XXH3_freeState(state);
            return hash;
        }

        uint64_t HashParams(const ImportSettings& settings)
        {
            std::string text = settings.Importer;
            for (const auto& [key, value] : settings.Params)
                text += '\n' + key + '=' + value;
            return XXH3_64bits(text.data(), text.size());
        }

        bool IsHidden(const std::filesystem::path& relative)
        {
            for (const auto& part : relative)
            {
                const auto name = part.generic_string();
                if (!name.empty() && name[0] == '.' && name != "." && name != "..")
                    return true;
            }
            return false;
        }

        void Log(const ImportOptions& options, const std::string& message)
        {
            if (options.Log)
                options.Log(message);
        }

        // Sources of one import run: absolute path -> settings (ids known before anything is imported, so a material
        // can refer to a texture imported after it).
        class Session
        {
        public:
            Session(const std::filesystem::path& root, AssetRegistry& registry, const ImportOptions& options) :
                m_Root(root), m_Registry(registry), m_Options(options), m_Database(root / kImportedFolder / "import-db.json")
            {
                std::string error;
                if (!m_Registry.Load(&error))
                {
                    Log(m_Options, "registry unreadable, rebuilding it: " + error);
                    m_Registry = AssetRegistry(m_Root);
                }
                m_Database.Load();
            }

            // The settings of a source, creating its sidecar on first sight. Null: not an importable file.
            const ImportSettings* Settings(const std::filesystem::path& source, ImportReport& report)
            {
                const auto key = source.generic_string();
                if (const auto found = m_Settings.find(key); found != m_Settings.end())
                    return &found->second;
                const auto* importer = FindImporterForSource(source);
                if (importer == nullptr)
                    return nullptr;

                const auto                    sidecar = ImportSettings::SidecarOf(source);
                std::optional<ImportSettings> settings;
                if (std::filesystem::exists(sidecar))
                {
                    std::string error;
                    settings = ImportSettings::Load(sidecar, &error);
                    if (!settings)
                        Log(m_Options, "warning: " + error + " (a new id is assigned)");
                }
                bool write = false;
                if (!settings)
                {
                    settings           = ImportSettings {};
                    settings->Importer = importer->Name;
                    settings->Id       = AssetId::Generate();
                    importer->DefaultParams(source, settings->Params);
                    write = true;
                    ++report.SidecarsCreated;
                }
                // A copied file comes with its original's sidecar: the copy gets a new id.
                if (const auto owner = m_IdOwners.find(settings->Id); owner != m_IdOwners.end() && owner->second != key)
                {
                    Log(m_Options, "warning: " + Relative(source) + " had the id of " + Relative(owner->second) + "; it gets a new one");
                    settings->Id = AssetId::Generate();
                    write        = true;
                }
                if (write)
                {
                    std::string error;
                    if (!settings->Save(sidecar, &error))
                        Log(m_Options, "warning: " + error);
                }
                m_IdOwners[settings->Id] = key;
                return &(m_Settings[key] = std::move(*settings));
            }

            bool Import(const std::filesystem::path& source, ImportReport& report)
            {
                const auto* settings = Settings(source, report);
                if (settings == nullptr)
                    return true;
                const auto  relative = Relative(source);
                const auto* importer = FindImporter(settings->Importer);
                if (importer == nullptr)
                    return Fail(report, relative, "unknown importer '" + settings->Importer + "'");

                Record record;
                record.Id              = settings->Id.ToString();
                record.Importer        = importer->Name;
                record.ImporterVersion = importer->Version;
                record.SourceHash      = HashFile(source);
                record.ParamsHash      = HashParams(*settings);

                RegistryEntry entry;
                entry.Id     = settings->Id;
                entry.Type   = importer->Type;
                entry.Source = relative;
                entry.Cooked = std::string(kImportedFolder) + "/" + settings->Id.ToString() + CookedExtension(importer->Type);

                const auto* previous = m_Database.Find(relative);
                const auto* existing = m_Registry.Find(settings->Id);
                if (!m_Options.Force && previous != nullptr && existing != nullptr && previous->Id == record.Id &&
                    previous->Importer == record.Importer && previous->ImporterVersion == record.ImporterVersion &&
                    previous->SourceHash == record.SourceHash && previous->ParamsHash == record.ParamsHash &&
                    previous->Unresolved == 0 && std::filesystem::exists(m_Root / entry.Cooked))
                {
                    ++report.UpToDate;
                    return true;
                }

                ImportContext context;
                context.Root      = m_Root;
                context.Source    = source;
                context.Settings  = settings;
                context.ResolveId = [this, &report](const std::filesystem::path& other) -> std::optional<AssetId> {
                    const auto* otherSettings = Settings(other, report);
                    return otherSettings != nullptr ? std::optional<AssetId>(otherSettings->Id) : std::nullopt;
                };

                std::string error;
                bool        ok = false;
                switch (importer->Type)
                {
                    case AssetType::Texture: {
                        TextureAsset asset;
                        ok = ImportTexture(context, asset) && SaveTexture(m_Root / entry.Cooked, entry.Id, asset, &error);
                        break;
                    }
                    case AssetType::Mesh: {
                        MeshAsset asset;
                        ok = ImportMesh(context, asset) && SaveMesh(m_Root / entry.Cooked, entry.Id, asset, &error);
                        break;
                    }
                    case AssetType::Material: {
                        MaterialAsset asset;
                        ok = ImportMaterial(context, asset) && SaveMaterial(m_Root / entry.Cooked, entry.Id, asset, &error);
                        for (const auto& property : asset.Properties)
                            record.Unresolved += property.Type == "Texture2D" && !property.Value.empty() && property.Texture.IsNil();
                        break;
                    }
                    default:
                        break;
                }
                for (const auto& warning : context.Warnings)
                    Log(m_Options, "warning: " + relative + ": " + warning);
                if (!ok)
                    return Fail(report, relative, !context.Error.empty() ? context.Error : error);

                // The id moved to a new source (a renamed file keeps its sidecar): drop the old source's record.
                if (existing != nullptr && existing->Source != relative)
                    m_Database.Erase(existing->Source);
                entry.Dependencies = std::move(context.Dependencies);
                m_Registry.Upsert(std::move(entry));
                m_Database.Set(relative, std::move(record));
                ++report.Imported;
                Log(m_Options, "imported " + relative);
                return true;
            }

            // Assets whose source no longer exists (or is no longer imported).
            void RemoveMissing(ImportReport& report)
            {
                std::vector<RegistryEntry> gone;
                for (const auto& [id, entry] : m_Registry.GetEntries())
                {
                    const auto source = (m_Root / entry.Source).lexically_normal();
                    if (!std::filesystem::exists(source) || FindImporterForSource(source) == nullptr)
                        gone.push_back(entry);
                }
                for (const auto& entry : gone)
                {
                    std::error_code code;
                    std::filesystem::remove(m_Root / entry.Cooked, code);
                    m_Database.Erase(entry.Source);
                    m_Registry.Remove(entry.Id);
                    ++report.Removed;
                    Log(m_Options, "removed " + entry.Source + " (source is gone)");
                }
            }

            void Save(ImportReport& report)
            {
                std::string error;
                if (!m_Registry.Save(&error))
                    Fail(report, "registry", error);
                m_Database.Save();
            }

            std::string Relative(const std::filesystem::path& source) const { return source.lexically_relative(m_Root).generic_string(); }

        private:
            bool Fail(ImportReport& report, const std::string& source, const std::string& message)
            {
                ++report.Failed;
                report.Errors.push_back(source + ": " + message);
                Log(m_Options, "error: " + source + ": " + message);
                return false;
            }

            std::filesystem::path                             m_Root;
            AssetRegistry&                                    m_Registry;
            const ImportOptions&                              m_Options;
            ImportDatabase                                    m_Database;
            std::unordered_map<std::string, ImportSettings>   m_Settings;
            std::unordered_map<AssetId, std::string>          m_IdOwners;
        };
    } // namespace

    AssetImporter::AssetImporter(std::filesystem::path root) :
        m_Root(std::filesystem::absolute(root).lexically_normal()), m_Registry(m_Root)
    {
        // "Assets/" and "Assets" are the same root.
        if (m_Root.filename().empty())
            m_Root = m_Root.parent_path();
        m_Registry = AssetRegistry(m_Root);
    }

    std::string AssetImporter::ImporterFor(const std::filesystem::path& source)
    {
        const auto* importer = FindImporterForSource(source);
        return importer != nullptr ? importer->Name : std::string();
    }

    ImportReport AssetImporter::ImportAll(const ImportOptions& options)
    {
        ImportReport report;
        if (!std::filesystem::is_directory(m_Root))
        {
            ++report.Failed;
            report.Errors.push_back(m_Root.generic_string() + " is not a directory");
            return report;
        }
        Session session(m_Root, m_Registry, options);

        // Sorted: the same order (and the same log) on every machine.
        std::vector<std::filesystem::path> sources;
        for (auto it = std::filesystem::recursive_directory_iterator(m_Root); it != std::filesystem::recursive_directory_iterator(); ++it)
        {
            const auto relative = it->path().lexically_relative(m_Root);
            if (IsHidden(relative))
            {
                if (it->is_directory())
                    it.disable_recursion_pending();
                continue;
            }
            if (it->is_regular_file() && FindImporterForSource(it->path()) != nullptr)
                sources.push_back(it->path().lexically_normal());
        }
        std::sort(sources.begin(), sources.end());

        // Ids first (sidecars), so references between assets resolve whatever the order.
        for (const auto& source : sources)
            session.Settings(source, report);
        for (const auto& source : sources)
            session.Import(source, report);
        session.RemoveMissing(report);
        session.Save(report);
        return report;
    }

    bool AssetImporter::Import(const std::filesystem::path& source, const ImportOptions& options, std::string* error)
    {
        const auto absolute = (source.is_absolute() ? source : m_Root / source).lexically_normal();
        ImportReport report;
        Session      session(m_Root, m_Registry, options);
        if (FindImporterForSource(absolute) == nullptr)
        {
            if (error != nullptr)
                *error = absolute.generic_string() + " is not an importable file";
            return false;
        }
        session.Import(absolute, report);
        session.Save(report);
        if (!report.Ok() && error != nullptr)
            *error = report.Errors.empty() ? "import failed" : report.Errors.front();
        return report.Ok();
    }
} // namespace SnowLeopardEngine::AssetPipeline
