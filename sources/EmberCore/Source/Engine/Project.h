// Engine/Project.h
//
// A project's layout on disk and the export pipeline that ships it.
//
// A project is a directory with a fixed shape:
//
//     project.emberproj      settings: name, entry scene, physics layers
//     scenes/*.ember         scenes
//     prefabs/*.ember        prefabs
//     scripts/*.lua          scripts
//     assets/meshes/*.gltf   meshes
//     assets/materials/*     materials
//     assets/audio/*         sounds
//     build/                 the exported game
//
// Export copies what the runtime needs into `build/` and writes a manifest that
// the runtime reads. The runtime never reads a scene file directly from the
// project: it reads the exported bundle, so what ships is exactly what was
// tested, and a project file that changes after export cannot change the game.

#pragma once

#include <string>
#include <vector>

#include "Core/FileSystem.h"
#include "Core/Json.h"
#include "Core/Math/Math.h"
#include "Core/Result.h"

namespace Ember
{
    /// A project's settings, as read from its project file.
    struct ProjectSettings
    {
        std::string Name = "Untitled";
        std::string Description;

        /// Scene loaded when the game starts, relative to the scenes directory.
        std::string StartScene;

        /// Fixed simulation step, in seconds.
        float PhysicsTimeStep = 1.0f / 60.0f;

        /// Upward acceleration for the simulation, in metres per second squared.
        Vec3 Gravity = Vec3(0.0f, -9.81f, 0.0f);

        /// Collision layers, as a name and the layers it collides with.
        struct Layer
        {
            std::string Name;
            std::vector<std::string> CollidesWith;
        };

        std::vector<Layer> CollisionLayers;

        [[nodiscard]] static ProjectSettings Defaults();
    };

    /// A project on disk.
    class Project
    {
    public:
        Project() = default;

        /// Opens an existing project directory. Fails with FileNotFound if the
        /// directory has no project file.
        [[nodiscard]] static Result<Project> Open(const FilePath& directory);

        /// Creates a project directory and its layout, with a default settings file.
        ///
        /// Fails with AlreadyExists if the directory already has a project file.
        [[nodiscard]] static Result<Project> Create(const FilePath& directory, std::string name);

        [[nodiscard]] bool IsValid() const noexcept { return m_Valid; }
        [[nodiscard]] const FilePath& GetDirectory() const noexcept { return m_Directory; }

        /// Builds an absolute path to a file inside the project.
        ///
        /// `relativePath` is resolved against the project directory, and may name
        /// any subdirectory. A path that escapes the project is rejected: a
        /// project file is data, and data should not be able to name a file
        /// outside the project it belongs to.
        [[nodiscard]] Result<FilePath> Resolve(std::string_view relativePath) const;

        [[nodiscard]] const ProjectSettings& GetSettings() const noexcept { return m_Settings; }

        /// Replaces the settings and writes them to disk.
        Result<void> SetSettings(const ProjectSettings& settings);

        [[nodiscard]] const FilePath& GetScenesDirectory() const noexcept { return m_ScenesDirectory; }
        [[nodiscard]] const FilePath& GetPrefabsDirectory() const noexcept { return m_PrefabsDirectory; }
        [[nodiscard]] const FilePath& GetScriptsDirectory() const noexcept { return m_ScriptsDirectory; }
        [[nodiscard]] const FilePath& GetAssetsDirectory() const noexcept { return m_AssetsDirectory; }
        [[nodiscard]] const FilePath& GetBuildDirectory() const noexcept { return m_BuildDirectory; }

        /// Path of the start scene, or nullptr when the settings name none.
        [[nodiscard]] Result<FilePath> GetStartScenePath() const;

        // ------------------------------------------------------------------ export

        /// Copies the project into its build directory and writes a manifest.
        ///
        /// The export is what a shipped game runs from, so it contains the scenes,
        /// the prefabs and the scripts, and the manifest that names them. Meshes
        /// and sounds are referenced by path from the manifest rather than
        /// copied, since they are already in a form the runtime reads directly.
        ///
        /// An existing build directory is replaced rather than merged: a stale
        /// file left behind from a previous export would otherwise ship.
        [[nodiscard]] Result<void> Export(const FilePath& buildDirectory) const;

        /// Reads an exported build's manifest.
        ///
        /// Fails with FileNotFound when the directory is not an export.
        [[nodiscard]] static Result<JsonValue> ReadManifest(const FilePath& buildDirectory);

        /// Files an export contains, for a test or a report.
        [[nodiscard]] static Result<std::vector<FilePath>> ListExportedFiles(const FilePath& buildDirectory);

    private:
        static constexpr const char* ProjectFileName = "project.emberproj";
        static constexpr const char* ManifestFileName = "manifest.json";

        [[nodiscard]] FilePath GetProjectFilePath() const;
        [[nodiscard]] FilePath GetManifestPath(const FilePath& buildDirectory) const;

        [[nodiscard]] Result<ProjectSettings> ReadSettings(const FilePath& path) const;
        [[nodiscard]] Result<void> WriteSettings(const FilePath& path, const ProjectSettings& settings) const;

        ProjectSettings m_Settings;
        FilePath m_Directory;
        FilePath m_ScenesDirectory;
        FilePath m_PrefabsDirectory;
        FilePath m_ScriptsDirectory;
        FilePath m_AssetsDirectory;
        FilePath m_BuildDirectory;
        bool m_Valid = false;
    };

    namespace ProjectFormat
    {
        inline constexpr const char* VersionKey = "version";
        inline constexpr const char* NameKey = "name";
        inline constexpr const char* DescriptionKey = "description";
        inline constexpr const char* StartSceneKey = "startScene";
        inline constexpr const char* PhysicsKey = "physics";
        inline constexpr const char* PhysicsTimeStepKey = "timeStep";
        inline constexpr const char* GravityKey = "gravity";
        inline constexpr const char* LayersKey = "collisionLayers";
        inline constexpr const char* LayerNameKey = "name";
        inline constexpr const char* LayerCollidesWithKey = "collidesWith";

        inline constexpr const char* ManifestVersionKey = "version";
        inline constexpr const char* ManifestProjectKey = "project";
        inline constexpr const char* ManifestStartSceneKey = "startScene";
        inline constexpr const char* ManifestScenesKey = "scenes";
        inline constexpr const char* ManifestPrefabsKey = "prefabs";
        inline constexpr const char* ManifestScriptsKey = "scripts";
        inline constexpr const char* ManifestAssetRootKey = "assetRoot";

        /// Format version of a project file.
        inline constexpr std::int32_t Version = 1;

        /// Format version of an exported manifest.
        inline constexpr std::int32_t ManifestVersion = 1;
    }
}