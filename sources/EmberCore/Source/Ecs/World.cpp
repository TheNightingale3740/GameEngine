// Ecs/World.cpp

#include "Ecs/World.h"

#include <algorithm>

#include "Core/Logging/Log.h"

namespace Ember
{
    World::World() = default;

    World::~World() = default;

    Entity World::CreateEntity(std::string name)
    {
        EntityIndex index;

        if (!m_FreeIndices.empty())
        {
            // Reuse a destroyed slot. Its generation has already been bumped, so
            // handles to the previous occupant stay detectably stale.
            index = m_FreeIndices.back();
            m_FreeIndices.pop_back();
        }
        else
        {
            index = static_cast<EntityIndex>(m_Entities.size());
            m_Entities.emplace_back();
        }

        EntityRecord& record = m_Entities[index];
        record.Name = std::move(name);
        record.Enabled = true;
        record.Parent = Entity::Null();
        record.Children.clear();

        return Entity(index, record.Generation);
    }

    bool World::DestroyEntity(Entity entity)
    {
        if (!IsAlive(entity))
        {
            return false;
        }

        // Children outlive the call: destroying a parent destroys its whole
        // subtree, and each child must be detached from its parent's list first
        // so that the recursion below does not walk a mutated vector.
        const std::vector<Entity> subtree = GetSubtree(entity);

        // Remove this entity's children from its own record before the record is
        // recycled, otherwise a stale child list would be inherited by whatever
        // entity reuses the slot.
        DetachFromParent(entity.Index);

        for (const Entity descendant : subtree)
        {
            for (std::unique_ptr<IComponentStorage>& storage : m_Storages)
            {
                if (storage != nullptr)
                {
                    storage->RemoveEntity(descendant.Index);
                }
            }

            EntityRecord& record = m_Entities[descendant.Index];
            record.Parent = Entity::Null();
            record.Children.clear();
            record.Name.clear();

            // Bumping the generation is what invalidates outstanding handles.
            ++record.Generation;
            m_FreeIndices.push_back(descendant.Index);
        }

        return true;
    }

    bool World::IsAlive(Entity entity) const noexcept
    {
        return entity.IsValid() && IsIndexAlive(entity.Index) &&
               m_Entities[entity.Index].Generation == entity.Generation;
    }

    std::size_t World::GetEntityCount() const noexcept
    {
        return m_Entities.size() - m_FreeIndices.size();
    }

    std::vector<Entity> World::GetEntities() const
    {
        std::vector<Entity> entities;
        entities.reserve(GetEntityCount());

        for (EntityIndex index = 0; index < m_Entities.size(); ++index)
        {
            if (IsIndexAlive(index))
            {
                entities.emplace_back(index, m_Entities[index].Generation);
            }
        }

        return entities;
    }

    void World::Clear()
    {
        for (std::unique_ptr<IComponentStorage>& storage : m_Storages)
        {
            if (storage != nullptr)
            {
                storage->Clear();
            }
        }

        m_Entities.clear();
        m_FreeIndices.clear();
    }

    void World::SetName(Entity entity, std::string name)
    {
        if (IsAlive(entity))
        {
            m_Entities[entity.Index].Name = std::move(name);
        }
    }

    const std::string& World::GetName(Entity entity) const
    {
        static const std::string empty;
        return IsAlive(entity) ? m_Entities[entity.Index].Name : empty;
    }

    void World::SetEnabled(Entity entity, bool enabled)
    {
        if (IsAlive(entity))
        {
            m_Entities[entity.Index].Enabled = enabled;
        }
    }

    bool World::IsEnabled(Entity entity) const
    {
        return IsAlive(entity) && m_Entities[entity.Index].Enabled;
    }

    bool World::IsActiveInHierarchy(Entity entity) const
    {
        if (!IsAlive(entity))
        {
            return false;
        }

        Entity current = entity;

        // The walk is bounded by the entity count rather than a `visited` set:
        // SetParent rejects cycles, so a chain can never revisit an entity.
        for (std::size_t depth = 0; depth <= m_Entities.size(); ++depth)
        {
            if (!m_Entities[current.Index].Enabled)
            {
                return false;
            }

            const Entity parent = m_Entities[current.Index].Parent;
            if (!IsAlive(parent))
            {
                return true;
            }

            current = parent;
        }

        EMBER_LOG_ERROR("Hierarchy cycle detected while testing whether entity {} is active", ToString(entity));
        return false;
    }

    void World::SetParent(Entity child, Entity parent)
    {
        if (!IsAlive(child))
        {
            return;
        }

        if (parent == child || (parent.IsValid() && IsAncestorOf(child.Index, parent)))
        {
            EMBER_LOG_ERROR("Refusing to parent entity {} to {}: that would create a cycle",
                            ToString(child), ToString(parent));
            return;
        }

        if (parent.IsValid() && !IsAlive(parent))
        {
            return;
        }

        DetachFromParent(child.Index);
        m_Entities[child.Index].Parent = parent;

        if (IsAlive(parent))
        {
            m_Entities[parent.Index].Children.push_back(child);
        }
    }

    Entity World::GetParent(Entity entity) const
    {
        if (!IsAlive(entity))
        {
            return Entity::Null();
        }

        const Entity parent = m_Entities[entity.Index].Parent;
        return IsAlive(parent) ? parent : Entity::Null();
    }

    void World::AddChild(Entity parent, Entity child)
    {
        SetParent(child, parent);
    }

    std::vector<Entity> World::GetChildren(Entity entity) const
    {
        if (!IsAlive(entity))
        {
            return {};
        }

        // Filter out any child whose handle has gone stale, so that a caller
        // never has to re-check what it is handed.
        std::vector<Entity> children;
        for (const Entity child : m_Entities[entity.Index].Children)
        {
            if (IsAlive(child))
            {
                children.push_back(child);
            }
        }

        return children;
    }

    std::vector<Entity> World::GetDescendants(Entity entity) const
    {
        std::vector<Entity> descendants;
        if (!IsAlive(entity))
        {
            return descendants;
        }

        std::vector<Entity> queue = GetChildren(entity);
        std::size_t head = 0;

        while (head < queue.size())
        {
            const Entity current = queue[head++];
            descendants.push_back(current);

            for (const Entity child : GetChildren(current))
            {
                queue.push_back(child);
            }
        }

        return descendants;
    }

    std::vector<Entity> World::GetSubtree(Entity entity) const
    {
        std::vector<Entity> subtree;
        if (!IsAlive(entity))
        {
            return subtree;
        }

        subtree.push_back(entity);

        std::vector<Entity> queue = GetChildren(entity);
        std::size_t head = 0;

        while (head < queue.size())
        {
            const Entity current = queue[head++];
            subtree.push_back(current);

            for (const Entity child : GetChildren(current))
            {
                queue.push_back(child);
            }
        }

        return subtree;
    }

    IComponentStorage& World::GetStorageFor(ComponentTypeId type)
    {
        // An unregistered type is a programming error, not a runtime condition:
        // every component the engine touches is declared with EMBER_COMPONENT, so
        // reaching here means a component's translation unit is not linked in.
        if (type == InvalidComponentType)
        {
            EMBER_LOG_FATAL("Cannot get storage for an unregistered component type");
            return NullStorage();
        }

        if (m_Storages.size() <= type)
        {
            m_Storages.resize(static_cast<std::size_t>(type) + 1);
        }

        if (m_Storages[type] == nullptr)
        {
            const ComponentTypeInfo* info = ComponentRegistry::Get().Find(type);
            if (info == nullptr)
            {
                EMBER_LOG_FATAL("No component type is registered for id {}", type);
                return NullStorage();
            }

            m_Storages[type] = info->CreateStorage(*info);
        }

        return *m_Storages[type];
    }

    IComponentStorage* World::FindStorageFor(ComponentTypeId type)
    {
        return type < m_Storages.size() ? m_Storages[type].get() : nullptr;
    }

    const IComponentStorage* World::FindStorageFor(ComponentTypeId type) const
    {
        return type < m_Storages.size() ? m_Storages[type].get() : nullptr;
    }

    const IComponentStorage& World::FindStorageForChecked(ComponentTypeId type) const
    {
        if (const IComponentStorage* storage = FindStorageFor(type); storage != nullptr)
        {
            return *storage;
        }

        return NullStorage();
    }

    void* World::GetComponentByType(Entity entity, ComponentTypeId type)
    {
        if (!IsAlive(entity))
        {
            return nullptr;
        }

        IComponentStorage* storage = FindStorageFor(type);
        return storage == nullptr ? nullptr : storage->Find(entity.Index);
    }

    const void* World::GetComponentByType(Entity entity, ComponentTypeId type) const
    {
        if (!IsAlive(entity))
        {
            return nullptr;
        }

        const IComponentStorage* storage = FindStorageFor(type);
        return storage == nullptr ? nullptr : storage->FindConst(entity.Index);
    }

    bool World::HasComponentByType(Entity entity, ComponentTypeId type) const
    {
        const IComponentStorage* storage = FindStorageFor(type);
        return IsAlive(entity) && storage != nullptr && storage->Has(entity.Index);
    }

    bool World::RemoveComponentByType(Entity entity, ComponentTypeId type)
    {
        if (!IsAlive(entity))
        {
            return false;
        }

        IComponentStorage* storage = FindStorageFor(type);
        return storage != nullptr && storage->Remove(entity.Index);
    }

    std::vector<ComponentTypeId> World::GetComponentTypes(Entity entity) const
    {
        std::vector<ComponentTypeId> types;
        if (!IsAlive(entity))
        {
            return types;
        }

        for (std::size_t type = 0; type < m_Storages.size(); ++type)
        {
            if (m_Storages[type] != nullptr && m_Storages[type]->Has(entity.Index))
            {
                types.push_back(static_cast<ComponentTypeId>(type));
            }
        }

        return types;
    }

    Result<void> World::AddComponentFromJson(Entity entity, std::string_view typeName, const JsonValue& fields)
    {
        if (!IsAlive(entity))
        {
            return {ErrorCode::InvalidArgument, "Cannot add a component to a dead entity"};
        }

        const ComponentTypeId type = ComponentRegistry::Get().FindByName(typeName);
        if (type == InvalidComponentType)
        {
            return Error(ErrorCode::NotFound, "No component type named '" + std::string(typeName) + "' is registered");
        }

        GetStorageFor(type).InsertFromJson(entity.Index, fields);
        return {};
    }

    JsonValue World::ComponentToJson(Entity entity, ComponentTypeId type) const
    {
        const IComponentStorage* storage = FindStorageFor(type);
        if (storage == nullptr || !IsAlive(entity))
        {
            return {};
        }

        return storage->ToJson(entity.Index);
    }

    bool World::IsAncestorOf(EntityIndex candidate, Entity entity) const noexcept
    {
        if (!IsAlive(entity))
        {
            return false;
        }

        Entity current = entity;

        for (std::size_t depth = 0; depth <= m_Entities.size(); ++depth)
        {
            const Entity parent = m_Entities[current.Index].Parent;
            if (!IsAlive(parent))
            {
                return false;
            }

            if (parent.Index == candidate)
            {
                return true;
            }

            current = parent;
        }

        return false;
    }

    void World::DetachFromParent(EntityIndex index)
    {
        const Entity parent = m_Entities[index].Parent;
        if (!IsAlive(parent))
        {
            m_Entities[index].Parent = Entity::Null();
            return;
        }

        std::vector<Entity>& siblings = m_Entities[parent.Index].Children;
        std::erase(siblings, Entity(index, m_Entities[index].Generation));
        m_Entities[index].Parent = Entity::Null();
    }
}
