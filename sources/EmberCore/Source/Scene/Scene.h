// Scene/Scene.h
//
// Scene and prefab serialisation.
//
// A scene is a JSON document holding every entity, its name, its parent and its
// components. Prefabs use the same format, so a prefab is a scene with a single
// root entity that other scenes instantiate from.
//
// Entity identity is the hard part. Handles are generational indices that depend
// on the order entities were created, so they cannot appear in a file. Instead
// each entity is written with a stable string id, and hierarchy links and
// cross-entity references are expressed in terms of those ids. Loading creates
// every entity first and then restores the links, so a scene can refer to an
// entity declared later in the file.

#pragma once

#include <string>
#include <vector>

#include "Core/FileSystem.h"
#include "Core/Json.h"
#include "Core/Result.h"
#include "Ecs/Entity.h"
#include "Ecs/World.h"

namespace Ember
{
    namespace SceneFormat
    {
        /// Keys used in the on-disk format. Scene files are read by hand often
        /// enough that the spelling is part of the engine's contract.
        inline constexpr const char* VersionKey = "version";
        inline constexpr const char* KindKey = "kind";
        inline constexpr const char* EntitiesKey = "entities";

        inline constexpr const char* EntityIdKey = "id";
        inline constexpr const char* EntityNameKey = "name";
        inline constexpr const char* EntityEnabledKey = "enabled";
        inline constexpr const char* EntityParentKey = "parent";
        inline constexpr const char* EntityComponentsKey = "components";

        /// Values of the `kind` key.
        inline constexpr const char* SceneKind = "scene";
        inline constexpr const char* PrefabKind = "prefab";
    }

    /// Format version written into every scene and prefab.
    ///
    /// A loader must reject a file whose version it does not understand rather
    /// than guessing at the layout.
    inline constexpr std::int32_t SceneFormatVersion = 1;

    /// A scene held in memory, decoupled from the file it came from.
    ///
    /// The editor keeps the loaded scene around so that undo, dirty tracking and
    /// the export pipeline all work from the same snapshot rather than re-reading
    /// the file.
    class Scene
    {
    public:
        Scene() = default;

        /// Builds a scene from a world, assigning every entity a stable id.
        [[nodiscard]] static Scene Capture(const World& world);

        /// Instantiates this scene's entities into `world` and returns their
        /// handles in the same order as `GetEntities()`.
        ///
        /// The world is left untouched if loading fails part way through: entities
        /// created by a failed load are destroyed before the error is returned, so
        /// a failed load never leaves a half-populated world behind.
        Result<std::vector<Entity>> Instantiate(World& world) const;

        /// Replaces the world's contents with this scene.
        Result<void> LoadInto(World& world) const;

        // -------------------------------------------------------------- in-memory access

        [[nodiscard]] bool IsEmpty() const noexcept { return m_Entities.empty(); }
        [[nodiscard]] std::size_t GetEntityCount() const noexcept { return m_Entities.size(); }

        /// Stable string ids, in file order. Entity `i` has id `GetEntityIds()[i]`.
        [[nodiscard]] const std::vector<std::string>& GetEntityIds() const noexcept { return m_EntityIds; }

        /// Stable string ids of the entities that have no parent.
        [[nodiscard]] const std::vector<std::string>& GetRootIds() const noexcept { return m_RootIds; }

        [[nodiscard]] const JsonValue& GetComponents(std::size_t index) const;
        [[nodiscard]] const JsonValue& GetComponentsById(std::string_view id) const;

        /// Returns the index of a stable id, or -1.
        [[nodiscard]] int FindIndexById(std::string_view id) const noexcept;

        // ------------------------------------------------------------------ text form

        /// Serialises to a JSON document tagged with `kind`.
        ///
        /// `kind` is informational: both spellings load through `FromJson`, so a
        /// prefab and a scene share one parser and cannot drift apart.
        [[nodiscard]] std::string ToJson(std::string_view kind = SceneFormat::SceneKind) const;

        /// Parses a JSON document. Fails with ParseError on a malformed or
        /// unsupported document.
        static Result<Scene> FromJson(std::string_view text);

        /// Writes the scene to a file.
        Result<void> SaveToFile(const FilePath& path) const;

        /// Reads a scene from a file.
        static Result<Scene> LoadFromFile(const FilePath& path);

    private:
        /// One entity's serialised state.
        struct EntityRecord
        {
            std::string Id;
            std::string Name;
            bool Enabled = true;
            std::string ParentId;
            JsonValue Components{JsonValue::Object{}};
        };

        /// Assigns an id to an entity handle, reusing the id the scene already has
        /// when the entity was produced by `Instantiate` on this same scene.
        static std::string MakeEntityId(const World& world, Entity entity);

        std::vector<EntityRecord> m_Entities;
        std::vector<std::string> m_EntityIds;
        std::vector<std::string> m_RootIds;
    };

    /// A prefab: a scene whose entities are instantiated into another world.
    ///
    /// A prefab is a scene file with `kind` set to "prefab". Both are loaded and
    /// saved by the same code, which is what keeps the two formats from drifting.
    using Prefab = Scene;

    namespace PrefabFormat
    {
        /// Writes a prefab file from a world holding exactly one root entity.
        Result<void> SavePrefab(const FilePath& path, const World& world, const Entity& root);

        /// Instantiates a prefab file into `world`, returning the new root entity.
        ///
        /// Any children of `parent` are reparented under the new root, so the
        /// instance inherits the parent's transform.
        Result<Entity> InstantiatePrefab(const FilePath& path, World& world, Entity parent = Entity::Null());
    }
}
