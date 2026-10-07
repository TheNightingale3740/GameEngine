#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Core/Json.h"
#include "Ecs/ComponentRegistry.h"
#include "Ecs/Components.h"
#include "Ecs/Entity.h"
#include "Ecs/World.h"

using namespace Ember;

// Every test needs the built-in component set. Registration is explicit because
// the definitions live in a static library and would otherwise be dropped by the
// linker in a build that never references them.
namespace
{
    /// Registers the built-in component set exactly once per process.
    ///
    /// Registration is an explicit call because the definitions live in a static
    /// library, where the linker would otherwise drop an object file that nothing
    /// references.
    struct BuiltinComponentRegistration
    {
        BuiltinComponentRegistration()
        {
            Ember::Ecs::RegisterBuiltinComponents();
        }
    };

    const BuiltinComponentRegistration s_BuiltinComponentRegistration;

    JsonValue ParseOrDie(std::string_view text)
    {
        Result<JsonValue> result = Json::Parse(text);
        EXPECT_TRUE(result.IsSuccess()) << (result.IsFailure() ? result.GetError().Message : std::string());
        return result.ValueOr(JsonValue());
    }

    /// A component declared only for these tests, to exercise registration
    /// without depending on the built-in set.
    struct TestMarkerComponent
    {
        std::int32_t Value = 0;
    };
}

EMBER_COMPONENT(TestMarkerComponent, "TestMarker",
    EMBER_FIELD(TestMarkerComponent, std::int32_t, Value))

namespace
{
    /// A second test component with no fields, to check empty reflection lists.
    struct EmptyComponent
    {
    };
}

EMBER_COMPONENT(EmptyComponent, "Empty")

// ---------------------------------------------------------------- entity handles

TEST(EntityTest, DefaultHandleIsNull)
{
    const Entity entity;

    EXPECT_FALSE(entity.IsValid());
    EXPECT_EQ(entity, Entity::Null());
}

TEST(EntityTest, HandleEqualityComparesIndexAndGeneration)
{
    const Entity first(3, 1);
    const Entity sameAsFirst(3, 1);
    const Entity otherGeneration(3, 2);
    const Entity otherIndex(4, 1);

    EXPECT_EQ(first, sameAsFirst);
    EXPECT_NE(first, otherGeneration);
    EXPECT_NE(first, otherIndex);
}

TEST(EntityTest, HandlesOrderByIndexThenGeneration)
{
    EXPECT_LT(Entity(1, 99), Entity(2, 1));
    EXPECT_LT(Entity(2, 1), Entity(2, 2));
}

TEST(EntityTest, ToStringNamesValidAndNullHandles)
{
    EXPECT_EQ(ToString(Entity(5, 2)), "Entity(5v2)");
    EXPECT_EQ(ToString(Entity::Null()), "Entity(Null)");
}

// ------------------------------------------------------------------- registry

TEST(ComponentRegistryTest, BuiltInComponentsAreRegistered)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();

    EXPECT_NE(registry.FindByName("Transform"), InvalidComponentType);
    EXPECT_NE(registry.FindByName("Mesh"), InvalidComponentType);
    EXPECT_NE(registry.FindByName("Light"), InvalidComponentType);
    EXPECT_NE(registry.FindByName("Camera"), InvalidComponentType);
    EXPECT_NE(registry.FindByName("Collider"), InvalidComponentType);
    EXPECT_NE(registry.FindByName("AudioSource"), InvalidComponentType);
    EXPECT_NE(registry.FindByName("Script"), InvalidComponentType);
    EXPECT_NE(registry.FindByName("Prefab"), InvalidComponentType);
}

TEST(ComponentRegistryTest, UnknownNameResolvesToInvalid)
{
    EXPECT_EQ(ComponentRegistry::Get().FindByName("NoSuchComponent"), InvalidComponentType);
    EXPECT_EQ(ComponentRegistry::Get().FindByName(""), InvalidComponentType);
}

TEST(ComponentRegistryTest, FindByNameAndByTypeAgree)
{
    const ComponentTypeId type = ComponentRegistry::Get().FindByName("Transform");
    const ComponentTypeInfo* info = ComponentRegistry::Get().Find(type);

    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->Name, "Transform");
    EXPECT_EQ(info->Size, sizeof(TransformComponent));
    EXPECT_EQ(ComponentRegistry::Get().Find<TransformComponent>(), info);
}

TEST(ComponentRegistryTest, FindRejectsOutOfRangeId)
{
    EXPECT_EQ(ComponentRegistry::Get().Find(InvalidComponentType), nullptr);
}

TEST(ComponentRegistryTest, ReflectsDeclaredFieldsInOrder)
{
    const ComponentTypeInfo* info = ComponentRegistry::Get().Find(ComponentRegistry::Get().FindByName("Transform"));

    ASSERT_NE(info, nullptr);
    ASSERT_EQ(info->Properties.size(), 3u);
    EXPECT_EQ(info->Properties[0].Name, "Position");
    EXPECT_EQ(info->Properties[1].Name, "Rotation");
    EXPECT_EQ(info->Properties[2].Name, "Scale");

    EXPECT_EQ(info->Properties[0].Type, PropertyType::Vec3);
    EXPECT_EQ(info->Properties[1].Type, PropertyType::Quat);
    EXPECT_EQ(info->Properties[2].Type, PropertyType::Vec3);
}

TEST(ComponentRegistryTest, FieldOffsetsMatchRealLayout)
{
    const ComponentTypeInfo* info = ComponentRegistry::Get().Find(ComponentRegistry::Get().FindByName("Transform"));

    ASSERT_NE(info, nullptr);

    EXPECT_EQ(info->Properties[0].Offset, offsetof(TransformComponent, Position));
    EXPECT_EQ(info->Properties[1].Offset, offsetof(TransformComponent, Rotation));
    EXPECT_EQ(info->Properties[2].Offset, offsetof(TransformComponent, Scale));
}

TEST(ComponentRegistryTest, FindsPropertiesByName)
{
    const ComponentTypeInfo* info = ComponentRegistry::Get().Find(ComponentRegistry::Get().FindByName("Camera"));

    ASSERT_NE(info, nullptr);
    ASSERT_NE(info->FindProperty("NearPlane"), nullptr);
    EXPECT_EQ(info->FindProperty("NearPlane")->Type, PropertyType::Float);
    EXPECT_EQ(info->FindProperty("NoSuchField"), nullptr);
}

TEST(ComponentRegistryTest, EnumPropertiesCarryTheirSpellings)
{
    const ComponentTypeInfo* info = ComponentRegistry::Get().Find(ComponentRegistry::Get().FindByName("Light"));

    ASSERT_NE(info, nullptr);
    const Property* lightType = info->FindProperty("LightType");

    ASSERT_NE(lightType, nullptr);
    EXPECT_EQ(lightType->Type, PropertyType::Enum);
    ASSERT_EQ(lightType->EnumValues.size(), 3u);
    EXPECT_EQ(lightType->EnumValues[0], "Directional");
    EXPECT_EQ(lightType->EnumValues[1], "Point");
    EXPECT_EQ(lightType->EnumValues[2], "Spot");
}

TEST(ComponentRegistryTest, EmptyComponentHasNoProperties)
{
    const ComponentTypeInfo* info = ComponentRegistry::Get().Find(ComponentRegistry::Get().FindByName("Empty"));

    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->Properties.empty());
}

TEST(ComponentRegistryTest, ComponentWithNoFieldsStillRegisters)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();

    ASSERT_NE(registry.FindByName("Empty"), InvalidComponentType);
    EXPECT_TRUE(registry.Find(registry.FindByName("Empty"))->Properties.empty());
    EXPECT_EQ(registry.Count(), registry.Types().size());
}

TEST(ComponentRegistryTest, EnumLookupRoundTrips)
{
    const std::vector<std::string> values{"Static", "Dynamic", "Kinematic"};

    EXPECT_EQ(FindEnumValue(values, "Dynamic"), 1);
    EXPECT_EQ(FindEnumValue(values, "Static"), 0);
    EXPECT_EQ(FindEnumValue(values, "Nope"), -1);
    EXPECT_EQ(FindEnumValue(values, ""), -1);

    EXPECT_EQ(FindEnumName(values, 2), "Kinematic");
    EXPECT_TRUE(FindEnumName(values, 3).empty());
    EXPECT_TRUE(FindEnumName(values, -1).empty());
}

// ---------------------------------------------------------------- serialisation

TEST(ComponentSerialisationTest, RoundTripsEveryFieldKind)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();
    const ComponentTypeInfo* info = registry.Find(registry.FindByName("Transform"));

    ASSERT_NE(info, nullptr);

    TransformComponent original;
    original.Position = Vec3(1.5f, -2.0f, 3.25f);
    original.Rotation = glm::normalize(Quat(0.5f, 0.5f, 0.5f, 0.5f));
    original.Scale = Vec3(2.0f, 3.0f, 4.0f);

    const JsonValue json = registry.Serialize(*info, original);
    TransformComponent restored{};
    registry.DeserializeInto(*info, &restored, json);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(restored.Position, original.Position, 1e-6f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(restored.Scale, original.Scale, 1e-6f)));
    EXPECT_NEAR(std::abs(glm::dot(restored.Rotation, original.Rotation)), 1.0f, 1e-6f);
}

TEST(ComponentSerialisationTest, WritesFieldsInDeclarationOrder)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();
    const ComponentTypeInfo* info = registry.Find(registry.FindByName("Camera"));

    ASSERT_NE(info, nullptr);
    const JsonValue json = registry.Serialize(*info, CameraComponent{});

    ASSERT_TRUE(json.IsObject());
    const JsonValue::Object& members = json.AsObject();

    ASSERT_EQ(members.size(), info->Properties.size());
    for (std::size_t i = 0; i < members.size(); ++i)
    {
        EXPECT_EQ(members[i].first, info->Properties[i].Name);
    }
}

TEST(ComponentSerialisationTest, RoundTripsEnumFieldsByName)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();
    const ComponentTypeInfo* info = registry.Find(registry.FindByName("Light"));

    ASSERT_NE(info, nullptr);

    LightComponent original;
    original.LightType = LightComponent::Type::Spot;
    original.CastsShadows = false;
    original.ShadowResolution = 4096;

    const JsonValue json = registry.Serialize(*info, original);
    EXPECT_EQ(json["LightType"].AsString(), "Spot");

    LightComponent restored;
    registry.DeserializeInto(*info, &restored, json);

    EXPECT_EQ(restored.LightType, LightComponent::Type::Spot);
    EXPECT_FALSE(restored.CastsShadows);
    EXPECT_EQ(restored.ShadowResolution, 4096u);
}

TEST(ComponentSerialisationTest, RoundTripsBoolAndStringFields)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();
    const ComponentTypeInfo* info = registry.Find(registry.FindByName("Script"));

    ASSERT_NE(info, nullptr);

    ScriptComponent original;
    original.ScriptPath = "scripts/player.lua";
    original.Enabled = false;

    const JsonValue json = registry.Serialize(*info, original);
    EXPECT_TRUE(json["Enabled"].AsBool() == false);

    ScriptComponent restored;
    registry.DeserializeInto(*info, &restored, json);

    EXPECT_EQ(restored.ScriptPath, "scripts/player.lua");
    EXPECT_FALSE(restored.Enabled);
}

TEST(ComponentSerialisationTest, MissingFieldsKeepCurrentValues)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();
    const ComponentTypeInfo* info = registry.Find(registry.FindByName("Camera"));

    ASSERT_NE(info, nullptr);

    CameraComponent component;
    component.FieldOfViewDegrees = 90.0f;
    component.NearPlane = 0.25f;

    registry.DeserializeInto(*info, &component, ParseOrDie(R"({"FarPlane": 42.0})"));

    EXPECT_FLOAT_EQ(component.FieldOfViewDegrees, 90.0f);
    EXPECT_FLOAT_EQ(component.NearPlane, 0.25f);
    EXPECT_FLOAT_EQ(component.FarPlane, 42.0f);
}

TEST(ComponentSerialisationTest, UnknownEnumValueKeepsCurrentValue)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();
    const ComponentTypeInfo* info = registry.Find(registry.FindByName("Light"));

    ASSERT_NE(info, nullptr);

    LightComponent component;
    component.LightType = LightComponent::Type::Point;

    registry.DeserializeInto(*info, &component, ParseOrDie(R"({"LightType": "Laser"})"));

    EXPECT_EQ(component.LightType, LightComponent::Type::Point);
}

TEST(ComponentSerialisationTest, NonObjectFieldsAreIgnored)
{
    const ComponentRegistry& registry = ComponentRegistry::Get();
    const ComponentTypeInfo* info = registry.Find(registry.FindByName("Camera"));

    ASSERT_NE(info, nullptr);

    CameraComponent component;
    component.FieldOfViewDegrees = 33.0f;

    registry.DeserializeInto(*info, &component, ParseOrDie("[1, 2, 3]"));

    EXPECT_FLOAT_EQ(component.FieldOfViewDegrees, 33.0f);
}

// -------------------------------------------------------------------- storage

TEST(ComponentStorageTest, InsertAndFind)
{
    ComponentStorage<TestMarkerComponent> storage;

    storage.Insert(5, TestMarkerComponent{42});

    ASSERT_NE(storage.Find(5), nullptr);
    EXPECT_EQ(storage.Find(5)->Value, 42);
    EXPECT_EQ(storage.Find(6), nullptr);
    EXPECT_EQ(storage.Size(), 1u);
}

TEST(ComponentStorageTest, InsertOverwrites)
{
    ComponentStorage<TestMarkerComponent> storage;

    storage.Insert(1, TestMarkerComponent{1});
    storage.Insert(1, TestMarkerComponent{2});

    EXPECT_EQ(storage.Size(), 1u);
    EXPECT_EQ(storage.Find(1)->Value, 2);
}

TEST(ComponentStorageTest, RemoveSwapsTheTailIntoTheHole)
{
    ComponentStorage<TestMarkerComponent> storage;

    storage.Insert(1, TestMarkerComponent{10});
    storage.Insert(2, TestMarkerComponent{20});
    storage.Insert(3, TestMarkerComponent{30});

    EXPECT_TRUE(storage.Remove(1));
    EXPECT_EQ(storage.Size(), 2u);
    EXPECT_EQ(storage.Find(1), nullptr);

    // The surviving components must still be reachable, and the sparse index
    // must have been repointed for whatever moved.
    ASSERT_NE(storage.Find(2), nullptr);
    ASSERT_NE(storage.Find(3), nullptr);
    EXPECT_EQ(storage.Find(2)->Value, 20);
    EXPECT_EQ(storage.Find(3)->Value, 30);
}

TEST(ComponentStorageTest, RemoveReportsWhetherPresent)
{
    ComponentStorage<TestMarkerComponent> storage;

    EXPECT_FALSE(storage.Remove(1));
    storage.Insert(1, TestMarkerComponent{});
    EXPECT_TRUE(storage.Remove(1));
    EXPECT_FALSE(storage.Remove(1));
}

TEST(ComponentStorageTest, RemoveOfTheOnlyElement)
{
    ComponentStorage<TestMarkerComponent> storage;

    storage.Insert(7, TestMarkerComponent{1});
    EXPECT_TRUE(storage.Remove(7));

    EXPECT_TRUE(storage.Empty());
    EXPECT_EQ(storage.Find(7), nullptr);
}

TEST(ComponentStorageTest, EntityIndicesTrackDenseOrder)
{
    ComponentStorage<TestMarkerComponent> storage;

    storage.Insert(10, TestMarkerComponent{1});
    storage.Insert(20, TestMarkerComponent{2});

    EXPECT_EQ(storage.GetEntityAt(0), 10u);
    EXPECT_EQ(storage.GetEntityAt(1), 20u);
    EXPECT_EQ(storage.EntityIndices().size(), storage.Size());
}

TEST(ComponentStorageTest, GetEntityAtOutOfRangeIsInvalid)
{
    ComponentStorage<TestMarkerComponent> storage;

    EXPECT_EQ(storage.GetEntityAt(0), Entity::InvalidIndex);
}

TEST(ComponentStorageTest, ClearRemovesEverything)
{
    ComponentStorage<TestMarkerComponent> storage;

    storage.Insert(1, TestMarkerComponent{});
    storage.Insert(2, TestMarkerComponent{});
    storage.Clear();

    EXPECT_TRUE(storage.Empty());
    EXPECT_EQ(storage.Find(1), nullptr);
    EXPECT_EQ(storage.Find(2), nullptr);
}

TEST(ComponentStorageTest, CanReuseAnIndexAfterRemoval)
{
    ComponentStorage<TestMarkerComponent> storage;

    storage.Insert(1, TestMarkerComponent{1});
    storage.Remove(1);
    storage.Insert(1, TestMarkerComponent{2});

    ASSERT_NE(storage.Find(1), nullptr);
    EXPECT_EQ(storage.Find(1)->Value, 2);
    EXPECT_EQ(storage.Size(), 1u);
}
