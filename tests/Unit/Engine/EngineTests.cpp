#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "Core/FileSystem.h"
#include "Engine/Application.h"
#include "Engine/Commands.h"
#include "Engine/Project.h"
#include "Engine/TestScene.h"

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

    /// A temporary project directory that removes itself.
    class TempProject
    {
    public:
        explicit TempProject(bool create = true)
        {
            static int counter = 0;
            m_Path = std::filesystem::temp_directory_path() /
                     ("ember-engine-tests-" + std::to_string(++counter));
            std::filesystem::remove_all(m_Path);
            std::filesystem::create_directories(m_Path);

            if (create)
            {
                const Result<Project> project = Project::Create(m_Path, "TestGame");
                EXPECT_TRUE(project.IsSuccess());
            }
        }

        ~TempProject()
        {
            std::error_code error;
            std::filesystem::remove_all(m_Path, error);
        }

        TempProject(const TempProject&) = delete;
        TempProject& operator=(const TempProject&) = delete;

        [[nodiscard]] const FilePath& Path() const { return m_Path; }
        [[nodiscard]] FilePath File(const std::string& name) const { return m_Path / name; }

        /// Opens the created project.
        [[nodiscard]] Result<Project> Open() const { return Project::Open(m_Path); }

    private:
        FilePath m_Path;
    };

    /// An application that shuts itself down.
    class AppFixture
    {
    public:
        AppFixture() = default;

        ~AppFixture()
        {
            m_Application.Shutdown();
        }

        AppFixture(const AppFixture&) = delete;
        AppFixture& operator=(const AppFixture&) = delete;

        [[nodiscard]] Application& Get() { return m_Application; }

        /// Starts the application, or records why it could not.
        void Initialise()
        {
            ASSERT_TRUE(m_Application.Initialise().IsSuccess());
        }

    private:
        Application m_Application;
    };
}

// ------------------------------------------------------------------- projects

TEST(ProjectTest, CreateMakesAProject)
{
    const TempProject directory;

    const Result<Project> project = directory.Open();

    ASSERT_TRUE(project.IsSuccess());
    EXPECT_TRUE(project.Value().IsValid());
    EXPECT_EQ(project.Value().GetSettings().Name, "TestGame");
    EXPECT_TRUE(FileSystem::Exists(directory.File("project.emberproj")));
}

TEST(ProjectTest, CreateMakesTheExpectedDirectories)
{
    const TempProject directory;
    const Project project = directory.Open().Value();

    EXPECT_TRUE(std::filesystem::is_directory(project.GetScenesDirectory()));
    EXPECT_TRUE(std::filesystem::is_directory(project.GetPrefabsDirectory()));
    EXPECT_TRUE(std::filesystem::is_directory(project.GetScriptsDirectory()));
    EXPECT_TRUE(std::filesystem::is_directory(project.GetAssetsDirectory()));
}

TEST(ProjectTest, CreateTwiceIsRefused)
{
    const TempProject directory;

    const Result<Project> again = Project::Create(directory.Path(), "Another");
    EXPECT_TRUE(again.IsFailure());
}

TEST(ProjectTest, OpenOfANonProjectFails)
{
    const TempProject directory(/*create=*/false);

    const Result<Project> project = directory.Open();
    ASSERT_TRUE(project.IsFailure());
    EXPECT_EQ(project.GetError().Code, ErrorCode::FileNotFound);
}

TEST(ProjectTest, SettingsSurviveARoundTrip)
{
    const TempProject directory;
    Project project = directory.Open().Value();

    ProjectSettings settings = project.GetSettings();
    settings.Description = "A test project";
    settings.StartScene = "level.ember";
    settings.PhysicsTimeStep = 1.0f / 120.0f;
    settings.Gravity = Vec3(0.0f, -20.0f, 0.0f);

    ASSERT_TRUE(project.SetSettings(settings).IsSuccess());

    const Result<Project> reopened = directory.Open();
    ASSERT_TRUE(reopened.IsSuccess());

    const ProjectSettings& read = reopened.Value().GetSettings();
    EXPECT_EQ(read.Description, "A test project");
    EXPECT_EQ(read.StartScene, "level.ember");
    EXPECT_FLOAT_EQ(read.PhysicsTimeStep, 1.0f / 120.0f);
    EXPECT_TRUE(glm::all(glm::epsilonEqual(read.Gravity, Vec3(0.0f, -20.0f, 0.0f), 1e-5f)));
}

TEST(ProjectTest, CollisionLayersSurviveARoundTrip)
{
    const TempProject directory;
    Project project = directory.Open().Value();

    const Result<Project> reopened = directory.Open();
    ASSERT_TRUE(reopened.IsSuccess());

    const std::vector<ProjectSettings::Layer>& layers = reopened.Value().GetSettings().CollisionLayers;
    ASSERT_FALSE(layers.empty());
    EXPECT_EQ(layers[0].Name, "Default");
    const bool hasPlayer = std::any_of(layers.begin(), layers.end(),
                                       [](const ProjectSettings::Layer& layer) { return layer.Name == "Player"; });
    EXPECT_TRUE(hasPlayer);
}

TEST(ProjectTest, ALayerReferencingAnUnknownOneIsRefused)
{
    const TempProject directory;
    Project project = directory.Open().Value();

    ProjectSettings settings = project.GetSettings();
    settings.CollisionLayers.push_back({"Ghost", {"Nowhere"}});

    const Result<void> written = project.SetSettings(settings);

    ASSERT_TRUE(written.IsFailure());
    EXPECT_EQ(written.GetError().Code, ErrorCode::NotFound);
}

TEST(ProjectTest, APathLeavingTheProjectIsRefused)
{
    const TempProject directory;
    const Project project = directory.Open().Value();

    // A project file is data. A path that climbs out would let a scene name any
    // file on the machine.
    EXPECT_TRUE(project.Resolve("../../etc/passwd").IsFailure());
    EXPECT_TRUE(project.Resolve("scenes/main.ember").IsSuccess());
    EXPECT_TRUE(project.Resolve("").IsFailure());
}

TEST(ProjectTest, AMalformedProjectFileIsRefused)
{
    const TempProject directory;
    ASSERT_TRUE(FileSystem::WriteTextFile(directory.File("project.emberproj"), "{ broken").IsSuccess());

    EXPECT_TRUE(directory.Open().IsFailure());
}

TEST(ProjectTest, AProjectFileWithoutAVersionIsRefused)
{
    const TempProject directory;
    ASSERT_TRUE(FileSystem::WriteTextFile(directory.File("project.emberproj"), R"({"name": "x"})").IsSuccess());

    const Result<Project> project = directory.Open();
    ASSERT_TRUE(project.IsFailure());
    EXPECT_EQ(project.GetError().Code, ErrorCode::ParseError);
}

TEST(ProjectTest, AProjectFileFromTheFutureIsRefused)
{
    const TempProject directory;
    ASSERT_TRUE(FileSystem::WriteTextFile(directory.File("project.emberproj"),
                                          R"({"version": 9999, "name": "x"})")
                    .IsSuccess());

    const Result<Project> project = directory.Open();
    ASSERT_TRUE(project.IsFailure());
    EXPECT_EQ(project.GetError().Code, ErrorCode::NotSupported);
}

// --------------------------------------------------------------------- export

TEST(ProjectExportTest, ExportWritesAManifest)
{
    const TempProject directory;
    Project project = directory.Open().Value();

    ASSERT_TRUE(TestScene::WriteToProject(project).IsSuccess());

    const FilePath build = directory.Path() / "build";
    ASSERT_TRUE(project.Export(build).IsSuccess());

    const Result<JsonValue> manifest = Project::ReadManifest(build);
    ASSERT_TRUE(manifest.IsSuccess());
    EXPECT_EQ(manifest.Value()[ProjectFormat::ManifestProjectKey].AsString(), "TestGame");
    EXPECT_FALSE(manifest.Value()[ProjectFormat::ManifestStartSceneKey].AsString().empty());
}

TEST(ProjectExportTest, ExportCopiesScenesAndScripts)
{
    const TempProject directory;
    Project project = directory.Open().Value();

    ASSERT_TRUE(TestScene::WriteToProject(project).IsSuccess());
    ASSERT_TRUE(project.Export(directory.Path() / "build").IsSuccess());

    const FilePath build = directory.Path() / "build";
    EXPECT_TRUE(FileSystem::Exists(build / "scenes" / "test.ember"));
    EXPECT_TRUE(FileSystem::Exists(build / "scripts" / "driven_sphere.lua"));
}

TEST(ProjectExportTest, ExportReplacesAPreviousOne)
{
    const TempProject directory;
    Project project = directory.Open().Value();
    ASSERT_TRUE(TestScene::WriteToProject(project).IsSuccess());

    const FilePath build = directory.Path() / "build";
    ASSERT_TRUE(project.Export(build).IsSuccess());

    // A file left from the previous export would ship, and nothing would say so.
    ASSERT_TRUE(FileSystem::WriteTextFile(build / "scenes" / "stale.ember", "stale").IsSuccess());

    ASSERT_TRUE(project.Export(build).IsSuccess());
    EXPECT_FALSE(FileSystem::Exists(build / "scenes" / "stale.ember"));
}

TEST(ProjectExportTest, ReadingAManifestThatIsNotThereFails)
{
    const TempProject directory;

    const Result<JsonValue> manifest = Project::ReadManifest(directory.Path() / "not-a-build");

    ASSERT_TRUE(manifest.IsFailure());
    EXPECT_EQ(manifest.GetError().Code, ErrorCode::FileNotFound);
}

TEST(ProjectExportTest, ABuildCanBeReadBackAndRun)
{
    const TempProject directory;
    Project project = directory.Open().Value();
    ASSERT_TRUE(TestScene::WriteToProject(project).IsSuccess());

    // The runtime starts whatever the manifest names, so the project's start scene
    // has to be the one that was written.
    ProjectSettings settings = project.GetSettings();
    settings.StartScene = "test.ember";
    ASSERT_TRUE(project.SetSettings(settings).IsSuccess());

    const FilePath build = directory.Path() / "build";
    ASSERT_TRUE(project.Export(build).IsSuccess());

    // The runtime reads only the manifest, so this is the shipped path exactly.
    const Result<JsonValue> manifest = Project::ReadManifest(build);
    ASSERT_TRUE(manifest.IsSuccess());

    const std::string startScene = manifest.Value()[ProjectFormat::ManifestStartSceneKey].AsString();
    ASSERT_FALSE(startScene.empty());

    const Result<Scene> scene = Scene::LoadFromFile(build / "scenes" / startScene);
    if (scene.IsFailure())
    {
        FAIL() << scene.GetError().Message;
    }

    ASSERT_TRUE(scene.IsSuccess());

    Application application;
    ASSERT_TRUE(application.Initialise().IsSuccess());
    ASSERT_TRUE(application.LoadScene(scene.Value()).IsSuccess());

    EXPECT_EQ(application.GetWorld().GetEntityCount(), TestScene::Build().GetEntityCount());
}

TEST(ProjectExportTest, ExportedFilesAreListed)
{
    const TempProject directory;
    Project project = directory.Open().Value();
    ASSERT_TRUE(TestScene::WriteToProject(project).IsSuccess());
    ASSERT_TRUE(project.Export(directory.Path() / "build").IsSuccess());

    const Result<std::vector<FilePath>> files = Project::ListExportedFiles(directory.Path() / "build");

    ASSERT_TRUE(files.IsSuccess());
    EXPECT_GE(files.Value().size(), 3u);
}

// -------------------------------------------------------------------- commands

TEST(CommandTest, ParsesANameAndArguments)
{
    const Result<Command> command = ParseCommand("add-component Player Transform Position=1");

    ASSERT_TRUE(command.IsSuccess());
    EXPECT_EQ(command.Value().Name, "add-component");
    ASSERT_EQ(command.Value().Arguments.size(), 3u);
    EXPECT_EQ(command.Value().Arguments[2], "Position=1");
}

TEST(CommandTest, QuotedArgumentsKeepTheirSpaces)
{
    const Result<Command> command = ParseCommand(R"(open "/Users/hamdan/My Games/Game")");

    ASSERT_TRUE(command.IsSuccess());
    ASSERT_EQ(command.Value().Arguments.size(), 1u);
    EXPECT_EQ(command.Value().Arguments[0], "/Users/hamdan/My Games/Game");
}

TEST(CommandTest, RepeatedSpacesDoNotProduceEmptyArguments)
{
    const Result<Command> command = ParseCommand("  run   10  0.1  ");

    ASSERT_TRUE(command.IsSuccess());
    EXPECT_EQ(command.Value().Arguments.size(), 2u);
}

TEST(CommandTest, AnEmptyLineIsRefused)
{
    EXPECT_TRUE(ParseCommand("").IsFailure());
    EXPECT_TRUE(ParseCommand("   ").IsFailure());
}

TEST(CommandTest, CreateAndOpenRoundTrip)
{
    const TempProject directory(/*create=*/false);
    CommandRunner runner;

    EXPECT_TRUE(runner.Create(directory.Path().string(), "Commanded").Succeeded);
    EXPECT_TRUE(runner.HasProject());
    EXPECT_EQ(runner.GetProject().GetSettings().Name, "Commanded");
}

TEST(CommandTest, OpeningNothingFails)
{
    const TempProject directory(/*create=*/false);
    CommandRunner runner;

    EXPECT_FALSE(runner.Open(directory.Path().string()).Succeeded);
    EXPECT_FALSE(runner.Save().Succeeded);
}

TEST(CommandTest, BuildingASceneThroughCommands)
{
    const TempProject directory;
    CommandRunner runner;

    ASSERT_TRUE(runner.Open(directory.Path().string()).Succeeded);
    ASSERT_TRUE(runner.CreateEntity("Player").Succeeded);
    ASSERT_TRUE(runner.AddComponent("Player", "Transform", {"Position.x=1"}).Succeeded);

    EXPECT_TRUE(runner.GetApplicationResult().IsSuccess());
}

TEST(CommandTest, AddingAnUnknownComponentFails)
{
    const TempProject directory;
    CommandRunner runner;

    ASSERT_TRUE(runner.Open(directory.Path().string()).Succeeded);
    ASSERT_TRUE(runner.CreateEntity("Player").Succeeded);

    const CommandResult result = runner.AddComponent("Player", "Nonexistent", {});

    EXPECT_FALSE(result.Succeeded);
    EXPECT_NE(result.Error.find("Nonexistent"), std::string::npos);
}

TEST(CommandTest, AddingAComponentToAnUnknownEntityFails)
{
    const TempProject directory;
    CommandRunner runner;

    ASSERT_TRUE(runner.Open(directory.Path().string()).Succeeded);

    EXPECT_FALSE(runner.AddComponent("Nobody", "Transform", {}).Succeeded);
    EXPECT_FALSE(runner.DestroyEntity("Nobody").Succeeded);
}

TEST(CommandTest, MalformedFieldsAreRefused)
{
    const TempProject directory;
    CommandRunner runner;

    ASSERT_TRUE(runner.Open(directory.Path().string()).Succeeded);
    ASSERT_TRUE(runner.CreateEntity("Player").Succeeded);

    EXPECT_FALSE(runner.AddComponent("Player", "Transform", {"not a field"}).Succeeded);
}

TEST(CommandTest, DestroyingAnEntityRemovesIt)
{
    const TempProject directory;
    CommandRunner runner;

    ASSERT_TRUE(runner.Open(directory.Path().string()).Succeeded);
    ASSERT_TRUE(runner.CreateEntity("Doomed").Succeeded);
    EXPECT_TRUE(runner.DestroyEntity("Doomed").Succeeded);
    EXPECT_FALSE(runner.DestroyEntity("Doomed").Succeeded);
}

TEST(CommandTest, LoadingTheTestSceneRunsTheWholePipeline)
{
    const TempProject directory;
    CommandRunner runner;

    ASSERT_TRUE(runner.Open(directory.Path().string()).Succeeded);

    const Result<Project> project = directory.Open();
    ASSERT_TRUE(project.IsSuccess());
    ASSERT_TRUE(TestScene::WriteToProject(project.Value()).IsSuccess());

    EXPECT_TRUE(runner.LoadScene("test.ember").Succeeded);
    EXPECT_TRUE(runner.Run(10, 1.0f / 60.0f).Succeeded);
}

TEST(CommandTest, LoadingAMissingSceneFails)
{
    const TempProject directory;
    CommandRunner runner;

    ASSERT_TRUE(runner.Open(directory.Path().string()).Succeeded);

    EXPECT_FALSE(runner.LoadScene("absent.ember").Succeeded);
}

TEST(CommandTest, ExportingProducesAShippableBuild)
{
    const TempProject directory;
    CommandRunner runner;

    ASSERT_TRUE(runner.Open(directory.Path().string()).Succeeded);

    const Result<Project> project = directory.Open();
    ASSERT_TRUE(project.IsSuccess());
    ASSERT_TRUE(TestScene::WriteToProject(project.Value()).IsSuccess());

    EXPECT_TRUE(runner.Export(directory.Path().string() + "/build").Succeeded);
}

TEST(CommandTest, ListingComponentsNamesThemAll)
{
    const CommandResult result = CommandRunner::ListComponents();

    ASSERT_TRUE(result.Succeeded);

    for (const char* name : {"Transform", "Mesh", "Light", "Camera", "Collider", "AudioSource", "Script", "Prefab"})
    {
        EXPECT_NE(result.Output.find(name), std::string::npos) << name;
    }
}

TEST(CommandTest, StatisticsNeedSomethingToHaveRun)
{
    CommandRunner runner;

    EXPECT_FALSE(runner.Statistics().Succeeded);
}

// ----------------------------------------------------------------- application

TEST(ApplicationTest, InitialiseAndShutdown)
{
    Application application;

    EXPECT_TRUE(application.Initialise().IsSuccess());
    EXPECT_TRUE(application.IsInitialised());

    application.Shutdown();
    EXPECT_FALSE(application.IsInitialised());
}

TEST(ApplicationTest, ShutdownIsIdempotent)
{
    Application application;
    ASSERT_TRUE(application.Initialise().IsSuccess());

    application.Shutdown();
    application.Shutdown();

    EXPECT_FALSE(application.IsInitialised());
}

TEST(ApplicationTest, AFrameAdvancesTimeAndCountsItself)
{
    AppFixture fixture;
    fixture.Initialise();
    fixture.Get().SetMaximumDeltaTime(1.0f);

    fixture.Get().RunFrame(0.5f);

    const FrameStatistics& statistics = fixture.Get().GetStatistics();
    EXPECT_FLOAT_EQ(statistics.DeltaTime, 0.5f);
    EXPECT_FLOAT_EQ(statistics.ElapsedTime, 0.5f);
    EXPECT_EQ(statistics.FrameCount, 1u);
}

TEST(ApplicationTest, TheDefaultDeltaTimeCapIsQuarterOfASecond)
{
    AppFixture fixture;
    fixture.Initialise();

    // A quarter of a second is the largest step that stays stable for the
    // simulation's own fixed step.
    fixture.Get().RunFrame(100.0f);

    EXPECT_FLOAT_EQ(fixture.Get().GetStatistics().DeltaTime, 0.25f);
}

TEST(ApplicationTest, ALongFrameIsClamped)
{
    AppFixture fixture;
    fixture.Initialise();
    fixture.Get().SetMaximumDeltaTime(0.25f);

    // A stall must not turn into a simulation that has to catch all of it up, or
    // everything tunnels through the geometry it should have hit.
    fixture.Get().RunFrame(10.0f);

    EXPECT_FLOAT_EQ(fixture.Get().GetStatistics().DeltaTime, 0.25f);
}

TEST(ApplicationTest, ANegativeDeltaIsIgnored)
{
    AppFixture fixture;
    fixture.Initialise();

    fixture.Get().RunFrame(-1.0f);

    EXPECT_FLOAT_EQ(fixture.Get().GetStatistics().DeltaTime, 0.0f);
}

TEST(ApplicationTest, RunningBeforeInitialiseDoesNothing)
{
    Application application;

    application.RunFrame(0.1f);

    EXPECT_EQ(application.GetStatistics().FrameCount, 0u);
}

TEST(ApplicationTest, StatisticsReportTheFrame)
{
    AppFixture fixture;
    fixture.Initialise();

    World& world = fixture.Get().GetWorld();
    TransformComponent transform;
    world.AddComponent(world.CreateEntity("Drawn"), transform);

    fixture.Get().GetRenderQueue().SetMeshBounds(0, Vec3(-0.5f), Vec3(0.5f));
    fixture.Get().RunFrame(0.016f);

    EXPECT_EQ(fixture.Get().GetStatistics().EntityCount, 1u);
}

// ------------------------------------------------------------------ test scene

TEST(TestSceneTest, UsesEveryBuiltInComponent)
{
    const Scene scene = TestScene::Build();

    World world;
    ASSERT_TRUE(scene.LoadInto(world).IsSuccess());

    const std::vector<ComponentTypeInfo>& types = ComponentRegistry::Get().Types();
    ASSERT_FALSE(types.empty());

    // Every component the engine registers has to appear somewhere in the scene
    // that exists to exercise every feature, or the component is untested.
    for (const ComponentTypeInfo& info : types)
    {
        bool found = false;
        for (const Entity entity : world.GetEntities())
        {
            found = found || world.HasComponentByType(entity, info.Id);
        }

        EXPECT_TRUE(found) << info.Name << " does not appear in the test scene";
    }
}

TEST(TestSceneTest, HasTheEntitiesTheTestsNeed)
{
    const Scene scene = TestScene::Build();

    World world;
    ASSERT_TRUE(scene.LoadInto(world).IsSuccess());

    for (const char* name : {"Ground", "Camera", "Sun", "Lamp", "FallingBox", "DrivenSphere", "Speaker",
                             "PrefabInstance", "DisabledParent", "HiddenChild"})
    {
        bool found = false;
        for (const Entity entity : world.GetEntities())
        {
            found = found || world.GetName(entity) == name;
        }

        EXPECT_TRUE(found) << "the test scene has no entity named " << name;
    }
}

TEST(TestSceneTest, TheFallingBoxLandsOnTheGround)
{
    AppFixture fixture;
    fixture.Initialise();

    ASSERT_TRUE(fixture.Get().LoadScene(TestScene::Build()).IsSuccess());
    EXPECT_EQ(fixture.Get().GetPhysics().GetBodyCount(), 3u);

    // Long enough for the box to fall, hit the ground and settle.
    for (int frame = 0; frame < 300; ++frame)
    {
        fixture.Get().RunFrame(1.0f / 60.0f);
    }

    bool found = false;
    fixture.Get().GetWorld().Each<TransformComponent>(
        [&](const TransformComponent& transform, Entity entity)
    {
        if (fixture.Get().GetWorld().GetName(entity) != "FallingBox")
        {
            return;
        }

        found = true;

        // The ground's top face is at 0.5 and the box's half-extent is 0.5, so it
        // comes to rest resting on it.
        EXPECT_GT(transform.Position.y, 0.8f);
        EXPECT_LT(transform.Position.y, 1.2f);
    });

    EXPECT_TRUE(found);
}

TEST(TestSceneTest, TheDisabledParentHidesItsChild)
{
    AppFixture fixture;
    fixture.Initialise();

    ASSERT_TRUE(fixture.Get().LoadScene(TestScene::Build()).IsSuccess());
    fixture.Get().RunFrame(1.0f / 60.0f);

    World& world = fixture.Get().GetWorld();

    for (const Entity entity : world.GetEntities())
    {
        if (world.GetName(entity) == "HiddenChild")
        {
            // The child's own flag is set, but it is not active because its parent
            // is not, and so the render queue never sees it.
            EXPECT_TRUE(world.IsEnabled(entity));
            EXPECT_FALSE(world.IsActiveInHierarchy(entity));
        }
    }

    int drawn = 0;
    for (const DrawItem& item : fixture.Get().GetRenderQueue().GetDrawItems())
    {
        drawn += world.GetName(item.Entity) == "HiddenChild" ? 1 : 0;
    }

    EXPECT_EQ(drawn, 0);
}

TEST(TestSceneTest, TheRenderQueueFindsTheMeshes)
{
    AppFixture fixture;
    fixture.Initialise();

    ASSERT_TRUE(fixture.Get().LoadScene(TestScene::Build()).IsSuccess());

    // The ground, the falling box and the driven sphere carry meshes. The meshes
    // have no registered bounds in this test, so nothing is culled and the point
    // is simply that every visible mesh reaches the queue.
    fixture.Get().RunFrame(1.0f / 60.0f);

    EXPECT_EQ(fixture.Get().GetStatistics().DrawItemCount, 3u);
    EXPECT_EQ(fixture.Get().GetRenderQueue().GetDirectionalLights().size(), 1u);
    EXPECT_EQ(fixture.Get().GetRenderQueue().GetPointLights().size(), 1u);
    EXPECT_EQ(fixture.Get().GetRenderQueue().GetShadowProjections().size(), 1u);
}

TEST(TestSceneTest, TheScriptMovesItsEntityAndSpawnsAnother)
{
    const TempProject directory;
    const Result<Project> project = directory.Open();
    ASSERT_TRUE(project.IsSuccess());

    const Result<std::string> script = TestScene::WriteToProject(project.Value());
    ASSERT_TRUE(script.IsSuccess());

    AppFixture fixture;
    fixture.Initialise();
    fixture.Get().SetScriptDirectory(FileSystem::ToString(project.Value().GetScriptsDirectory()));

    ASSERT_TRUE(fixture.Get().LoadScene(TestScene::Build()).IsSuccess());

    // Attach the script to the driven sphere.
    for (const Entity entity : fixture.Get().GetWorld().GetEntities())
    {
        if (fixture.Get().GetWorld().GetName(entity) == "DrivenSphere")
        {
            ScriptComponent component;
            component.ScriptPath = script.Value();
            fixture.Get().GetWorld().AddComponent(entity, component);
        }
    }

    const std::size_t before = fixture.Get().GetWorld().GetEntityCount();

    for (int frame = 0; frame < 30; ++frame)
    {
        fixture.Get().RunFrame(1.0f / 60.0f);
    }

    // The script spawned one entity in OnStart.
    EXPECT_EQ(fixture.Get().GetWorld().GetEntityCount(), before + 1);
    EXPECT_EQ(fixture.Get().GetScriptEngine().GetActiveScriptCount(), 1u);
    EXPECT_TRUE(fixture.Get().GetScriptEngine().GetErrors().empty());

    // The script's own counters are what prove it ran: the sphere is a kinematic
    // body, so the simulation owns its transform and would overwrite anything the
    // script wrote to it anyway.
    Entity driven = Entity::Null();
    for (const Entity entity : fixture.Get().GetWorld().GetEntities())
    {
        driven = fixture.Get().GetWorld().GetName(entity) == "DrivenSphere" ? entity : driven;
    }

    ASSERT_TRUE(fixture.Get().GetWorld().IsAlive(driven));

    const ScriptEngine& scripts = fixture.Get().GetScriptEngine();
    EXPECT_DOUBLE_EQ(scripts.GetInstanceGlobalNumber(driven, "updateCount"), 30.0);
    EXPECT_DOUBLE_EQ(scripts.GetInstanceGlobalNumber(driven, "spawnedCount"), 1.0);

    // The script's own configuration globals are reachable too, which is what
    // makes a running script inspectable from the editor and from a test.
    EXPECT_EQ(scripts.GetInstanceGlobalNumber(driven, "radius"), 2.0);
    EXPECT_EQ(scripts.GetInstanceGlobalNumber(driven, "speed"), 1.0);

    // The spawned entity exists, and OnDestroy is what removes it.
    EXPECT_EQ(scripts.GetInstanceGlobalNumber(driven, "destroyedCount"), 0.0);
}

TEST(TestSceneTest, UnloadingStopsTheScriptsAndClearsTheWorld)
{
    AppFixture fixture;
    fixture.Initialise();

    ASSERT_TRUE(fixture.Get().LoadScene(TestScene::Build()).IsSuccess());
    EXPECT_GT(fixture.Get().GetWorld().GetEntityCount(), 0u);

    fixture.Get().UnloadScene();

    EXPECT_EQ(fixture.Get().GetWorld().GetEntityCount(), 0u);
    EXPECT_EQ(fixture.Get().GetScriptEngine().GetActiveScriptCount(), 0u);
}

TEST(TestSceneTest, TheSceneRoundTripsThroughAFile)
{
    const TempProject directory;
    const Result<Project> project = directory.Open();
    ASSERT_TRUE(project.IsSuccess());

    ASSERT_TRUE(TestScene::WriteToProject(project.Value()).IsSuccess());

    const Result<Scene> scene = Scene::LoadFromFile(project.Value().GetScenesDirectory() / "test.ember");
    ASSERT_TRUE(scene.IsSuccess());
    EXPECT_EQ(scene.Value().GetEntityCount(), TestScene::Build().GetEntityCount());
}

TEST(TestSceneTest, TheScriptSourceIsNotEmptyAndBalanced)
{
    const std::string source = TestScene::GetScriptSource();

    EXPECT_FALSE(source.empty());

    // A quick check that the script really is Lua rather than something that
    // merely looks like it.
    EXPECT_NE(source.find("function OnStart"), std::string::npos);
    EXPECT_NE(source.find("function OnUpdate"), std::string::npos);
    EXPECT_NE(source.find("function OnDestroy"), std::string::npos);
}