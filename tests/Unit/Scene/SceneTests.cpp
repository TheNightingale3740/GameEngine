#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Core/FileSystem.h"
#include "Core/Json.h"
#include "Ecs/Components.h"
#include "Scene/Scene.h"

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

    /// A temporary directory that removes itself, for the file-based tests.
    class TempDirectory
    {
    public:
        TempDirectory()
        {
            static int counter = 0;
            m_Path = std::filesystem::temp_directory_path() /
                     ("ember-scene-tests-" + std::to_string(++counter));
            std::filesystem::remove_all(m_Path);
            std::filesystem::create_directories(m_Path);
        }

        ~TempDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_Path, error);
        }

        TempDirectory(const TempDirectory&) = delete;
        TempDirectory& operator=(const TempDirectory&) = delete;

        [[nodiscard]] FilePath File(const std::string& name) const { return m_Path / name; }
        [[nodiscard]] const FilePath& Path() const { return m_Path; }

    private:
        FilePath m_Path;
    };

    /// Populates a world with a parent, a transform and one child.
    ///
    /// A world owns its entities and is not copyable, so callers pass in a world
    /// to fill rather than receiving one.
    void PopulateSampleWorld(World& world)
    {
        const Entity parent = world.CreateEntity("Parent");

        TransformComponent parentTransform;
        parentTransform.Position = Vec3(1.0f, 2.0f, 3.0f);
        world.AddComponent(parent, parentTransform);

        const Entity child = world.CreateEntity("Child");
        world.SetParent(child, parent);

        TransformComponent childTransform;
        childTransform.Scale = Vec3(2.0f);
        world.AddComponent(child, childTransform);

        world.AddComponent<MeshComponent>(child);
    }

    /// Builds a small scene: a parent with a transform and one child.
    class SampleWorld
    {
    public:
        SampleWorld()
        {
            PopulateSampleWorld(m_World);
        }

        [[nodiscard]] World& Get() { return m_World; }
        [[nodiscard]] const World& Get() const { return m_World; }

    private:
        World m_World;
    };
}

// ------------------------------------------------------------------- capture

TEST(SceneTest, CaptureOfEmptyWorldIsEmpty)
{
    const World world;

    const Scene scene = Scene::Capture(world);

    EXPECT_TRUE(scene.IsEmpty());
    EXPECT_EQ(scene.GetEntityCount(), 0u);
    EXPECT_TRUE(scene.GetRootIds().empty());
}

TEST(SceneTest, CaptureRecordsNamesAndComponents)
{
    World world;
    const Entity entity = world.CreateEntity("Cube");
    world.AddComponent<MeshComponent>(entity);

    const Scene scene = Scene::Capture(world);

    ASSERT_EQ(scene.GetEntityCount(), 1u);
    EXPECT_EQ(scene.GetEntityIds()[0], "e0");

    const JsonValue& components = scene.GetComponents(0);
    ASSERT_TRUE(components.IsObject());
    EXPECT_TRUE(components["Mesh"].IsObject());
    EXPECT_TRUE(components["Transform"].IsNull());
}

TEST(SceneTest, CaptureRecordsEnabledState)
{
    World world;
    const Entity entity = world.CreateEntity();
    world.SetEnabled(entity, false);

    const Scene scene = Scene::Capture(world);

    ASSERT_EQ(scene.GetEntityCount(), 1u);
    EXPECT_FALSE(scene.GetComponents(0)["Mesh"].IsObject());

    const Result<Scene> reparsed = Scene::FromJson(scene.ToJson());
    ASSERT_TRUE(reparsed.IsSuccess());
}

TEST(SceneTest, CaptureRecordsRoots)
{
    World world;
    const Entity rootA = world.CreateEntity();
    const Entity child = world.CreateEntity();
    world.CreateEntity();
    world.SetParent(child, rootA);

    const Scene scene = Scene::Capture(world);

    EXPECT_EQ(scene.GetRootIds().size(), 2u);
    EXPECT_EQ(scene.GetRootIds()[0], "e0");
    EXPECT_EQ(scene.GetRootIds()[1], "e2");
}

TEST(SceneTest, CapturePreservesHierarchy)
{
    World world;
    const Entity parent = world.CreateEntity();
    const Entity child = world.CreateEntity();
    world.SetParent(child, parent);

    const Scene scene = Scene::Capture(world);
    const Result<Scene> reparsed = Scene::FromJson(scene.ToJson());

    ASSERT_TRUE(reparsed.IsSuccess());

    World restored;
    ASSERT_TRUE(reparsed.Value().LoadInto(restored).IsSuccess());

    const std::vector<Entity> entities = restored.GetEntities();
    ASSERT_EQ(entities.size(), 2u);

    // The parent is written before the child, so the child's parent link resolves.
    EXPECT_EQ(restored.GetParent(entities[1]), entities[0]);
}

// ------------------------------------------------------------------ round trip

TEST(SceneTest, RoundTripsASampleScene)
{
    const Scene scene = Scene::Capture(SampleWorld().Get());

    const Result<Scene> reparsed = Scene::FromJson(scene.ToJson());
    ASSERT_TRUE(reparsed.IsSuccess());
    EXPECT_EQ(reparsed.Value().GetEntityCount(), scene.GetEntityCount());

    World restored;
    ASSERT_TRUE(reparsed.Value().LoadInto(restored).IsSuccess());

    const std::vector<Entity> entities = restored.GetEntities();
    ASSERT_EQ(entities.size(), 2u);

    EXPECT_EQ(restored.GetName(entities[0]), "Parent");
    EXPECT_TRUE(glm::all(glm::epsilonEqual(
        restored.GetComponent<TransformComponent>(entities[0]).Position, Vec3(1.0f, 2.0f, 3.0f), 1e-6f)));

    EXPECT_EQ(restored.GetName(entities[1]), "Child");
    EXPECT_TRUE(glm::all(glm::epsilonEqual(
        restored.GetComponent<TransformComponent>(entities[1]).Scale, Vec3(2.0f), 1e-6f)));
    EXPECT_TRUE(restored.HasComponent<MeshComponent>(entities[1]));

    EXPECT_EQ(restored.GetParent(entities[1]), entities[0]);
}

TEST(SceneTest, SerialisingTwiceProducesIdenticalText)
{
    const Scene scene = Scene::Capture(SampleWorld().Get());

    EXPECT_EQ(scene.ToJson(), scene.ToJson());
}

TEST(SceneTest, RoundTripIsStable)
{
    SampleWorld sample;
    World& world = sample.Get();

    const Scene first = Scene::Capture(world);
    World reloaded;
    ASSERT_TRUE(first.LoadInto(reloaded).IsSuccess());

    const Scene second = Scene::Capture(reloaded);

    EXPECT_EQ(first.ToJson(), second.ToJson());
}

TEST(SceneTest, PreservesDisabledEntities)
{
    World world;
    const Entity entity = world.CreateEntity("Off");
    world.SetEnabled(entity, false);

    const Result<Scene> reparsed = Scene::FromJson(Scene::Capture(world).ToJson());
    ASSERT_TRUE(reparsed.IsSuccess());

    World restored;
    ASSERT_TRUE(reparsed.Value().LoadInto(restored).IsSuccess());

    const Entity restoredEntity = restored.GetEntities().front();
    EXPECT_FALSE(restored.IsEnabled(restoredEntity));
}

TEST(SceneTest, EmptyComponentsObjectRoundTrips)
{
    World world;
    world.CreateEntity("Bare");

    const Scene scene = Scene::Capture(world);
    const Result<Scene> reparsed = Scene::FromJson(scene.ToJson());

    ASSERT_TRUE(reparsed.IsSuccess());

    World restored;
    ASSERT_TRUE(reparsed.Value().LoadInto(restored).IsSuccess());
    EXPECT_EQ(restored.GetComponentTypes(restored.GetEntities().front()).size(), 0u);
}

// ------------------------------------------------------------------ load into

TEST(SceneTest, LoadIntoReplacesExistingEntities)
{
    SampleWorld sample;
    World& world = sample.Get();
    EXPECT_EQ(world.GetEntityCount(), 2u);

    World other;
    other.CreateEntity("Stale");
    ASSERT_TRUE(Scene::Capture(world).LoadInto(other).IsSuccess());

    EXPECT_EQ(other.GetEntityCount(), 2u);
    EXPECT_EQ(other.GetEntities().front().Index, 0u);
}

TEST(SceneTest, LoadIntoAnEmptyWorld)
{
    World world;

    ASSERT_TRUE(Scene::Capture(SampleWorld().Get()).LoadInto(world).IsSuccess());

    EXPECT_EQ(world.GetEntityCount(), 2u);
}

TEST(SceneTest, InstantiateAddsAlongsideExistingEntities)
{
    SampleWorld sample;
    World& world = sample.Get();

    Result<std::vector<Entity>> created = Scene::Capture(world).Instantiate(world);
    ASSERT_TRUE(created.IsSuccess());

    EXPECT_EQ(created.Value().size(), 2u);
    EXPECT_EQ(world.GetEntityCount(), 4u);
}

TEST(SceneTest, InstantiateOfAnEmptySceneCreatesNothing)
{
    World world;
    world.CreateEntity("Existing");

    const Result<std::vector<Entity>> created = Scene::Capture(World{}).Instantiate(world);

    ASSERT_TRUE(created.IsSuccess());
    EXPECT_TRUE(created.Value().empty());
    EXPECT_EQ(world.GetEntityCount(), 1u);
}

// --------------------------------------------------------------- format checks

TEST(SceneTest, DocumentDeclaresVersionAndKind)
{
    const Result<JsonValue> document = Json::Parse(Scene::Capture(SampleWorld().Get()).ToJson());

    ASSERT_TRUE(document.IsSuccess());
    EXPECT_EQ(document.Value()["version"].AsInt(), SceneFormatVersion);
    EXPECT_EQ(document.Value()["kind"].AsString(), SceneFormat::SceneKind);
    EXPECT_TRUE(document.Value()["entities"].IsArray());
}

TEST(SceneTest, RejectsAMissingVersion)
{
    const Result<Scene> scene = Scene::FromJson(R"({"entities": []})");

    ASSERT_TRUE(scene.IsFailure());
    EXPECT_EQ(scene.GetError().Code, ErrorCode::ParseError);
}

TEST(SceneTest, RejectsAnUnsupportedVersion)
{
    const Result<Scene> scene = Scene::FromJson(R"({"version": 9999, "entities": []})");

    ASSERT_TRUE(scene.IsFailure());
    EXPECT_EQ(scene.GetError().Code, ErrorCode::NotSupported);
}

TEST(SceneTest, RejectsAMissingEntitiesArray)
{
    const Result<Scene> scene = Scene::FromJson(R"({"version": 1})");

    ASSERT_TRUE(scene.IsFailure());
    EXPECT_EQ(scene.GetError().Code, ErrorCode::ParseError);
}

TEST(SceneTest, RejectsMalformedJson)
{
    EXPECT_TRUE(Scene::FromJson("{").IsFailure());
    EXPECT_TRUE(Scene::FromJson("not json").IsFailure());
}

TEST(SceneTest, RejectsANonObjectDocument)
{
    EXPECT_TRUE(Scene::FromJson("[]").IsFailure());
    EXPECT_TRUE(Scene::FromJson("42").IsFailure());
}

TEST(SceneTest, RejectsAnEntityWithNoId)
{
    const Result<Scene> scene = Scene::FromJson(R"({"version": 1, "entities": [{"name": "x"}]})");

    ASSERT_TRUE(scene.IsFailure());
    EXPECT_EQ(scene.GetError().Code, ErrorCode::ParseError);
}

TEST(SceneTest, RejectsDuplicateEntityIds)
{
    const Result<Scene> scene = Scene::FromJson(
        R"({"version": 1, "entities": [{"id": "a"}, {"id": "a"}]})");

    ASSERT_TRUE(scene.IsFailure());
    EXPECT_EQ(scene.GetError().Code, ErrorCode::Serialization);
}

TEST(SceneTest, RejectsADanglingParentReference)
{
    const Result<Scene> scene = Scene::FromJson(
        R"({"version": 1, "entities": [{"id": "a", "parent": "missing"}]})");

    ASSERT_TRUE(scene.IsFailure());
    EXPECT_EQ(scene.GetError().Code, ErrorCode::Serialization);
}

TEST(SceneTest, InstantiateRejectsADanglingParentAtRuntime)
{
    // The id lookup is validated on load; this checks the runtime guard by
    // bypassing it through a scene built by hand.
    World world;
    Result<Scene> scene = Scene::FromJson(R"({"version": 1, "entities": [{"id": "a"}]})");
    ASSERT_TRUE(scene.IsSuccess());

    const Result<std::vector<Entity>> created = scene.Value().Instantiate(world);
    EXPECT_TRUE(created.IsSuccess());
}

TEST(SceneTest, RejectsAnUnknownComponentName)
{
    const Result<Scene> scene = Scene::FromJson(
        R"({"version": 1, "entities": [{"id": "a", "components": {"Nonexistent": {}}}]})");

    ASSERT_TRUE(scene.IsSuccess());

    World world;
    const Result<std::vector<Entity>> created = scene.Value().Instantiate(world);

    ASSERT_TRUE(created.IsFailure());
    EXPECT_EQ(created.GetError().Code, ErrorCode::NotFound);
}

TEST(SceneTest, FailedInstantiateLeavesNoEntitiesBehind)
{
    World world;

    const Result<Scene> scene = Scene::FromJson(R"({
        "version": 1,
        "entities": [
            {"id": "a"},
            {"id": "b", "components": {"Nonexistent": {}}}
        ]
    })");
    ASSERT_TRUE(scene.IsSuccess());

    EXPECT_TRUE(scene.Value().Instantiate(world).IsFailure());
    EXPECT_EQ(world.GetEntityCount(), 0u);
}

TEST(SceneTest, FailedLoadIntoKeepsTheExistingScene)
{
    SampleWorld sample;
    World& world = sample.Get();

    const Result<Scene> broken = Scene::FromJson(R"({
        "version": 1,
        "entities": [
            {"id": "a", "components": {"Nonexistent": {}}}
        ]
    })");
    ASSERT_TRUE(broken.IsSuccess());

    EXPECT_TRUE(broken.Value().LoadInto(world).IsFailure());
    EXPECT_EQ(world.GetEntityCount(), 2u);
    EXPECT_EQ(world.GetName(world.GetEntities().front()), "Parent");
}

// --------------------------------------------------------------------- files

TEST(SceneFileTest, SaveAndLoadRoundTrip)
{
    const TempDirectory directory;
    const FilePath path = directory.File("scene.ember");

    ASSERT_TRUE(Scene::Capture(SampleWorld().Get()).SaveToFile(path).IsSuccess());
    ASSERT_TRUE(FileSystem::Exists(path));

    const Result<Scene> loaded = Scene::LoadFromFile(path);
    ASSERT_TRUE(loaded.IsSuccess());
    EXPECT_EQ(loaded.Value().GetEntityCount(), 2u);
}

TEST(SceneFileTest, LoadOfAMissingFileFails)
{
    const TempDirectory directory;

    const Result<Scene> loaded = Scene::LoadFromFile(directory.File("absent.ember"));

    ASSERT_TRUE(loaded.IsFailure());
    EXPECT_EQ(loaded.GetError().Code, ErrorCode::FileNotFound);
}

TEST(SceneFileTest, LoadOfAMalformedFileFails)
{
    const TempDirectory directory;
    const FilePath path = directory.File("broken.ember");
    ASSERT_TRUE(FileSystem::WriteTextFile(path, "{ this is not json").IsSuccess());

    EXPECT_TRUE(Scene::LoadFromFile(path).IsFailure());
}

TEST(SceneFileTest, LoadErrorNamesTheFile)
{
    const TempDirectory directory;
    const FilePath path = directory.File("broken.ember");
    ASSERT_TRUE(FileSystem::WriteTextFile(path, "[]").IsSuccess());

    const Result<Scene> loaded = Scene::LoadFromFile(path);

    ASSERT_TRUE(loaded.IsFailure());
    EXPECT_NE(loaded.GetError().Message.find("broken.ember"), std::string::npos);
}

TEST(SceneFileTest, SaveCreatesMissingDirectories)
{
    const TempDirectory directory;
    const FilePath path = directory.Path() / "nested" / "deeper" / "scene.ember";

    ASSERT_TRUE(Scene::Capture(SampleWorld().Get()).SaveToFile(path).IsSuccess());
    EXPECT_TRUE(FileSystem::Exists(path));
}

// -------------------------------------------------------------------- prefabs

TEST(PrefabTest, SaveAndInstantiateARootEntity)
{
    const TempDirectory directory;
    const FilePath path = directory.File("prefab.ember");

    World source;
    const Entity root = source.CreateEntity("PrefabRoot");
    TransformComponent transform;
    transform.Position = Vec3(4.0f, 5.0f, 6.0f);
    source.AddComponent(root, transform);

    const Entity child = source.CreateEntity("Child");
    source.SetParent(child, root);
    source.AddComponent<MeshComponent>(child);

    ASSERT_TRUE(PrefabFormat::SavePrefab(path, source, root).IsSuccess());

    World target;
    const Result<Entity> instance = PrefabFormat::InstantiatePrefab(path, target);

    ASSERT_TRUE(instance.IsSuccess());
    EXPECT_EQ(target.GetName(instance.Value()), "PrefabRoot");
    EXPECT_EQ(target.GetEntityCount(), 2u);
    EXPECT_TRUE(glm::all(glm::epsilonEqual(
        target.GetComponent<TransformComponent>(instance.Value()).Position, Vec3(4.0f, 5.0f, 6.0f), 1e-6f)));
    EXPECT_EQ(target.GetChildren(instance.Value()).size(), 1u);
}

TEST(PrefabTest, InstantiatedPrefabCanBeReparented)
{
    const TempDirectory directory;
    const FilePath path = directory.File("prefab.ember");

    World source;
    source.AddComponent<TransformComponent>(source.CreateEntity("PrefabRoot"));
    ASSERT_TRUE(PrefabFormat::SavePrefab(path, source, source.GetEntities().front()).IsSuccess());

    World target;
    const Entity parent = target.CreateEntity("Parent");
    const Result<Entity> instance = PrefabFormat::InstantiatePrefab(path, target, parent);

    ASSERT_TRUE(instance.IsSuccess());
    EXPECT_EQ(target.GetParent(instance.Value()), parent);
}

TEST(PrefabTest, SavingFromADeadEntityFails)
{
    const TempDirectory directory;
    World world;
    const Entity entity = world.CreateEntity();
    world.DestroyEntity(entity);

    const Result<void> saved = PrefabFormat::SavePrefab(directory.File("dead.ember"), world, entity);

    ASSERT_TRUE(saved.IsFailure());
    EXPECT_EQ(saved.GetError().Code, ErrorCode::InvalidArgument);
}

TEST(PrefabTest, InstantiatingAMissingPrefabFails)
{
    const TempDirectory directory;
    World target;

    EXPECT_TRUE(PrefabFormat::InstantiatePrefab(directory.File("absent.ember"), target).IsFailure());
}

TEST(PrefabTest, InstantiatingAPrefabWithNoEntitiesFails)
{
    const TempDirectory directory;
    const FilePath path = directory.File("empty.ember");
    ASSERT_TRUE(FileSystem::WriteTextFile(path, R"({"version": 1, "entities": []})").IsSuccess());

    World target;
    const Result<Entity> instance = PrefabFormat::InstantiatePrefab(path, target);

    ASSERT_TRUE(instance.IsFailure());
    EXPECT_EQ(instance.GetError().Code, ErrorCode::Serialization);
}

TEST(PrefabTest, PrefabFileDeclaresPrefabKind)
{
    const TempDirectory directory;
    const FilePath path = directory.File("prefab.ember");

    World source;
    source.AddComponent<TransformComponent>(source.CreateEntity("Root"));
    ASSERT_TRUE(PrefabFormat::SavePrefab(path, source, source.GetEntities().front()).IsSuccess());

    const Result<std::string> text = FileSystem::ReadTextFile(path);
    ASSERT_TRUE(text.IsSuccess());

    const Result<JsonValue> document = Json::Parse(text.Value());
    ASSERT_TRUE(document.IsSuccess());
    EXPECT_EQ(document.Value()["kind"].AsString(), SceneFormat::PrefabKind);
}

TEST(PrefabTest, TwoInstancesOfOnePrefabAreIndependent)
{
    const TempDirectory directory;
    const FilePath path = directory.File("prefab.ember");

    World source;
    const Entity root = source.CreateEntity("Root");
    TransformComponent transform;
    transform.Position = Vec3(1.0f);
    source.AddComponent(root, transform);
    ASSERT_TRUE(PrefabFormat::SavePrefab(path, source, root).IsSuccess());

    World target;
    const Result<Entity> first = PrefabFormat::InstantiatePrefab(path, target);
    const Result<Entity> second = PrefabFormat::InstantiatePrefab(path, target);

    ASSERT_TRUE(first.IsSuccess());
    ASSERT_TRUE(second.IsSuccess());
    EXPECT_NE(first.Value(), second.Value());
    EXPECT_EQ(target.GetEntityCount(), 2u);

    target.GetComponent<TransformComponent>(first.Value()).Position = Vec3(9.0f);

    EXPECT_FLOAT_EQ(target.GetComponent<TransformComponent>(second.Value()).Position.x, 1.0f);
}
