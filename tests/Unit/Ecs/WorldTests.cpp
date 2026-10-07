#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Core/Json.h"
#include "Ecs/ComponentRegistry.h"
#include "Ecs/ComponentStorage.h"
#include "Ecs/Components.h"
#include "Ecs/World.h"

using namespace Ember;

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
}

// ------------------------------------------------------------------- lifecycle

TEST(WorldTest, StartsEmpty)
{
    const World world;

    EXPECT_EQ(world.GetEntityCount(), 0u);
    EXPECT_TRUE(world.GetEntities().empty());
}

TEST(WorldTest, CreateEntityReturnsLiveHandle)
{
    World world;
    const Entity entity = world.CreateEntity();

    EXPECT_TRUE(entity.IsValid());
    EXPECT_TRUE(world.IsAlive(entity));
    EXPECT_EQ(world.GetEntityCount(), 1u);
}

TEST(WorldTest, EntitiesAreDistinct)
{
    World world;
    const Entity first = world.CreateEntity();
    const Entity second = world.CreateEntity();

    EXPECT_NE(first, second);
    EXPECT_EQ(world.GetEntityCount(), 2u);
}

TEST(WorldTest, DestroyEntityInvalidatesItsHandle)
{
    World world;
    const Entity entity = world.CreateEntity();

    EXPECT_TRUE(world.DestroyEntity(entity));
    EXPECT_FALSE(world.IsAlive(entity));
    EXPECT_EQ(world.GetEntityCount(), 0u);
}

TEST(WorldTest, DestroyingTwiceReportsFailure)
{
    World world;
    const Entity entity = world.CreateEntity();

    EXPECT_TRUE(world.DestroyEntity(entity));
    EXPECT_FALSE(world.DestroyEntity(entity));
}

TEST(WorldTest, DestroyingAStaleHandleDoesNotAffectTheRecycledSlot)
{
    World world;
    const Entity original = world.CreateEntity();
    world.DestroyEntity(original);

    // The slot is recycled; the old handle must not resolve to the new occupant.
    const Entity recycled = world.CreateEntity();

    EXPECT_EQ(recycled.Index, original.Index);
    EXPECT_NE(recycled, original);
    EXPECT_FALSE(world.IsAlive(original));
    EXPECT_TRUE(world.IsAlive(recycled));
}

TEST(WorldTest, GetEntitiesReturnsLiveEntitiesInOrder)
{
    World world;
    const Entity first = world.CreateEntity();
    const Entity second = world.CreateEntity();
    world.DestroyEntity(first);
    const Entity third = world.CreateEntity();

    // Ordering is by entity index, not creation order: `third` reuses the slot
    // freed by `first` and therefore sorts ahead of `second`.
    const std::vector<Entity> entities = world.GetEntities();

    ASSERT_EQ(entities.size(), 2u);
    EXPECT_EQ(entities[0], third);
    EXPECT_EQ(entities[1], second);
}

TEST(WorldTest, ClearRemovesEveryEntityButKeepsRegistrations)
{
    World world;
    world.CreateEntity();
    world.CreateEntity();

    world.Clear();

    EXPECT_EQ(world.GetEntityCount(), 0u);
    EXPECT_TRUE(world.GetEntities().empty());
    EXPECT_NE(ComponentRegistry::Get().FindByName("Transform"), InvalidComponentType);
}

TEST(WorldTest, SurvivesManyCreateDestroyCycles)
{
    World world;

    for (int i = 0; i < 1000; ++i)
    {
        const Entity entity = world.CreateEntity();
        world.AddComponent(entity, TransformComponent{});
        EXPECT_TRUE(world.DestroyEntity(entity));
    }

    EXPECT_EQ(world.GetEntityCount(), 0u);
}

// ---------------------------------------------------------------------- naming

TEST(WorldTest, NamesRoundTrip)
{
    World world;
    const Entity entity = world.CreateEntity("Player");

    EXPECT_EQ(world.GetName(entity), "Player");
}

TEST(WorldTest, UnnamedEntityHasEmptyName)
{
    World world;
    const Entity entity = world.CreateEntity();

    EXPECT_TRUE(world.GetName(entity).empty());
}

TEST(WorldTest, NameOfStaleHandleIsEmpty)
{
    World world;
    const Entity entity = world.CreateEntity("Player");
    world.DestroyEntity(entity);

    EXPECT_TRUE(world.GetName(entity).empty());
}

TEST(WorldTest, DuplicateNamesAreAllowed)
{
    World world;
    const Entity first = world.CreateEntity("Cube");
    const Entity second = world.CreateEntity("Cube");

    EXPECT_EQ(world.GetName(first), world.GetName(second));
}

// -------------------------------------------------------------------- enabling

TEST(WorldTest, EntitiesStartEnabled)
{
    World world;
    const Entity entity = world.CreateEntity();

    EXPECT_TRUE(world.IsEnabled(entity));
    EXPECT_TRUE(world.IsActiveInHierarchy(entity));
}

TEST(WorldTest, SetEnabledTogglesTheEntitiesOwnFlag)
{
    World world;
    const Entity entity = world.CreateEntity();

    world.SetEnabled(entity, false);

    EXPECT_FALSE(world.IsEnabled(entity));
    EXPECT_FALSE(world.IsActiveInHierarchy(entity));
}

TEST(WorldTest, DisablingAParentDisablesItsSubtree)
{
    World world;
    const Entity parent = world.CreateEntity("Parent");
    const Entity child = world.CreateEntity("Child");
    world.SetParent(child, parent);

    world.SetEnabled(parent, false);

    // The child's own flag is untouched, but it is not active in the hierarchy.
    EXPECT_TRUE(world.IsEnabled(child));
    EXPECT_FALSE(world.IsActiveInHierarchy(child));
}

TEST(WorldTest, ReenablingAParentRestoresAnActiveChild)
{
    World world;
    const Entity parent = world.CreateEntity();
    const Entity child = world.CreateEntity();
    world.SetParent(child, parent);

    world.SetEnabled(parent, false);
    world.SetEnabled(parent, true);

    EXPECT_TRUE(world.IsActiveInHierarchy(child));
}

TEST(WorldTest, ReenablingAParentDoesNotOverrideAChildDisabledOnItsOwn)
{
    World world;
    const Entity parent = world.CreateEntity();
    const Entity child = world.CreateEntity();
    world.SetParent(child, parent);

    world.SetEnabled(child, false);
    world.SetEnabled(parent, false);
    world.SetEnabled(parent, true);

    EXPECT_FALSE(world.IsActiveInHierarchy(child));
}

// ------------------------------------------------------------------ hierarchy

TEST(WorldTest, NewEntitiesAreRoots)
{
    World world;
    const Entity entity = world.CreateEntity();

    EXPECT_EQ(world.GetParent(entity), Entity::Null());
    EXPECT_TRUE(world.GetChildren(entity).empty());
}

TEST(WorldTest, SetParentLinksBothDirections)
{
    World world;
    const Entity parent = world.CreateEntity("Parent");
    const Entity child = world.CreateEntity("Child");

    world.SetParent(child, parent);

    EXPECT_EQ(world.GetParent(child), parent);
    ASSERT_EQ(world.GetChildren(parent).size(), 1u);
    EXPECT_EQ(world.GetChildren(parent)[0], child);
}

TEST(WorldTest, SettingParentToNullMakesARoot)
{
    World world;
    const Entity parent = world.CreateEntity();
    const Entity child = world.CreateEntity();
    world.SetParent(child, parent);

    world.SetParent(child, Entity::Null());

    EXPECT_EQ(world.GetParent(child), Entity::Null());
    EXPECT_TRUE(world.GetChildren(parent).empty());
}

TEST(WorldTest, ReparentingMovesTheChild)
{
    World world;
    const Entity first = world.CreateEntity();
    const Entity second = world.CreateEntity();
    const Entity child = world.CreateEntity();

    world.SetParent(child, first);
    world.SetParent(child, second);

    EXPECT_TRUE(world.GetChildren(first).empty());
    ASSERT_EQ(world.GetChildren(second).size(), 1u);
    EXPECT_EQ(world.GetChildren(second)[0], child);
}

TEST(WorldTest, SelfParentingIsRejected)
{
    World world;
    const Entity entity = world.CreateEntity();

    world.SetParent(entity, entity);

    EXPECT_EQ(world.GetParent(entity), Entity::Null());
}

TEST(WorldTest, CycleIsRejected)
{
    World world;
    const Entity grandparent = world.CreateEntity();
    const Entity parent = world.CreateEntity();
    const Entity child = world.CreateEntity();
    world.SetParent(parent, grandparent);
    world.SetParent(child, parent);

    world.SetParent(grandparent, child);

    // The rejected call must leave the original structure intact.
    EXPECT_EQ(world.GetParent(grandparent), Entity::Null());
    EXPECT_EQ(world.GetParent(parent), grandparent);
    EXPECT_EQ(world.GetParent(child), parent);
}

TEST(WorldTest, SiblingCycleIsRejected)
{
    World world;
    const Entity first = world.CreateEntity();
    const Entity second = world.CreateEntity();
    world.SetParent(second, first);

    world.SetParent(first, second);

    EXPECT_EQ(world.GetParent(first), Entity::Null());
}

TEST(WorldTest, DescendantsAreReturnedBreadthFirst)
{
    World world;
    const Entity root = world.CreateEntity();
    const Entity childA = world.CreateEntity();
    const Entity childB = world.CreateEntity();
    const Entity grandchild = world.CreateEntity();
    world.SetParent(childA, root);
    world.SetParent(childB, root);
    world.SetParent(grandchild, childA);

    const std::vector<Entity> descendants = world.GetDescendants(root);

    ASSERT_EQ(descendants.size(), 3u);
    EXPECT_NE(std::find(descendants.begin(), descendants.end(), childA), descendants.end());
    EXPECT_NE(std::find(descendants.begin(), descendants.end(), childB), descendants.end());
    EXPECT_NE(std::find(descendants.begin(), descendants.end(), grandchild), descendants.end());
}

TEST(WorldTest, DescendantsDoNotIncludeTheRoot)
{
    World world;
    const Entity root = world.CreateEntity();
    const Entity child = world.CreateEntity();
    world.SetParent(child, root);

    EXPECT_EQ(world.GetSubtree(root).size(), 2u);
    EXPECT_EQ(world.GetDescendants(root).size(), 1u);
    EXPECT_EQ(world.GetDescendants(root)[0], child);
}

TEST(WorldTest, LeafHasNoDescendants)
{
    World world;
    const Entity entity = world.CreateEntity();

    EXPECT_TRUE(world.GetDescendants(entity).empty());
}

TEST(WorldTest, DestroyingAParentDestroysItsSubtree)
{
    World world;
    const Entity root = world.CreateEntity();
    const Entity child = world.CreateEntity();
    const Entity grandchild = world.CreateEntity();
    world.SetParent(child, root);
    world.SetParent(grandchild, child);

    EXPECT_TRUE(world.DestroyEntity(root));

    EXPECT_FALSE(world.IsAlive(root));
    EXPECT_FALSE(world.IsAlive(child));
    EXPECT_FALSE(world.IsAlive(grandchild));
    EXPECT_EQ(world.GetEntityCount(), 0u);
}

TEST(WorldTest, DestroyingAParentClearsItsChildList)
{
    World world;
    const Entity root = world.CreateEntity();
    const Entity child = world.CreateEntity();
    world.SetParent(child, root);

    world.DestroyEntity(root);

    // The recycled slot must not inherit the destroyed subtree's children.
    const Entity recycled = world.CreateEntity();
    EXPECT_TRUE(world.GetChildren(recycled).empty());
}

// ------------------------------------------------------------------ components

TEST(WorldTest, AddComponentStoresAValue)
{
    World world;
    const Entity entity = world.CreateEntity();

    TransformComponent transform;
    transform.Position = Vec3(1.0f, 2.0f, 3.0f);

    ASSERT_NE(world.AddComponent(entity, transform), nullptr);
    EXPECT_TRUE(world.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(world.GetComponent<TransformComponent>(entity).Position,
                                           Vec3(1.0f, 2.0f, 3.0f), 1e-6f)));
}

TEST(WorldTest, AddComponentDefaultsWhenNoValueIsGiven)
{
    World world;
    const Entity entity = world.CreateEntity();

    world.AddComponent<TransformComponent>(entity);

    ASSERT_TRUE(world.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(world.GetComponent<TransformComponent>(entity).Scale,
                                           Vec3(1.0f), 1e-6f)));
}

TEST(WorldTest, AddComponentOverwritesAnExistingOne)
{
    World world;
    const Entity entity = world.CreateEntity();

    world.AddComponent(entity, MeshComponent{MeshComponent{.Visible = true}});
    MeshComponent second;
    second.Visible = false;
    world.AddComponent(entity, second);

    EXPECT_FALSE(world.GetComponent<MeshComponent>(entity).Visible);
    EXPECT_EQ(world.GetComponentCount<MeshComponent>(), 1u);
}

TEST(WorldTest, RemoveComponent)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});

    EXPECT_TRUE(world.RemoveComponent<TransformComponent>(entity));
    EXPECT_FALSE(world.HasComponent<TransformComponent>(entity));
    EXPECT_FALSE(world.RemoveComponent<TransformComponent>(entity));
}

TEST(WorldTest, ComponentsAreIndependentPerEntity)
{
    World world;
    const Entity first = world.CreateEntity();
    const Entity second = world.CreateEntity();

    TransformComponent transform;
    transform.Position = Vec3(5.0f);
    world.AddComponent(first, transform);

    EXPECT_TRUE(world.HasComponent<TransformComponent>(first));
    EXPECT_FALSE(world.HasComponent<TransformComponent>(second));
}

TEST(WorldTest, StaleHandleHasNoComponents)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});
    world.DestroyEntity(entity);

    EXPECT_FALSE(world.HasComponent<TransformComponent>(entity));
    EXPECT_EQ(world.TryGetComponent<TransformComponent>(entity), nullptr);
    EXPECT_EQ(world.AddComponent(entity, TransformComponent{}), nullptr);
    EXPECT_FALSE(world.RemoveComponent<TransformComponent>(entity));
}

TEST(WorldTest, GetComponentFallsBackWhenAbsent)
{
    World world;
    const Entity entity = world.CreateEntity();

    TransformComponent fallback;
    fallback.Position = Vec3(7.0f);

    const TransformComponent result = world.GetComponent(entity, fallback);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(result.Position, Vec3(7.0f), 1e-6f)));
}

TEST(WorldTest, DestroyingAnEntityRemovesItsComponents)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});
    world.AddComponent(entity, MeshComponent{});

    EXPECT_EQ(world.GetComponentCount<TransformComponent>(), 1u);

    world.DestroyEntity(entity);

    EXPECT_EQ(world.GetComponentCount<TransformComponent>(), 0u);
    EXPECT_EQ(world.GetComponentCount<MeshComponent>(), 0u);
}

TEST(WorldTest, DestroyingOneEntityLeavesOthersComponentsIntact)
{
    World world;
    const Entity first = world.CreateEntity();
    const Entity second = world.CreateEntity();
    world.AddComponent(first, TransformComponent{});
    world.AddComponent(second, TransformComponent{});

    world.DestroyEntity(first);

    EXPECT_EQ(world.GetComponentCount<TransformComponent>(), 1u);
    EXPECT_TRUE(world.HasComponent<TransformComponent>(second));
}

TEST(WorldTest, ManyEntitiesWithComponents)
{
    World world;

    for (int i = 0; i < 500; ++i)
    {
        const Entity entity = world.CreateEntity();
        TransformComponent transform;
        transform.Position = Vec3(static_cast<float>(i));
        world.AddComponent(entity, transform);
    }

    EXPECT_EQ(world.GetComponentCount<TransformComponent>(), 500u);

    int visited = 0;
    world.Each<TransformComponent>([&](const TransformComponent&, Entity) { ++visited; });
    EXPECT_EQ(visited, 500);
}

// -------------------------------------------------------- type-erased access

TEST(WorldTest, AccessComponentByTypeId)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});

    const ComponentTypeId type = ComponentRegistry::Get().FindByName("Transform");

    ASSERT_NE(ComponentRegistry::Get().FindByName("Transform"), InvalidComponentType);
    EXPECT_TRUE(world.HasComponentByType(entity, type));

    const void* component = world.GetComponentByType(entity, type);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(static_cast<const TransformComponent*>(component)->Scale.x, 1.0f);
}

TEST(WorldTest, TypeErasedAccessOnStaleHandle)
{
    World world;
    const Entity entity = world.CreateEntity();
    const ComponentTypeId type = ComponentRegistry::Get().FindByName("Transform");
    world.DestroyEntity(entity);

    EXPECT_EQ(world.GetComponentByType(entity, type), nullptr);
    EXPECT_FALSE(world.HasComponentByType(entity, type));
}

TEST(WorldTest, RemoveComponentByTypeId)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});
    const ComponentTypeId type = ComponentRegistry::Get().FindByName("Transform");

    EXPECT_TRUE(world.RemoveComponentByType(entity, type));
    EXPECT_FALSE(world.RemoveComponentByType(entity, type));
}

TEST(WorldTest, GetComponentTypesListsWhatIsPresent)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});
    world.AddComponent(entity, CameraComponent{});

    const std::vector<ComponentTypeId> types = world.GetComponentTypes(entity);

    EXPECT_EQ(types.size(), 2u);
    EXPECT_NE(std::find(types.begin(), types.end(), ComponentRegistry::Get().FindByName("Transform")), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), ComponentRegistry::Get().FindByName("Camera")), types.end());
}

TEST(WorldTest, GetComponentTypesOnEntityWithNone)
{
    World world;
    const Entity entity = world.CreateEntity();

    EXPECT_TRUE(world.GetComponentTypes(entity).empty());
}

TEST(WorldTest, ComponentToJsonRoundTripsThroughTheWorld)
{
    World world;
    const Entity entity = world.CreateEntity();

    TransformComponent transform;
    transform.Position = Vec3(1.0f, 2.0f, 3.0f);
    transform.Scale = Vec3(2.0f);
    world.AddComponent(entity, transform);

    const ComponentTypeId type = ComponentRegistry::Get().FindByName("Transform");
    const JsonValue json = world.ComponentToJson(entity, type);

    EXPECT_TRUE(json["Position"].IsArray());
    EXPECT_EQ(json["Position"].Size(), 3u);

    World other;
    const Entity otherEntity = other.CreateEntity();
    ASSERT_TRUE(other.AddComponentFromJson(otherEntity, "Transform", json).IsSuccess());

    EXPECT_TRUE(glm::all(glm::epsilonEqual(other.GetComponent<TransformComponent>(otherEntity).Position,
                                           Vec3(1.0f, 2.0f, 3.0f), 1e-6f)));
}

TEST(WorldTest, AddComponentFromJsonRejectsUnknownType)
{
    World world;
    const Entity entity = world.CreateEntity();

    const Result<void> result = world.AddComponentFromJson(entity, "NotAComponent", JsonValue());

    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::NotFound);
}

TEST(WorldTest, AddComponentFromJsonRejectsStaleHandle)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.DestroyEntity(entity);

    const Result<void> result = world.AddComponentFromJson(entity, "Transform", JsonValue());

    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::InvalidArgument);
}

// -------------------------------------------------------------------- iteration

TEST(WorldTest, EachVisitsEveryMatchingEntity)
{
    World world;
    world.AddComponent<TransformComponent>(world.CreateEntity());
    world.AddComponent<TransformComponent>(world.CreateEntity());
    world.AddComponent<MeshComponent>(world.CreateEntity());

    int count = 0;
    world.Each<TransformComponent>([&](const TransformComponent&, Entity) { ++count; });

    EXPECT_EQ(count, 2);
}

TEST(WorldTest, EachSkipsComponentsOfDestroyedEntities)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});
    world.DestroyEntity(entity);

    int count = 0;
    world.Each<TransformComponent>([&](const TransformComponent&, Entity) { ++count; });

    EXPECT_EQ(count, 0);
}

TEST(WorldTest, EachCanMutateComponents)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});

    world.Each<TransformComponent>([](TransformComponent& transform, Entity)
    {
        transform.Position = Vec3(9.0f);
    });

    EXPECT_FLOAT_EQ(world.GetComponent<TransformComponent>(entity).Position.x, 9.0f);
}

TEST(WorldTest, EachWithRequiresEveryComponent)
{
    World world;
    const Entity both = world.CreateEntity();
    world.AddComponent(both, TransformComponent{});
    world.AddComponent<MeshComponent>(both);
    world.AddComponent<TransformComponent>(world.CreateEntity());

    int count = 0;
    world.EachWith<TransformComponent, MeshComponent>(
        [&](TransformComponent&, Entity, MeshComponent&) { ++count; });

    EXPECT_EQ(count, 1);
}

TEST(WorldTest, EachWithCanReadTheAdditionalComponents)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});

    MeshComponent mesh;
    mesh.MeshId = 12;
    world.AddComponent(entity, mesh);

    std::uint32_t observed = 0;
    world.EachWith<TransformComponent, MeshComponent>(
        [&](TransformComponent&, Entity, MeshComponent& found) { observed = found.MeshId; });

    EXPECT_EQ(observed, 12u);
}

TEST(WorldTest, RawStorageReflectsTheWorld)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});

    EXPECT_EQ(world.GetStorage<TransformComponent>().Size(), 1u);
    EXPECT_EQ(world.GetComponentCount<TransformComponent>(), 1u);
}

// ------------------------------------------------------------------- transform

TEST(TransformComponentTest, DefaultsAreIdentity)
{
    const TransformComponent transform;

    EXPECT_TRUE(glm::all(glm::epsilonEqual(transform.Position, Vec3(0.0f), 1e-6f)));
    EXPECT_NEAR(std::abs(glm::dot(transform.Rotation, Quat(1.0f, 0.0f, 0.0f, 0.0f))), 1.0f, 1e-6f);
    EXPECT_TRUE(glm::all(glm::epsilonEqual(transform.Scale, Vec3(1.0f), 1e-6f)));
}

TEST(TransformComponentTest, ToMatrixMatchesCompose)
{
    TransformComponent transform;
    transform.Position = Vec3(1.0f, 2.0f, 3.0f);
    transform.Scale = Vec3(2.0f);

    const Mat4 expected = Math::ComposeTransform(transform.Position, transform.Rotation, transform.Scale);
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(transform.ToMatrix()[column][row], expected[column][row], 1e-5f);
        }
    }
}

TEST(TransformComponentTest, FromMatrixIsTheInverseOfToMatrix)
{
    TransformComponent original;
    original.Position = Vec3(1.0f, -2.0f, 3.0f);
    original.Rotation = glm::normalize(Quat(glm::angleAxis(ToRadians(40.0f), Vec3(0.0f, 1.0f, 0.0f))));
    original.Scale = Vec3(2.0f, 2.0f, 2.0f);

    TransformComponent restored;
    restored.FromMatrix(original.ToMatrix());

    EXPECT_TRUE(glm::all(glm::epsilonEqual(restored.Position, original.Position, 1e-4f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(restored.Scale, original.Scale, 1e-4f)));
    EXPECT_NEAR(std::abs(glm::dot(restored.Rotation, original.Rotation)), 1.0f, 1e-4f);
}
