// Ecs/Reflection.h
//
// Compile-time reflection for component types.
//
// The editor, the serialiser and the script bindings all need to read and write
// a component's fields without the engine knowing the component's layout. This
// header provides that in one place: a component declares its fields once, and
// every consumer derives from the same description.
//
// Reflection is deliberately property-list based rather than
// serialize-by-hand. A component that adds a field must not be able to forget
// to teach the serialiser about it, so serialisation is generated from the
// property list instead of being written separately.

#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "Core/Math/Math.h"

namespace Ember
{
    /// Index of a component type within the registry. Defined in
    /// ComponentRegistry.h; only the declaration is needed to spell the
    /// registration macro.
    using ComponentTypeId = std::uint16_t;

    /// The value shape of a reflected field.
    enum class PropertyType : std::uint8_t
    {
        Bool,
        Int,
        Float,
        Vec2,
        Vec3,
        Vec4,
        Quat,
        String,
        Enum
    };

    /// One reflected field of a component.
    struct Property
    {
        /// Field name as it appears in scene files and to scripts.
        std::string Name;

        /// Value shape of the field.
        PropertyType Type = PropertyType::Float;

        /// Byte offset of the field within the component.
        std::size_t Offset = 0;

        /// For `PropertyType::Enum`, the accepted spellings in declaration order.
        /// The integer value of the field is the index into this list.
        std::vector<std::string> EnumValues;
    };

    /// Maps a C++ type to its reflected property type.
    template <typename T>
    struct PropertyTraits;

    /// Declares the reflected type of a supported component field type.
#define EMBER_DECLARE_PROPERTY_TRAITS(Type, EnumValue)  \
    template <>                                         \
    struct PropertyTraits<Type>                         \
    {                                                   \
        static constexpr PropertyType Value = EnumValue; \
    }

    // Only the fixed-width spellings are registered. Plain `int` and
    // `unsigned int` are intentionally absent: a component's wire format must not
    // depend on how wide the host's `int` happens to be.
    EMBER_DECLARE_PROPERTY_TRAITS(bool, PropertyType::Bool);
    EMBER_DECLARE_PROPERTY_TRAITS(std::int32_t, PropertyType::Int);
    EMBER_DECLARE_PROPERTY_TRAITS(std::uint32_t, PropertyType::Int);
    EMBER_DECLARE_PROPERTY_TRAITS(float, PropertyType::Float);
    EMBER_DECLARE_PROPERTY_TRAITS(Vec2, PropertyType::Vec2);
    EMBER_DECLARE_PROPERTY_TRAITS(Vec3, PropertyType::Vec3);
    EMBER_DECLARE_PROPERTY_TRAITS(Vec4, PropertyType::Vec4);
    EMBER_DECLARE_PROPERTY_TRAITS(Quat, PropertyType::Quat);
    EMBER_DECLARE_PROPERTY_TRAITS(std::string, PropertyType::String);

#undef EMBER_DECLARE_PROPERTY_TRAITS

    /// Builds an enumerated property from its accepted spellings.
    [[nodiscard]] inline Property MakeEnumProperty(std::string name,
                                                  std::size_t offset,
                                                  std::initializer_list<std::string_view> values)
    {
        Property property;
        property.Name = std::move(name);
        property.Type = PropertyType::Enum;
        property.Offset = offset;
        property.EnumValues.reserve(values.size());

        for (const std::string_view value : values)
        {
            property.EnumValues.emplace_back(value);
        }

        return property;
    }

    /// Resolves an enum member name to its integer value, or -1 if unknown.
    [[nodiscard]] int FindEnumValue(const std::vector<std::string>& values, std::string_view name) noexcept;

    /// Resolves an integer value to its enum member name. Unknown values return
    /// an empty string.
    [[nodiscard]] std::string_view FindEnumName(const std::vector<std::string>& values, int value) noexcept;
}


/// Declares a component type and registers it with the global registry.
///
/// Place at namespace scope in the translation unit that defines the component.
/// Every field the component exposes to the editor, to scene files and to scripts
/// is listed; fields left out are invisible to all of them and are not
/// serialised.
///
///     struct TransformComponent { Vec3 Position; Vec3 Scale; };
///     EMBER_COMPONENT(TransformComponent, "Transform",
///         EMBER_FIELD(TransformComponent, Vec3, Position),
///         EMBER_FIELD(TransformComponent, Vec3, Scale))
///
/// Registration runs during static initialisation, before any world exists.
///
/// Every field macro repeats the component type. That is unavoidable: a field
/// macro's arguments are expanded before `EMBER_COMPONENT` substitutes its own
/// parameters, so a field cannot refer to the enclosing component's name.
///
/// Several components may be declared on one line (an aggregate macro does
/// exactly that), so the registration variable is named after `__COUNTER__`,
/// which is unique per expansion rather than per line.
#define EMBER_DETAIL_CONCAT_INNER(Left, Right) Left##Right
#define EMBER_DETAIL_CONCAT(Left, Right) EMBER_DETAIL_CONCAT_INNER(Left, Right)
#define EMBER_DETAIL_EMPTY

#define EMBER_COMPONENT(Type, TypeName, ...)                                                 \
    namespace                                                                                  \
    {                                                                                          \
        [[maybe_unused]] const ::Ember::ComponentTypeId                                       \
            EMBER_DETAIL_CONCAT(s_EmberComponentRegistration, __COUNTER__) =                 \
                ::Ember::Ecs::RegisterComponent<Type>(TypeName, std::vector<::Ember::Property>{__VA_ARGS__}); \
    }

/// Lists one reflected field of a component. Only valid inside `EMBER_COMPONENT`.
///
/// The first argument is the component type, then the field's type and name.
#define EMBER_FIELD(ComponentType, FieldType, FieldName)                              \
    ::Ember::Property{#FieldName,                                                     \
                      ::Ember::PropertyTraits<FieldType>::Value,                      \
                      offsetof(ComponentType, FieldName),                             \
                      {}}

/// Lists one reflected enumerated field. Only valid inside `EMBER_COMPONENT`.
///
/// The second argument is the component type, then the field's name and the
/// accepted enum spellings in declaration order.
#define EMBER_ENUM_FIELD(FieldName, ComponentType, ...)                                        \
    ::Ember::MakeEnumProperty(#FieldName, offsetof(ComponentType, FieldName), {__VA_ARGS__})
