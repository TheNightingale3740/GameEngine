// Ecs/World.h
//
// The entity-component world.
//
// Storage is archetype-free: each component type owns a sparse set of component
// values indexed by entity. Iteration over a single component type is therefore
// contiguous in memory, which is what the renderer and the physics step need,
// and adding or removing a component for one entity never touches another
// entity's storage.
//
// Threading: a world is not thread safe. Systems run one at a time on the thread
// that owns the world, and any parallelism inside a system is that system's own
// business.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Core/Math/Math.h"
#include "Core/Result.h"
#include "Ecs/ComponentRegistry.h"
#include "Ecs/ComponentStorage.h"
#include "Ecs/Entity.h"

namespace Ember
{
    /// Owns entities and their components.
    class World
    {
    public:
        World();
        ~World();

        World(const World&) = delete;
        World& operator=(const World&) = delete;
        World(World&&) = delete;
        World& operator=(World&&) = delete;

        // ----------------------------------------------------------------- entities

        /// Creates an entity and returns its handle.
        Entity CreateEntity(std::string name = {});

        /// Destroys an entity, its components and its links to parents and
        /// children. Returns false if the handle was already stale.
        bool DestroyEntity(Entity entity);

        /// True when the handle refers to a live entity.
        [[nodiscard]] bool IsAlive(Entity entity) const noexcept;

        /// Number of live entities.
        [[nodiscard]] std::size_t GetEntityCount() const noexcept;

        /// All live entities, in ascending index order.
        [[nodiscard]] std::vector<Entity> GetEntities() const;

        /// Destroys every entity. The component registry is unaffected.
        void Clear();

        // ----------------------------------------------------------------- naming

        /// Sets the entity's editor-facing name. Names need not be unique.
        void SetName(Entity entity, std::string name);

        /// Returns the entity's name, or "" if it has none.
        [[nodiscard]] const std::string& GetName(Entity entity) const;

        /// Enables or disables an entity and its descendants.
        ///
        /// A disabled entity is skipped by every system and is not rendered.
        /// Disabling is a property of the entity itself: re-enabling a parent
        /// does not override a child that was disabled on its own.
        void SetEnabled(Entity entity, bool enabled);

        /// Returns the entity's own enabled flag, ignoring its ancestors.
        [[nodiscard]] bool IsEnabled(Entity entity) const;

        /// True when the entity and every ancestor up to the root are enabled.
        [[nodiscard]] bool IsActiveInHierarchy(Entity entity) const;

        // ----------------------------------------------------------------- hierarchy

        /// Makes `child` a child of `parent`, or a root when `parent` is null.
        ///
        /// Rejects a cycle: a world in which an entity is its own ancestor would
        /// make every hierarchy traversal non-terminating.
        void SetParent(Entity child, Entity parent);

        [[nodiscard]] Entity GetParent(Entity entity) const;

        /// Appends a child to `parent`.
        void AddChild(Entity parent, Entity child);

        [[nodiscard]] std::vector<Entity> GetChildren(Entity entity) const;

        /// The entity's children followed by their descendants, breadth first.
        [[nodiscard]] std::vector<Entity> GetDescendants(Entity entity) const;

        /// The entity and all of its descendants, breadth first.
        [[nodiscard]] std::vector<Entity> GetSubtree(Entity entity) const;

        // ----------------------------------------------------------------- components

        /// Adds or overwrites a component and returns a pointer to it.
        ///
        /// The pointer is valid until the component is removed or the storage
        /// reallocates. Pass a value to set every field at once.
        template <typename T>
        T* AddComponent(Entity entity, T value = {})
        {
            if (!IsAlive(entity))
            {
                return nullptr;
            }

            const ComponentTypeId type = Ecs::GetComponentTypeId<T>();
            IComponentStorage& storage = GetStorageFor(type);
            storage.Insert(entity.Index, &value);
            return static_cast<T*>(storage.Find(entity.Index));
        }

        /// Removes a component. Returns false if it was not present.
        template <typename T>
        bool RemoveComponent(Entity entity)
        {
            return IsAlive(entity) && GetStorageFor(Ecs::GetComponentTypeId<T>()).Remove(entity.Index);
        }

        template <typename T>
        [[nodiscard]] bool HasComponent(Entity entity) const
        {
            const IComponentStorage* storage = FindStorageFor(Ecs::GetComponentTypeId<T>());
            return storage != nullptr && storage->Has(entity.Index);
        }

        /// Returns the component, or nullptr if absent or the handle is stale.
        template <typename T>
        [[nodiscard]] T* TryGetComponent(Entity entity)
        {
            IComponentStorage* storage = IsAlive(entity) ? FindStorageFor(Ecs::GetComponentTypeId<T>()) : nullptr;
            return storage == nullptr ? nullptr : static_cast<T*>(storage->Find(entity.Index));
        }

        template <typename T>
        [[nodiscard]] const T* TryGetComponent(Entity entity) const
        {
            const IComponentStorage* storage = IsAlive(entity) ? FindStorageFor(Ecs::GetComponentTypeId<T>()) : nullptr;
            return storage == nullptr ? nullptr : static_cast<const T*>(storage->FindConst(entity.Index));
        }

        /// Returns the component, or `fallback` when it is absent.
        ///
        /// Convenient for read-mostly code. Never use the result to mutate, since
        /// the component may not exist.
        template <typename T>
        [[nodiscard]] T GetComponent(Entity entity, T fallback = {}) const
        {
            if (const T* component = TryGetComponent<T>(entity); component != nullptr)
            {
                return *component;
            }

            return fallback;
        }

        /// Raw storage for a component type, for bulk work.
        ///
        /// Normal code should use `Each`, `TryGetComponent` or the type-erased
        /// accessors; this exists for the systems that iterate dense arrays and
        /// for tests.
        template <typename T>
        [[nodiscard]] ComponentStorage<T>& GetStorage()
        {
            return static_cast<TypedComponentStorage<T>&>(GetStorageFor(Ecs::GetComponentTypeId<T>())).Inner();
        }

        template <typename T>
        [[nodiscard]] const ComponentStorage<T>& GetStorage() const
        {
            return static_cast<const TypedComponentStorage<T>&>(FindStorageForChecked(Ecs::GetComponentTypeId<T>())).Inner();
        }

        /// Number of entities holding a component.
        template <typename T>
        [[nodiscard]] std::size_t GetComponentCount() const
        {
            const IComponentStorage* storage = FindStorageFor(Ecs::GetComponentTypeId<T>());
            return storage == nullptr ? 0 : storage->GetCount();
        }

        // ------------------------------------------------- type-erased component access

        /// Returns a component by type id, or nullptr.
        [[nodiscard]] void* GetComponentByType(Entity entity, ComponentTypeId type);
        [[nodiscard]] const void* GetComponentByType(Entity entity, ComponentTypeId type) const;

        [[nodiscard]] bool HasComponentByType(Entity entity, ComponentTypeId type) const;
        bool RemoveComponentByType(Entity entity, ComponentTypeId type);

        /// Component type ids present on an entity, in registration order.
        [[nodiscard]] std::vector<ComponentTypeId> GetComponentTypes(Entity entity) const;

        /// Adds a component from a JSON object, resolving the type by name.
        ///
        /// Fails with NotFound when no component of that name is registered.
        Result<void> AddComponentFromJson(Entity entity, std::string_view typeName, const JsonValue& fields);

        /// Serialises a component's reflected fields into a JSON object.
        [[nodiscard]] JsonValue ComponentToJson(Entity entity, ComponentTypeId type) const;

        // ----------------------------------------------------------------- iteration

        /// Calls `callback(T&, Entity)` for every entity holding a `T`.
        ///
        /// Adding or removing a `T` from inside the callback is undefined: the
        /// iteration holds an index into a vector that may move. Collect the
        /// entities first when the callback mutates the world.
        template <typename T, typename Callback>
        void Each(Callback&& callback)
        {
            ComponentStorage<T>& storage = GetStorage<T>();
            for (std::size_t i = 0; i < storage.Size(); ++i)
            {
                const EntityIndex index = storage.GetEntityAt(i);
                if (IsIndexAlive(index))
                {
                    callback(*storage.Find(index), Entity(index, GenerationOf(index)));
                }
            }
        }

        template <typename T, typename Callback>
        void Each(Callback&& callback) const
        {
            const ComponentStorage<T>& storage = GetStorage<T>();
            for (std::size_t i = 0; i < storage.Size(); ++i)
            {
                const EntityIndex index = storage.GetEntityAt(i);
                if (IsIndexAlive(index))
                {
                    callback(*storage.Find(index), Entity(index, GenerationOf(index)));
                }
            }
        }

        /// Calls `callback(First&, Entity, Rest&...)` for entities holding every
        /// listed component. Iteration is driven by the first storage listed, so
        /// put the rarest component first when performance matters.
        template <typename First, typename... Rest, typename Callback>
        void EachWith(Callback&& callback)
        {
            ComponentStorage<First>& primary = GetStorage<First>();

            for (std::size_t i = 0; i < primary.Size(); ++i)
            {
                const EntityIndex index = primary.GetEntityAt(i);
                if (!IsIndexAlive(index))
                {
                    continue;
                }

                const Entity entity(index, GenerationOf(index));
                if (!(HasComponent<Rest>(entity) && ...))
                {
                    continue;
                }

                callback(*primary.Find(index), entity, *TryGetComponent<Rest>(entity)...);
            }
        }

        template <typename First, typename... Rest, typename Callback>
        void EachWith(Callback&& callback) const
        {
            const ComponentStorage<First>& storage = GetStorage<First>();

            for (std::size_t i = 0; i < storage.Size(); ++i)
            {
                const EntityIndex index = storage.GetEntityAt(i);
                if (!IsIndexAlive(index))
                {
                    continue;
                }

                const Entity entity(index, GenerationOf(index));
                if (!(HasComponent<Rest>(entity) && ...))
                {
                    continue;
                }

                callback(*storage.Find(index), entity, *TryGetComponent<Rest>(entity)...);
            }
        }

    private:
        /// Per-entity state that the component storages do not hold.
        ///
        /// `Generation` of zero marks a free slot, which is what lets a destroyed
        /// entity's index be recycled without a second "is this live" flag.
        struct EntityRecord
        {
            EntityGeneration Generation = 1;
            std::string Name;
            bool Enabled = true;
            Entity Parent = Entity::Null();
            std::vector<Entity> Children;
        };

        [[nodiscard]] bool IsIndexAlive(EntityIndex index) const noexcept
        {
            return index < m_Entities.size() && m_Entities[index].Generation != 0;
        }

        [[nodiscard]] EntityGeneration GenerationOf(EntityIndex index) const noexcept
        {
            return index < m_Entities.size() ? m_Entities[index].Generation : EntityGeneration(0);
        }

        /// Returns the storage for a type, creating it on first use.
        [[nodiscard]] IComponentStorage& GetStorageFor(ComponentTypeId type);

        /// Returns the storage for a type if it already exists, else nullptr.
        [[nodiscard]] IComponentStorage* FindStorageFor(ComponentTypeId type);
        [[nodiscard]] const IComponentStorage* FindStorageFor(ComponentTypeId type) const;

        [[nodiscard]] const IComponentStorage& FindStorageForChecked(ComponentTypeId type) const;

        /// True when walking up from `entity` reaches `candidate`.
        [[nodiscard]] bool IsAncestorOf(EntityIndex candidate, Entity entity) const noexcept;

        void DetachFromParent(EntityIndex index);

        std::vector<EntityRecord> m_Entities;
        std::vector<EntityIndex> m_FreeIndices;

        /// Lazily created storage, indexed by component type id.
        std::vector<std::unique_ptr<IComponentStorage>> m_Storages;
    };
}
