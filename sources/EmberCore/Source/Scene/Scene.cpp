// Scene/Scene.cpp

#include "Scene/Scene.h"

#include <algorithm>
#include <format>
#include <unordered_map>

#include "Core/FileSystem.h"
#include "Core/Logging/Log.h"

namespace Ember
{
    namespace
    {
        /// Builds the id assigned to an entity in a scene it did not come from.
        ///
        /// Ids only need to be unique within one scene, and a scene that has just
        /// been captured has no id for any of its entities, so the entity index is
        /// unique by construction.
        std::string MakeFreshId(EntityIndex index)
        {
            return std::format("e{}", index);
        }

        /// Reads a required string member, or returns an empty string.
        std::string ReadString(const JsonValue& object, const char* key)
        {
            const JsonValue* value = object.Find(key);
            return value != nullptr && value->IsString() ? value->AsString() : std::string();
        }

        /// Reads a bool member, falling back to `fallback` when absent.
        bool ReadBool(const JsonValue& object, const char* key, bool fallback)
        {
            const JsonValue* value = object.Find(key);
            return value != nullptr && value->IsBool() ? value->AsBool() : fallback;
        }
    }

    std::string Scene::MakeEntityId(const World& world, Entity entity)
    {
        (void)world;
        return MakeFreshId(entity.Index);
    }

    Scene Scene::Capture(const World& world)
    {
        Scene scene;

        for (const Entity entity : world.GetEntities())
        {
            EntityRecord record;
            record.Id = MakeEntityId(world, entity);
            record.Name = world.GetName(entity);
            record.Enabled = world.IsEnabled(entity);

            const Entity parent = world.GetParent(entity);
            if (parent != Entity::Null())
            {
                record.ParentId = MakeEntityId(world, parent);
            }

            JsonValue components(JsonValue::Object{});
            for (const ComponentTypeId type : world.GetComponentTypes(entity))
            {
                const ComponentTypeInfo* info = ComponentRegistry::Get().Find(type);
                if (info == nullptr)
                {
                    continue;
                }

                components.Set(info->Name, world.ComponentToJson(entity, type));
            }

            record.Components = std::move(components);

            if (record.ParentId.empty())
            {
                scene.m_RootIds.push_back(record.Id);
            }

            scene.m_Entities.push_back(std::move(record));
            scene.m_EntityIds.push_back(scene.m_Entities.back().Id);
        }

        return scene;
    }

    Result<std::vector<Entity>> Scene::Instantiate(World& world) const
    {
        std::vector<Entity> created;
        std::unordered_map<std::string, Entity> byId;
        created.reserve(m_Entities.size());
        byId.reserve(m_Entities.size());

        // Every entity is created before any hierarchy link is restored, so a
        // record may name a parent that appears later in the file.
        for (const EntityRecord& record : m_Entities)
        {
            const Entity entity = world.CreateEntity(record.Name);
            created.push_back(entity);
            byId.emplace(record.Id, entity);
        }

        // On failure, every entity this call created is destroyed, including the
        // one whose components could not be applied. Destroying in reverse creation
        // order takes each child before its parent, so a rollback never has to walk
        // a subtree it has already removed.
        const auto rollback = [&world, &created]
        {
            for (auto it = created.rbegin(); it != created.rend(); ++it)
            {
                world.DestroyEntity(*it);
            }
        };

        for (std::size_t i = 0; i < m_Entities.size(); ++i)
        {
            const EntityRecord& record = m_Entities[i];
            const Entity entity = created[i];

            world.SetEnabled(entity, record.Enabled);

            if (!record.ParentId.empty())
            {
                const auto parent = byId.find(record.ParentId);
                if (parent == byId.end())
                {
                    // A dangling parent reference means the file is internally
                    // inconsistent; unwinding keeps the world as the caller left it.
                    rollback();

                    return Error(ErrorCode::Serialization,
                                 "Entity '" + record.Id + "' names a parent '" + record.ParentId +
                                     "' that the scene does not contain");
                }

                world.SetParent(entity, parent->second);
            }

            for (const JsonValue::Member& member : record.Components.AsObject())
            {
                if (Result<void> result = world.AddComponentFromJson(entity, member.first, member.second);
                    result.IsFailure())
                {
                    rollback();
                    return result.GetError();
                }
            }

        }

        return created;
    }

    Result<void> Scene::LoadInto(World& world) const
    {
        // The new scene is built alongside whatever the world already holds. Only
        // once it is fully in place are the previous entities destroyed, so a
        // failed load leaves the caller's scene intact.
        const std::vector<Entity> previous = world.GetEntities();

        Result<std::vector<Entity>> created = Instantiate(world);
        if (created.IsFailure())
        {
            return created.GetError();
        }

        for (const Entity entity : previous)
        {
            world.DestroyEntity(entity);
        }

        return {};
    }

    const JsonValue& Scene::GetComponents(std::size_t index) const
    {
        static const JsonValue empty(JsonValue::Object{});
        return index < m_Entities.size() ? m_Entities[index].Components : empty;
    }

    const JsonValue& Scene::GetComponentsById(std::string_view id) const
    {
        const int index = FindIndexById(id);
        return index < 0 ? GetComponents(m_Entities.size()) : GetComponents(static_cast<std::size_t>(index));
    }

    int Scene::FindIndexById(std::string_view id) const noexcept
    {
        for (std::size_t i = 0; i < m_Entities.size(); ++i)
        {
            if (m_Entities[i].Id == id)
            {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    std::string Scene::ToJson(std::string_view kind) const
    {
        JsonValue entities(JsonValue::Array{});

        for (const EntityRecord& record : m_Entities)
        {
            JsonValue entity(JsonValue::Object{});
            entity.Set(SceneFormat::EntityIdKey, JsonValue(record.Id));

            if (!record.Name.empty())
            {
                entity.Set(SceneFormat::EntityNameKey, JsonValue(record.Name));
            }

            entity.Set(SceneFormat::EntityEnabledKey, JsonValue(record.Enabled));

            if (!record.ParentId.empty())
            {
                entity.Set(SceneFormat::EntityParentKey, JsonValue(record.ParentId));
            }

            entity.Set(SceneFormat::EntityComponentsKey, record.Components);
            entities.Push(std::move(entity));
        }

        JsonValue document(JsonValue::Object{});
        document.Set(SceneFormat::VersionKey, JsonValue(static_cast<double>(SceneFormatVersion)));
        document.Set(SceneFormat::KindKey, JsonValue(std::string(kind)));
        document.Set(SceneFormat::EntitiesKey, std::move(entities));

        return document.ToString();
    }

    Result<Scene> Scene::FromJson(std::string_view text)
    {
        Result<JsonValue> parsed = Json::Parse(text);
        if (parsed.IsFailure())
        {
            return parsed.GetError();
        }

        const JsonValue& document = parsed.Value();
        if (!document.IsObject())
        {
            return Error(ErrorCode::ParseError, "A scene must be a JSON object");
        }

        const JsonValue* version = document.Find(SceneFormat::VersionKey);
        if (version == nullptr || !version->IsNumber())
        {
            return Error(ErrorCode::ParseError, "A scene must declare its format version");
        }

        const auto formatVersion = version->AsInt();
        if (formatVersion != SceneFormatVersion)
        {
            return Error(ErrorCode::NotSupported,
                         std::format("Scene format version {} is not supported by this engine (expected {})",
                                     formatVersion, SceneFormatVersion));
        }

        const JsonValue* entities = document.Find(SceneFormat::EntitiesKey);
        if (entities == nullptr || !entities->IsArray())
        {
            return Error(ErrorCode::ParseError, "A scene must contain an array of entities");
        }

        Scene scene;

        for (const JsonValue& entry : entities->AsArray())
        {
            if (!entry.IsObject())
            {
                return Error(ErrorCode::ParseError, "Each scene entity must be a JSON object");
            }

            EntityRecord record;
            record.Id = ReadString(entry, SceneFormat::EntityIdKey);
            if (record.Id.empty())
            {
                return Error(ErrorCode::ParseError, "Each scene entity must have a non-empty id");
            }

            if (scene.FindIndexById(record.Id) >= 0)
            {
                return Error(ErrorCode::Serialization,
                             "Scene contains more than one entity with the id '" + record.Id + "'");
            }

            record.Name = ReadString(entry, SceneFormat::EntityNameKey);
            record.Enabled = ReadBool(entry, SceneFormat::EntityEnabledKey, true);
            record.ParentId = ReadString(entry, SceneFormat::EntityParentKey);

            if (const JsonValue* components = entry.Find(SceneFormat::EntityComponentsKey);
                components != nullptr && components->IsObject())
            {
                record.Components = *components;
            }

            if (record.ParentId.empty())
            {
                scene.m_RootIds.push_back(record.Id);
            }

            scene.m_EntityIds.push_back(record.Id);
            scene.m_Entities.push_back(std::move(record));
        }

        // Validate every parent link now, so a broken file fails at load rather
        // than half way through instantiating it.
        for (const EntityRecord& record : scene.m_Entities)
        {
            if (!record.ParentId.empty() && scene.FindIndexById(record.ParentId) < 0)
            {
                return Error(ErrorCode::Serialization,
                             "Entity '" + record.Id + "' names a parent '" + record.ParentId +
                                 "' that the scene does not contain");
            }
        }

        return scene;
    }

    Result<void> Scene::SaveToFile(const FilePath& path) const
    {
        return FileSystem::WriteTextFile(path, ToJson() + "\n");
    }

    Result<Scene> Scene::LoadFromFile(const FilePath& path)
    {
        Result<std::string> text = FileSystem::ReadTextFile(path);
        if (text.IsFailure())
        {
            return text.GetError();
        }

        Result<Scene> scene = FromJson(text.Value());
        if (scene.IsFailure())
        {
            return Error(scene.GetError().Code,
                         "Could not load scene '" + FileSystem::ToString(path) + "': " + scene.GetError().Message);
        }

        return scene;
    }

    namespace PrefabFormat
    {
        Result<void> SavePrefab(const FilePath& path, const World& world, const Entity& root)
        {
            if (!world.IsAlive(root))
            {
                EMBER_LOG_ERROR("Cannot save a prefab from a dead entity");
                return {ErrorCode::InvalidArgument, "Cannot save a prefab from a dead entity"};
            }

            // Prefabs and scenes share a format; the recorded kind is the only
            // difference between the two files.
            const std::string text = Scene::Capture(world).ToJson(SceneFormat::PrefabKind) + "\n";
            return FileSystem::WriteTextFile(path, text);
        }

        Result<Entity> InstantiatePrefab(const FilePath& path, World& world, Entity parent)
        {
            Result<Scene> scene = Scene::LoadFromFile(path);
            if (scene.IsFailure())
            {
                return scene.GetError();
            }

            Result<std::vector<Entity>> created = scene.Value().Instantiate(world);
            if (created.IsFailure())
            {
                return created.GetError();
            }

            const std::vector<Entity>& entities = created.Value();
            if (entities.empty())
            {
                return Error(ErrorCode::Serialization,
                             "Prefab '" + FileSystem::ToString(path) + "' contains no entities");
            }

            // The first entity is the prefab's root: the file lists a parent before
            // its children, because Capture walks entities in creation order.
            const Entity root = entities.front();

            if (parent != Entity::Null())
            {
                world.SetParent(root, parent);
            }

            return root;
        }
    }
}
