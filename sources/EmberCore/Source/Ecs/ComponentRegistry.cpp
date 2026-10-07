// Ecs/ComponentRegistry.cpp

#include "Ecs/ComponentRegistry.h"

#include <cstring>

#include "Core/Logging/Log.h"
#include "Ecs/ComponentStorage.h"

namespace Ember
{
    namespace
    {
        /// Reads the field at `component + property.Offset` as `FieldType`.
        template <typename FieldType>
        [[nodiscard]] FieldType& FieldAt(void* component, const Property& property) noexcept
        {
            // The offset is `offsetof` of a real field, so the cast is
            // well defined; the intermediate cast to char* is what makes it legal.
            auto* address = static_cast<char*>(component) + property.Offset;
            return *reinterpret_cast<FieldType*>(address);
        }

        template <typename FieldType>
        [[nodiscard]] const FieldType& FieldAt(const void* component, const Property& property) noexcept
        {
            auto* address = static_cast<const char*>(component) + property.Offset;
            return *reinterpret_cast<const FieldType*>(address);
        }

        /// Writes a reflected field into a JSON object.
        void SerialiseProperty(JsonValue& object, const void* component, const Property& property,
                               std::string_view ownerName)
        {
            switch (property.Type)
            {
                case PropertyType::Bool:
                    object.Set(property.Name, JsonValue(FieldAt<bool>(component, property)));
                    break;

                case PropertyType::Int:
                    object.Set(property.Name, JsonValue(static_cast<double>(FieldAt<std::int32_t>(component, property))));
                    break;

                case PropertyType::Float:
                    object.Set(property.Name, JsonValue(static_cast<double>(FieldAt<float>(component, property))));
                    break;

                case PropertyType::Vec2:
                {
                    const Vec2 value = FieldAt<Vec2>(component, property);
                    object.Set(property.Name, JsonValue(JsonValue::Array{JsonValue(static_cast<double>(value.x)), JsonValue(static_cast<double>(value.y))}));
                    break;
                }

                case PropertyType::Vec3:
                {
                    const Vec3 value = FieldAt<Vec3>(component, property);
                    object.Set(property.Name, JsonValue(JsonValue::Array{JsonValue(static_cast<double>(value.x)),
                                                                         JsonValue(static_cast<double>(value.y)),
                                                                         JsonValue(static_cast<double>(value.z))}));
                    break;
                }

                case PropertyType::Vec4:
                {
                    const Vec4 value = FieldAt<Vec4>(component, property);
                    object.Set(property.Name, JsonValue(JsonValue::Array{JsonValue(static_cast<double>(value.x)),
                                                                         JsonValue(static_cast<double>(value.y)),
                                                                         JsonValue(static_cast<double>(value.z)),
                                                                         JsonValue(static_cast<double>(value.w))}));
                    break;
                }

                case PropertyType::Quat:
                {
                    // Quaternions are written x, y, z, w to match the JSON object
                    // layout the reader accepts, which is also the order scripts see.
                    const Quat value = FieldAt<Quat>(component, property);
                    object.Set(property.Name, JsonValue(JsonValue::Array{JsonValue(static_cast<double>(value.x)),
                                                                         JsonValue(static_cast<double>(value.y)),
                                                                         JsonValue(static_cast<double>(value.z)),
                                                                         JsonValue(static_cast<double>(value.w))}));
                    break;
                }

                case PropertyType::String:
                    object.Set(property.Name, JsonValue(FieldAt<std::string>(component, property)));
                    break;

                case PropertyType::Enum:
                {
                    const std::int32_t value = FieldAt<std::int32_t>(component, property);
                    const std::string_view name = FindEnumName(property.EnumValues, value);

                    if (name.empty())
                    {
                        EMBER_LOG_ERROR("Component '{}' field '{}' has value {}, which is not one of its enum names",
                                        ownerName, property.Name, value);
                        object.Set(property.Name, JsonValue(nullptr));
                        break;
                    }

                    object.Set(property.Name, JsonValue(std::string(name)));
                    break;
                }
            }
        }

        /// Reads a reflected field from a JSON object.
        void DeserialiseProperty(void* component, const Property& property, const JsonValue& value,
                                 std::string_view ownerName)
        {
            switch (property.Type)
            {
                case PropertyType::Bool:
                    FieldAt<bool>(component, property) = value.AsBool(FieldAt<bool>(component, property));
                    break;

                case PropertyType::Int:
                    FieldAt<std::int32_t>(component, property) =
                        static_cast<std::int32_t>(value.AsInt(FieldAt<std::int32_t>(component, property)));
                    break;

                case PropertyType::Float:
                    FieldAt<float>(component, property) = value.AsFloat(FieldAt<float>(component, property));
                    break;

                case PropertyType::Vec2:
                    FieldAt<Vec2>(component, property) = value.AsVec2(FieldAt<Vec2>(component, property));
                    break;

                case PropertyType::Vec3:
                    FieldAt<Vec3>(component, property) = value.AsVec3(FieldAt<Vec3>(component, property));
                    break;

                case PropertyType::Vec4:
                    FieldAt<Vec4>(component, property) = value.AsVec4(FieldAt<Vec4>(component, property));
                    break;

                case PropertyType::Quat:
                    FieldAt<Quat>(component, property) = value.AsQuat(FieldAt<Quat>(component, property));
                    break;

                case PropertyType::String:
                    if (value.IsString())
                    {
                        FieldAt<std::string>(component, property) = value.AsString();
                    }
                    break;

                case PropertyType::Enum:
                {
                    if (!value.IsString())
                    {
                        break;
                    }

                    const int resolved = FindEnumValue(property.EnumValues, value.AsString());
                    if (resolved < 0)
                    {
                        EMBER_LOG_WARN("Component '{}' field '{}' has unknown enum value '{}'; keeping the current value",
                                       ownerName, property.Name, value.AsString());
                        break;
                    }

                    FieldAt<std::int32_t>(component, property) = resolved;
                    break;
                }
            }
        }
    }

    const Property* ComponentTypeInfo::FindProperty(std::string_view name) const noexcept
    {
        for (const Property& property : Properties)
        {
            if (property.Name == name)
            {
                return &property;
            }
        }

        return nullptr;
    }

    ComponentRegistry& ComponentRegistry::Get() noexcept
    {
        static ComponentRegistry instance;
        return instance;
    }

    ComponentTypeId ComponentRegistry::RegisterImpl(std::type_index type,
                                                    std::string_view name,
                                                    std::size_t size,
                                                    std::size_t alignment,
                                                    std::vector<Property> properties,
                                                    std::function<std::unique_ptr<IComponentStorage>(const ComponentTypeInfo&)> createStorage)
    {
        // ComponentTypeId is 16 bits, so the registry tops out far below this.
        if (m_Types.size() >= InvalidComponentType)
        {
            EMBER_LOG_FATAL("Component registry is full; cannot register '{}'", name);
            return InvalidComponentType;
        }

        const std::string nameString(name);

        if (const auto existing = m_IndexByName.find(nameString); existing != m_IndexByName.end())
        {
            EMBER_LOG_FATAL("Two component types are both named '{}'; the second registration is ignored",
                            nameString);
            return existing->second;
        }

        // Two fields with the same name would make serialisation ambiguous.
        for (std::size_t i = 0; i < properties.size(); ++i)
        {
            for (std::size_t j = i + 1; j < properties.size(); ++j)
            {
                if (properties[i].Name == properties[j].Name)
                {
                    EMBER_LOG_FATAL("Component '{}' declares the field '{}' twice", nameString, properties[i].Name);
                }
            }
        }

        ComponentTypeInfo info;
        info.Id = static_cast<ComponentTypeId>(m_Types.size());
        info.Name = nameString;
        info.Size = size;
        info.Alignment = alignment;
        info.Properties = std::move(properties);
        info.CreateStorage = std::move(createStorage);

        m_Types.push_back(std::move(info));
        m_IndexByType.emplace(type, m_Types.size() - 1);
        m_IndexByName.emplace(nameString, static_cast<ComponentTypeId>(m_Types.size() - 1));

        return static_cast<ComponentTypeId>(m_Types.size() - 1);
    }

    ComponentTypeId ComponentRegistry::FindByName(std::string_view name) const noexcept
    {
        const auto found = m_IndexByName.find(std::string(name));
        return found == m_IndexByName.end() ? InvalidComponentType : found->second;
    }

    const ComponentTypeInfo* ComponentRegistry::Find(ComponentTypeId id) const noexcept
    {
        return id < m_Types.size() ? &m_Types[id] : nullptr;
    }

    JsonValue ComponentRegistry::Serialize(const ComponentTypeInfo& info, const void* component) const
    {
        JsonValue object(JsonValue::Object{});

        for (const Property& property : info.Properties)
        {
            SerialiseProperty(object, component, property, info.Name);
        }

        return object;
    }

    void ComponentRegistry::DeserializeInto(const ComponentTypeInfo& info, void* component, const JsonValue& fields) const
    {
        if (!fields.IsObject())
        {
            EMBER_LOG_WARN("Component '{}' expects an object of fields but got {}",
                           info.Name, fields.IsNull() ? "null" : "a different type");
            return;
        }

        for (const Property& property : info.Properties)
        {
            const JsonValue* value = fields.Find(property.Name);
            if (value == nullptr)
            {
                // A field missing from the file keeps the component's default.
                continue;
            }

            DeserialiseProperty(component, property, *value, info.Name);
        }

        for (const JsonValue::Member& member : fields.AsObject())
        {
            if (info.FindProperty(member.first) == nullptr)
            {
                EMBER_LOG_WARN("Component '{}' has no field named '{}'; ignoring it", info.Name, member.first);
            }
        }
    }

    int FindEnumValue(const std::vector<std::string>& values, std::string_view name) noexcept
    {
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (values[i] == name)
            {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    std::string_view FindEnumName(const std::vector<std::string>& values, int value) noexcept
    {
        if (value < 0 || static_cast<std::size_t>(value) >= values.size())
        {
            return {};
        }

        return values[static_cast<std::size_t>(value)];
    }
}
