// Ecs/ComponentRegistry.h
//
// The process-wide table of registered component types.
//
// Every component declares itself once with `EMBER_COMPONENT`, which registers
// its name, layout, reflected properties and storage factory here during static
// initialisation. The editor, the scene serialiser and the script bindings all
// resolve components through this table, which is why none of them needs to know
// the set of components that exists.
//
// Registration is a start-of-process concern: the table is written during static
// initialisation and only read afterwards, so it carries no locking. Worlds may
// then be created and used from any thread.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "Core/Json.h"
#include "Ecs/Reflection.h"

namespace Ember
{
    class IComponentStorage;

    /// Index of a component type within the registry.
    using ComponentTypeId = std::uint16_t;

    /// Sentinel for "no such component type".
    inline constexpr ComponentTypeId InvalidComponentType = 0xFFFF;

    /// Everything the engine knows about one registered component type.
    struct ComponentTypeInfo
    {
        ComponentTypeId Id = InvalidComponentType;

        /// Name as declared in the component's `EMBER_COMPONENT` call. This is
        /// the spelling used in scene files and by scripts.
        std::string Name;

        /// Size and alignment of the component.
        std::size_t Size = 0;
        std::size_t Alignment = 0;

        /// Reflected fields in declaration order. Serialisation writes them in
        /// this order, which keeps saved scenes stable and diff-friendly.
        std::vector<Property> Properties;

        /// Creates this type's storage. Always set for a registered type.
        std::function<std::unique_ptr<IComponentStorage>(const ComponentTypeInfo&)> CreateStorage;

        /// Returns the reflected property with a name, or nullptr.
        [[nodiscard]] const Property* FindProperty(std::string_view name) const noexcept;
    };

    /// Creates the erased storage for a component type.
    ///
    /// Defined in ComponentStorage.h, which includes this header; declaring it
    /// here keeps the registration template below independent of that ordering.
    template <typename T>
    [[nodiscard]] std::unique_ptr<IComponentStorage> CreateComponentStorage(const ComponentTypeInfo& info);

    /// The process-wide component type table.
    class ComponentRegistry
    {
    public:
        /// Returns the process-wide registry.
        static ComponentRegistry& Get() noexcept;

        /// Looks up a registered type by name, or InvalidComponentType.
        [[nodiscard]] ComponentTypeId FindByName(std::string_view name) const noexcept;

        /// Returns the info for an id, or nullptr if the id is not registered.
        [[nodiscard]] const ComponentTypeInfo* Find(ComponentTypeId id) const noexcept;

        /// Returns the info for a type, or nullptr if unregistered.
        template <typename T>
        [[nodiscard]] const ComponentTypeInfo* Find() const noexcept
        {
            return Find(TypeId<T>());
        }

        /// Returns the id for `T`, or InvalidComponentType if unregistered.
        template <typename T>
        [[nodiscard]] ComponentTypeId TypeId() const noexcept
        {
            const auto found = m_IndexByType.find(std::type_index(typeid(T)));
            return found == m_IndexByType.end() ? InvalidComponentType : m_Types[found->second].Id;
        }

        /// True when `T` has been registered.
        template <typename T>
        [[nodiscard]] bool IsRegistered() const noexcept
        {
            return TypeId<T>() != InvalidComponentType;
        }

        [[nodiscard]] std::size_t Count() const noexcept { return m_Types.size(); }

        /// All registered types in registration order.
        [[nodiscard]] const std::vector<ComponentTypeInfo>& Types() const noexcept { return m_Types; }

        // ------------------------------------------------------------- serialisation

        /// Writes a component's reflected fields into a JSON object.
        ///
        /// Fields are written in declaration order, so saving an unmodified scene
        /// reproduces the file byte for byte.
        [[nodiscard]] JsonValue Serialize(const ComponentTypeInfo& info, const void* component) const;

        /// Populates a component's reflected fields from a JSON object.
        ///
        /// Unknown fields are ignored and missing fields keep their current value,
        /// so a scene written by a newer engine version still loads.
        void DeserializeInto(const ComponentTypeInfo& info, void* component, const JsonValue& fields) const;

        /// Type-safe front end for `Serialize`.
        template <typename T>
        [[nodiscard]] JsonValue Serialize(const ComponentTypeInfo& info, const T& component) const
        {
            return Serialize(info, static_cast<const void*>(&component));
        }

        /// Type-safe front end for `DeserializeInto`.
        template <typename T>
        void DeserializeInto(const ComponentTypeInfo& info, T* component, const JsonValue& fields) const
        {
            DeserializeInto(info, static_cast<void*>(component), fields);
        }

        /// Registers a type and returns its id. Called by `RegisterComponent`.
        ///
        /// Public so that the registration template below can reach it, but not
        /// part of the engine's own API: use `EMBER_COMPONENT` instead.
        ComponentTypeId RegisterImpl(std::type_index type,
                                     std::string_view name,
                                     std::size_t size,
                                     std::size_t alignment,
                                     std::vector<Property> properties,
                                     std::function<std::unique_ptr<IComponentStorage>(const ComponentTypeInfo&)> createStorage);

    private:
        std::vector<ComponentTypeInfo> m_Types;
        std::unordered_map<std::type_index, std::size_t> m_IndexByType;
        std::unordered_map<std::string, ComponentTypeId> m_IndexByName;
    };
}

namespace Ember::Ecs
{
    /// Registers `T` with the global registry and returns its id.
    ///
    /// Called by the `EMBER_COMPONENT` macro. Defined in ComponentStorage.h,
    /// which is where the storage factory it installs becomes instantiable.
    template <typename T>
    ComponentTypeId RegisterComponent(std::string_view name, std::vector<Property> properties);

    /// Returns the registered id for `T`, or InvalidComponentType if unregistered.
    template <typename T>
    [[nodiscard]] ComponentTypeId GetComponentTypeId() noexcept
    {
        return Ember::ComponentRegistry::Get().TypeId<T>();
    }
}
