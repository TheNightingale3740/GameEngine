// Engine/Project.cpp

#include "Engine/Project.h"

#include <algorithm>
#include <system_error>

#include "Core/FileSystem.h"
#include "Core/Logging/Log.h"

namespace Ember
{
    namespace
    {
        /// Copies one file, creating the destination's directory if needed.
        [[nodiscard]] Result<void> CopyFile(const FilePath& from, const FilePath& to)
        {
            Result<std::vector<std::uint8_t>> bytes = FileSystem::ReadBinaryFile(from);
            if (bytes.IsFailure())
            {
                return bytes.GetError();
            }

            return FileSystem::WriteBinaryFile(to, bytes.Value().data(), bytes.Value().size());
        }

        /// Writes a JSON document with a trailing newline, as a text file.
        [[nodiscard]] Result<void> WriteJson(const FilePath& path, const JsonValue& document)
        {
            return FileSystem::WriteTextFile(path, document.ToString() + "\n");
        }

        /// Reads a vector of strings from a JSON array member.
        [[nodiscard]] std::vector<std::string> ReadStringArray(const JsonValue& object, const char* key)
        {
            std::vector<std::string> values;

            const JsonValue* array = object.Find(key);
            if (array == nullptr || !array->IsArray())
            {
                return values;
            }

            for (const JsonValue& entry : array->AsArray())
            {
                if (entry.IsString())
                {
                    values.push_back(entry.AsString());
                }
            }

            return values;
        }
    }

    ProjectSettings ProjectSettings::Defaults()
    {
        ProjectSettings settings;
        settings.Name = "Untitled";
        settings.StartScene = "main.ember";

        // The default layer set is what a new project needs to get going: static
        // geometry that everything stands on, and a player that stands on it.
        settings.CollisionLayers = {
            {"Default", {"Default"}},
            {"Player", {"Default", "Player"}},
            {"Enemy", {"Default", "Enemy"}},
        };

        return settings;
    }

    FilePath Project::GetProjectFilePath() const
    {
        return m_Directory / ProjectFileName;
    }

    FilePath Project::GetManifestPath(const FilePath& buildDirectory) const
    {
        return buildDirectory / ManifestFileName;
    }

    Result<ProjectSettings> Project::ReadSettings(const FilePath& path) const
    {
        Result<std::string> text = FileSystem::ReadTextFile(path);
        if (text.IsFailure())
        {
            return text.GetError();
        }

        Result<JsonValue> document = Json::Parse(text.Value());
        if (document.IsFailure())
        {
            return Error(ErrorCode::ParseError,
                         "The project file is not valid JSON: " + document.GetError().Message);
        }

        const JsonValue& root = document.Value();
        if (!root.IsObject())
        {
            return Error(ErrorCode::ParseError, "A project file must be a JSON object");
        }

        const JsonValue* version = root.Find(ProjectFormat::VersionKey);
        if (version == nullptr || !version->IsNumber())
        {
            return Error(ErrorCode::ParseError, "A project file must declare its format version");
        }

        if (version->AsInt() != ProjectFormat::Version)
        {
            return Error(ErrorCode::NotSupported,
                         "Project format version " + std::to_string(version->AsInt()) +
                             " is not supported by this engine");
        }

        ProjectSettings settings;

        if (const JsonValue* name = root.Find(ProjectFormat::NameKey); name != nullptr && name->IsString())
        {
            settings.Name = name->AsString();
        }

        if (const JsonValue* description = root.Find(ProjectFormat::DescriptionKey);
            description != nullptr && description->IsString())
        {
            settings.Description = description->AsString();
        }

        if (const JsonValue* startScene = root.Find(ProjectFormat::StartSceneKey);
            startScene != nullptr && startScene->IsString())
        {
            settings.StartScene = startScene->AsString();
        }

        if (const JsonValue* physics = root.Find(ProjectFormat::PhysicsKey); physics != nullptr)
        {
            if (const JsonValue* timeStep = physics->Find(ProjectFormat::PhysicsTimeStepKey);
                timeStep != nullptr)
            {
                settings.PhysicsTimeStep = timeStep->AsFloat(settings.PhysicsTimeStep);
            }

            if (const JsonValue* gravity = physics->Find(ProjectFormat::GravityKey); gravity != nullptr)
            {
                settings.Gravity = gravity->AsVec3(settings.Gravity);
            }
        }

        settings.CollisionLayers.clear();

        const JsonValue* layers = root.Find(ProjectFormat::LayersKey);
        if (layers != nullptr && layers->IsArray())
        {
            for (const JsonValue& entry : layers->AsArray())
            {
                ProjectSettings::Layer layer;
                layer.Name = entry[ProjectFormat::LayerNameKey].AsString();

                if (layer.Name.empty())
                {
                    return Error(ErrorCode::ParseError, "Every collision layer must have a name");
                }

                layer.CollidesWith = ReadStringArray(entry, ProjectFormat::LayerCollidesWithKey);
                settings.CollisionLayers.push_back(std::move(layer));
            }
        }

        return settings;
    }

    Result<void> Project::WriteSettings(const FilePath& path, const ProjectSettings& settings) const
    {
        JsonValue layers(JsonValue::Array{});
        for (const ProjectSettings::Layer& layer : settings.CollisionLayers)
        {
            JsonValue entry(JsonValue::Object{});
            entry.Set(ProjectFormat::LayerNameKey, JsonValue(layer.Name));

            JsonValue collidesWith(JsonValue::Array{});
            for (const std::string& other : layer.CollidesWith)
            {
                collidesWith.Push(JsonValue(other));
            }

            entry.Set(ProjectFormat::LayerCollidesWithKey, std::move(collidesWith));
            layers.Push(std::move(entry));
        }

        JsonValue physics(JsonValue::Object{});
        physics.Set(ProjectFormat::PhysicsTimeStepKey,
                    JsonValue(static_cast<double>(settings.PhysicsTimeStep)));
        physics.Set(ProjectFormat::GravityKey,
                    JsonValue(JsonValue::Array{
                        JsonValue(static_cast<double>(settings.Gravity.x)),
                        JsonValue(static_cast<double>(settings.Gravity.y)),
                        JsonValue(static_cast<double>(settings.Gravity.z))}));

        JsonValue document(JsonValue::Object{});
        document.Set(ProjectFormat::VersionKey, JsonValue(static_cast<double>(ProjectFormat::Version)));
        document.Set(ProjectFormat::NameKey, JsonValue(settings.Name));

        if (!settings.Description.empty())
        {
            document.Set(ProjectFormat::DescriptionKey, JsonValue(settings.Description));
        }

        document.Set(ProjectFormat::StartSceneKey, JsonValue(settings.StartScene));
        document.Set(ProjectFormat::PhysicsKey, std::move(physics));
        document.Set(ProjectFormat::LayersKey, std::move(layers));

        return WriteJson(path, document);
    }

    Result<Project> Project::Open(const FilePath& directory)
    {
        Project project;

        const FilePath projectFile = directory / ProjectFileName;
        if (!FileSystem::Exists(projectFile))
        {
            return Error(ErrorCode::FileNotFound,
                         "No " + std::string(ProjectFileName) + " in '" + FileSystem::ToString(directory) + "'");
        }

        Result<ProjectSettings> settings = project.ReadSettings(projectFile);
        if (settings.IsFailure())
        {
            return settings.GetError();
        }

        project.m_Settings = settings.Value();
        project.m_Directory = directory;
        project.m_ScenesDirectory = directory / "scenes";
        project.m_PrefabsDirectory = directory / "prefabs";
        project.m_ScriptsDirectory = directory / "scripts";
        project.m_AssetsDirectory = directory / "assets";
        project.m_BuildDirectory = directory / "build";
        project.m_Valid = true;

        return project;
    }

    Result<Project> Project::Create(const FilePath& directory, std::string name)
    {
        const FilePath projectFile = directory / ProjectFileName;

        if (FileSystem::Exists(projectFile))
        {
            return Error(ErrorCode::AlreadyExists,
                         "'" + FileSystem::ToString(directory) + "' already holds a project");
        }

        Project project;
        project.m_Settings = ProjectSettings::Defaults();

        if (!name.empty())
        {
            project.m_Settings.Name = std::move(name);
        }

        project.m_Directory = directory;
        project.m_ScenesDirectory = directory / "scenes";
        project.m_PrefabsDirectory = directory / "prefabs";
        project.m_ScriptsDirectory = directory / "scripts";
        project.m_AssetsDirectory = directory / "assets";
        project.m_BuildDirectory = directory / "build";

        for (const FilePath& subdirectory :
             {project.m_ScenesDirectory, project.m_PrefabsDirectory, project.m_ScriptsDirectory,
              project.m_AssetsDirectory, project.m_BuildDirectory})
        {
            if (Result<void> created = FileSystem::CreateDirectories(subdirectory); created.IsFailure())
            {
                return created.GetError();
            }
        }

        if (Result<void> written = project.WriteSettings(projectFile, project.m_Settings); written.IsFailure())
        {
            return written.GetError();
        }

        project.m_Valid = true;

        EMBER_LOG_INFO("Created project '{}' at {}", project.m_Settings.Name, FileSystem::ToString(directory));
        return project;
    }

    Result<FilePath> Project::Resolve(std::string_view relativePath) const
    {
        if (relativePath.empty())
        {
            return {ErrorCode::InvalidArgument, "A path cannot be empty"};
        }

        const FilePath resolved = m_Directory / FilePath(relativePath).lexically_normal();

        // A project file is data. A path that climbs out of the project would let
        // a scene reference any file on the machine, so it is refused rather than
        // normalised away.
        const std::string relative = FileSystem::ToString(resolved.lexically_relative(m_Directory));
        if (relative.rfind("..", 0) == 0)
        {
            return Error(ErrorCode::InvalidArgument,
                         "Path '" + std::string(relativePath) + "' leaves the project directory");
        }

        return resolved;
    }

    Result<void> Project::SetSettings(const ProjectSettings& settings)
    {
        // The layers are checked against each other before they are written, so a
        // project file cannot describe a collision layer that collides with one
        // that does not exist.
        for (const ProjectSettings::Layer& layer : settings.CollisionLayers)
        {
            for (const std::string& other : layer.CollidesWith)
            {
                const bool exists = std::any_of(
                    settings.CollisionLayers.begin(), settings.CollisionLayers.end(),
                    [&other](const ProjectSettings::Layer& candidate) { return candidate.Name == other; });

                if (!exists)
                {
                    return Error(ErrorCode::NotFound,
                                 "Collision layer '" + layer.Name + "' refers to '" + other +
                                     "', which is not declared");
                }
            }
        }

        if (Result<void> written = WriteSettings(GetProjectFilePath(), settings); written.IsFailure())
        {
            return written;
        }

        m_Settings = settings;
        return {};
    }

    Result<FilePath> Project::GetStartScenePath() const
    {
        if (m_Settings.StartScene.empty())
        {
            return Error(ErrorCode::InvalidArgument, "The project names no start scene");
        }

        return Resolve(m_Settings.StartScene);
    }

    Result<void> Project::Export(const FilePath& buildDirectory) const
    {
        if (!m_Valid)
        {
            return {ErrorCode::InvalidArgument, "The project was not opened"};
        }

        // The previous export is replaced rather than merged. A file left behind
        // from it would ship, and nothing would say so.
        std::error_code error;
        std::filesystem::remove_all(buildDirectory, error);

        if (Result<void> created = FileSystem::CreateDirectories(buildDirectory); created.IsFailure())
        {
            return created;
        }

        JsonValue scenes(JsonValue::Array{});
        JsonValue prefabs(JsonValue::Array{});
        JsonValue scripts(JsonValue::Array{});

        const auto collect = [](const FilePath& directory, JsonValue& into)
            -> Result<std::vector<FilePath>>
        {
            Result<std::vector<FilePath>> files = FileSystem::ListFiles(directory);
            if (files.IsFailure())
            {
                // A project need not have prefabs or scripts yet; only the start
                // scene is actually required.
                return std::vector<FilePath>{};
            }

            for (const FilePath& file : files.Value())
            {
                into.Push(JsonValue(file.filename().string()));
            }

            return files;
        };

        Result<std::vector<FilePath>> sceneFiles = collect(m_ScenesDirectory, scenes);
        if (sceneFiles.IsFailure())
        {
            return sceneFiles.GetError();
        }

        Result<std::vector<FilePath>> prefabFiles = collect(m_PrefabsDirectory, prefabs);
        if (prefabFiles.IsFailure())
        {
            return prefabFiles.GetError();
        }

        Result<std::vector<FilePath>> scriptFiles = collect(m_ScriptsDirectory, scripts);
        if (scriptFiles.IsFailure())
        {
            return scriptFiles.GetError();
        }

        for (const Result<std::vector<FilePath>>* directory :
             {&sceneFiles, &prefabFiles, &scriptFiles})
        {
            for (const FilePath& file : directory->Value())
            {
                const FilePath from = file.parent_path();
                if (Result<void> copied = CopyFile(file, buildDirectory / from.filename() / file.filename());
                    copied.IsFailure())
                {
                    return copied;
                }
            }
        }

        JsonValue manifest(JsonValue::Object{});
        manifest.Set(ProjectFormat::ManifestVersionKey,
                     JsonValue(static_cast<double>(ProjectFormat::ManifestVersion)));
        manifest.Set(ProjectFormat::ManifestProjectKey, JsonValue(m_Settings.Name));
        manifest.Set(ProjectFormat::ManifestStartSceneKey, JsonValue(m_Settings.StartScene));
        manifest.Set(ProjectFormat::ManifestScenesKey, std::move(scenes));
        manifest.Set(ProjectFormat::ManifestPrefabsKey, std::move(prefabs));
        manifest.Set(ProjectFormat::ManifestScriptsKey, std::move(scripts));
        manifest.Set(ProjectFormat::ManifestAssetRootKey, JsonValue("assets"));

        if (Result<void> written = WriteJson(GetManifestPath(buildDirectory), manifest); written.IsFailure())
        {
            return written;
        }

        EMBER_LOG_INFO("Exported {} scene(s), {} prefab(s) and {} script(s) to {}",
                       sceneFiles.Value().size(), prefabFiles.Value().size(), scriptFiles.Value().size(),
                       FileSystem::ToString(buildDirectory));

        return {};
    }

    Result<JsonValue> Project::ReadManifest(const FilePath& buildDirectory)
    {
        Result<std::string> text = FileSystem::ReadTextFile(buildDirectory / ManifestFileName);
        if (text.IsFailure())
        {
            return Error(ErrorCode::FileNotFound,
                         "'" + FileSystem::ToString(buildDirectory) + "' is not an exported build");
        }

        Result<JsonValue> manifest = Json::Parse(text.Value());
        if (manifest.IsFailure())
        {
            return Error(ErrorCode::ParseError,
                         "The export manifest is not valid JSON: " + manifest.GetError().Message);
        }

        const JsonValue* version = manifest.Value().Find(ProjectFormat::ManifestVersionKey);
        if (version == nullptr || !version->IsNumber())
        {
            return Error(ErrorCode::ParseError, "An export manifest must declare its format version");
        }

        if (version->AsInt() != ProjectFormat::ManifestVersion)
        {
            return Error(ErrorCode::NotSupported,
                         "Export manifest version " + std::to_string(version->AsInt()) +
                             " is not supported by this engine");
        }

        return manifest;
    }

    Result<std::vector<FilePath>> Project::ListExportedFiles(const FilePath& buildDirectory)
    {
        std::vector<FilePath> files;

        for (const char* subdirectory : {"scenes", "prefabs", "scripts"})
        {
            const FilePath directory = buildDirectory / subdirectory;

            Result<std::vector<FilePath>> found = FileSystem::ListFiles(directory);
            if (found.IsFailure())
            {
                continue;
            }

            files.insert(files.end(), found.Value().begin(), found.Value().end());
        }

        std::sort(files.begin(), files.end());

        Result<std::vector<FilePath>> manifestFiles = FileSystem::ListFiles(buildDirectory);
        if (manifestFiles.IsSuccess())
        {
            for (const FilePath& file : manifestFiles.Value())
            {
                if (file.filename() == ManifestFileName)
                {
                    files.push_back(file);
                }
            }
        }

        return files;
    }
}