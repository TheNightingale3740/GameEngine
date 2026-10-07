// Ecs/ComponentStorage.h
//
// Type-erased component storage.
//
// `ComponentStorage<T>` is the sparse set for one component type.
// `IComponentStorage` is the handle the world keeps, so the world can destroy an
// entity's components, enumerate them and serialise them without knowing their
// types. Reading and writing a component's fields stays in
// ComponentRegistry.cpp, driven by the reflected property list.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "Core/Json.h"
#include "Ecs/ComponentRegistry.h"
#include "Ecs/Entity.h"

namespace Ember
{
    /// A sparse set of components of one type, keyed by entity index.
    ///
    /// The dense arrays hold component values and their owning entity indices in
    /// the same order. Removal moves the tail entry into the freed slot so that
    /// iteration stays contiguous; that move is observable, so a caller must not
    /// hold a component pointer across a removal.
    template <typename T>
    class ComponentStorage
    {
    public:
        /// Component values in dense order.
        [[nodiscard]] T* Data() noexcept { return m_Components.data(); }
        [[nodiscard]] const T* Data() const noexcept { return m_Components.data(); }

        /// Entity index owning each dense entry, parallel to `Data()`.
        [[nodiscard]] const std::vector<EntityIndex>& EntityIndices() const noexcept { return m_EntityIndices; }

        [[nodiscard]] std::size_t Size() const noexcept { return m_Components.size(); }
        [[nodiscard]] bool Empty() const noexcept { return m_Components.empty(); }

        /// Returns the component for an entity index, or nullptr.
        [[nodiscard]] T* Find(EntityIndex index) noexcept
        {
            const auto found = m_Sparse.find(index);
            return found == m_Sparse.end() ? nullptr : &m_Components[found->second];
        }

        [[nodiscard]] const T* Find(EntityIndex index) const noexcept
        {
            const auto found = m_Sparse.find(index);
            return found == m_Sparse.end() ? nullptr : &m_Components[found->second];
        }

        /// Entity index owning the dense entry at `slot`.
        [[nodiscard]] EntityIndex GetEntityAt(std::size_t slot) const noexcept
        {
            return slot < m_EntityIndices.size() ? m_EntityIndices[slot] : Entity::InvalidIndex;
        }

        /// Adds or overwrites the component for an entity index.
        T& Insert(EntityIndex index, T value)
        {
            if (T* existing = Find(index); existing != nullptr)
            {
                *existing = std::move(value);
                return *existing;
            }

            m_Components.push_back(std::move(value));
            m_EntityIndices.push_back(index);
            m_Sparse[index] = m_Components.size() - 1;
            return m_Components.back();
        }

        /// Removes the component for an entity index. Returns false if absent.
        bool Remove(EntityIndex index) noexcept
        {
            const auto found = m_Sparse.find(index);
            if (found == m_Sparse.end())
            {
                return false;
            }

            const std::size_t slot = found->second;
            const std::size_t lastSlot = m_Components.size() - 1;

            if (slot != lastSlot)
            {
                m_Components[slot] = std::move(m_Components[lastSlot]);
                m_EntityIndices[slot] = m_EntityIndices[lastSlot];
                m_Sparse[m_EntityIndices[slot]] = slot;
            }

            m_Components.pop_back();
            m_EntityIndices.pop_back();
            m_Sparse.erase(index);
            return true;
        }

        void Clear() noexcept
        {
            m_Components.clear();
            m_EntityIndices.clear();
            m_Sparse.clear();
        }

    private:
        std::vector<T> m_Components;
        std::vector<EntityIndex> m_EntityIndices;
        std::unordered_map<EntityIndex, std::size_t> m_Sparse;
    };

    /// Type-erased access to one component type's storage.
    class IComponentStorage
    {
    public:
        virtual ~IComponentStorage() = default;

        /// Type information for the component this storage holds.
        [[nodiscard]] virtual const ComponentTypeInfo& GetInfo() const noexcept = 0;

        /// Number of components stored.
        [[nodiscard]] virtual std::size_t GetCount() const noexcept = 0;

        [[nodiscard]] virtual bool Has(EntityIndex index) const noexcept = 0;

        /// Returns a mutable component for an entity index, or nullptr.
        [[nodiscard]] virtual void* Find(EntityIndex index) noexcept = 0;

        /// Returns a component for an entity index, or nullptr.
        ///
        /// Named rather than a `const`-qualified overload of `Find`: a pair of
        /// virtuals differing only in const qualification and return cv-qualifier
        /// is rejected by Clang, and a distinct name keeps the intent explicit.
        [[nodiscard]] virtual const void* FindConst(EntityIndex index) const noexcept = 0;

        /// Adds or overwrites a component for an entity index.
        virtual void Insert(EntityIndex index, const void* component) = 0;

        /// Removes the component for an entity index. Returns false if absent.
        virtual bool Remove(EntityIndex index) noexcept = 0;

        /// Removes every component belonging to a destroyed entity.
        virtual void RemoveEntity(EntityIndex index) noexcept = 0;

        /// Removes all components.
        virtual void Clear() noexcept = 0;

        /// Constructs a component from a JSON object of reflected fields.
        virtual void InsertFromJson(EntityIndex index, const JsonValue& fields) = 0;

        /// Serialises a component's reflected fields into a JSON object.
        [[nodiscard]] virtual JsonValue ToJson(EntityIndex index) const = 0;
    };

    /// Erases one concrete component storage.
    template <typename T>
    class TypedComponentStorage final : public IComponentStorage
    {
    public:
        explicit TypedComponentStorage(const ComponentTypeInfo& info)
            : m_Info(&info)
            , m_Storage()
        {
        }

        [[nodiscard]] const ComponentTypeInfo& GetInfo() const noexcept override { return *m_Info; }
        [[nodiscard]] std::size_t GetCount() const noexcept override { return m_Storage.Size(); }

        [[nodiscard]] bool Has(EntityIndex index) const noexcept override { return m_Storage.Find(index) != nullptr; }

        [[nodiscard]] void* Find(EntityIndex index) noexcept override { return m_Storage.Find(index); }

        [[nodiscard]] const void* FindConst(EntityIndex index) const noexcept override { return m_Storage.Find(index); }

        void Insert(EntityIndex index, const void* component) override
        {
            if (component != nullptr)
            {
                m_Storage.Insert(index, *static_cast<const T*>(component));
            }
        }

        bool Remove(EntityIndex index) noexcept override { return m_Storage.Remove(index); }
        void RemoveEntity(EntityIndex index) noexcept override { m_Storage.Remove(index); }
        void Clear() noexcept override { m_Storage.Clear(); }

        void InsertFromJson(EntityIndex index, const JsonValue& fields) override
        {
            T component{};
            ComponentRegistry::Get().DeserializeInto(*m_Info, static_cast<void*>(&component), fields);
            m_Storage.Insert(index, component);
        }

        [[nodiscard]] JsonValue ToJson(EntityIndex index) const override
        {
            const T* component = m_Storage.Find(index);
            return component == nullptr ? JsonValue()
                                        : ComponentRegistry::Get().Serialize(*m_Info, static_cast<const void*>(component));
        }

        /// Typed access to the underlying sparse set.
        [[nodiscard]] ComponentStorage<T>& Inner() noexcept { return m_Storage; }
        [[nodiscard]] const ComponentStorage<T>& Inner() const noexcept { return m_Storage; }

    private:
        const ComponentTypeInfo* m_Info;
        ComponentStorage<T> m_Storage;
    };

    /// Creates the erased storage for a component type.
    template <typename T>
    [[nodiscard]] std::unique_ptr<IComponentStorage> CreateComponentStorage(const ComponentTypeInfo& info)
    {
        return std::make_unique<TypedComponentStorage<T>>(info);
    }

    /// A storage that holds nothing, returned when a type is not registered.
    ///
    /// Asking a world for the storage of an unregistered component type is a
    /// programming error that has already been logged. Returning an empty
    /// storage rather than a null reference keeps the caller alive long enough to
    /// fail its own assertions, instead of dereferencing undefined behaviour.
    class NullComponentStorage final : public IComponentStorage
    {
    public:
        [[nodiscard]] const ComponentTypeInfo& GetInfo() const noexcept override { return m_Info; }
        [[nodiscard]] std::size_t GetCount() const noexcept override { return 0; }
        [[nodiscard]] bool Has(EntityIndex) const noexcept override { return false; }

        [[nodiscard]] void* Find(EntityIndex) noexcept override { return nullptr; }

        [[nodiscard]] const void* FindConst(EntityIndex) const noexcept override { return nullptr; }
        void Insert(EntityIndex, const void*) override {}
        bool Remove(EntityIndex) noexcept override { return false; }
        void RemoveEntity(EntityIndex) noexcept override {}
        void Clear() noexcept override {}
        void InsertFromJson(EntityIndex, const JsonValue&) override {}
        [[nodiscard]] JsonValue ToJson(EntityIndex) const override { return {}; }

    private:
        ComponentTypeInfo m_Info;
    };

    /// Returns the shared empty storage used when a type is not registered.
    [[nodiscard]] inline IComponentStorage& NullStorage() noexcept
    {
        static NullComponentStorage instance;
        return instance;
    }
}

namespace Ember::Ecs
{
    template <typename T>
    void RegisterBuiltin(std::string_view name, std::vector<Property> properties)
    {
        (void)RegisterComponent<T>(name, std::move(properties));
    }

    template <typename T>
    ComponentTypeId RegisterComponent(std::string_view name, std::vector<Property> properties)
    {
        static_assert(std::is_default_constructible_v<T>,
                      "components must be default constructible so that loading a scene can fill them in");
        static_assert(std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>,
                      "component storage relocates components when an entity is removed, so components must be "
                      "copyable rather than move-only");

        const ComponentRegistry& registry = ComponentRegistry::Get();

        if (const ComponentTypeId existing = registry.TypeId<T>(); existing != InvalidComponentType)
        {
            return existing;
        }

        return ComponentRegistry::Get().RegisterImpl(
            std::type_index(typeid(T)),
            name,
            sizeof(T),
            alignof(T),
            std::move(properties),
            &CreateComponentStorage<T>);
    }
}
