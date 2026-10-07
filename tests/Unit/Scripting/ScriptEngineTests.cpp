#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "Core/FileSystem.h"
#include "Ecs/Components.h"
#include "Scripting/ScriptEngine.h"

using namespace Ember;

namespace
{
    /// Registers the built-in component set exactly once per process.
    struct BuiltinComponentRegistration
    {
        BuiltinComponentRegistration()
        {
            Ember::Ecs::RegisterBuiltinComponents();
        }
    };

    const BuiltinComponentRegistration s_BuiltinComponentRegistration;

    /// A temporary script directory that removes itself, for the file-based tests.
    class ScriptDirectory
    {
    public:
        ScriptDirectory()
        {
            static int counter = 0;
            m_Path = std::filesystem::temp_directory_path() /
                     ("ember-script-tests-" + std::to_string(++counter));
            std::filesystem::remove_all(m_Path);
            std::filesystem::create_directories(m_Path);
            m_PathString = FileSystem::ToString(m_Path);
        }

        ~ScriptDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_Path, error);
        }

        ScriptDirectory(const ScriptDirectory&) = delete;
        ScriptDirectory& operator=(const ScriptDirectory&) = delete;

        /// Writes a script file and returns its path relative to the directory.
        [[nodiscard]] std::string Write(std::string_view name, std::string_view source) const
        {
            const std::string fileName(name);
            EXPECT_TRUE(FileSystem::WriteTextFile(m_Path / fileName, source).IsSuccess());
            return fileName;
        }

        [[nodiscard]] const std::string& Path() const { return m_PathString; }

    private:
        FilePath m_Path;
        std::string m_PathString;
    };

    /// An initialised script engine, shut down when the test ends.
    class EngineFixture
    {
    public:
        EngineFixture() = default;

        ~EngineFixture()
        {
            m_Engine.Shutdown();
        }

        EngineFixture(const EngineFixture&) = delete;
        EngineFixture& operator=(const EngineFixture&) = delete;

        [[nodiscard]] ScriptEngine& Get() { return m_Engine; }

        /// Opens the Lua state. Call from the body of a test.
        [[nodiscard]] bool Initialise()
        {
            const Result<void> result = m_Engine.Initialise();
            return result.IsSuccess();
        }

    private:
        ScriptEngine m_Engine;
    };

    /// Runs one frame of the given engine against a world.
    void RunFrame(ScriptEngine& engine, World& world, float deltaTime = 0.016f, int frames = 1)
    {
        for (int i = 0; i < frames; ++i)
        {
            engine.Start(world);
            engine.Update(world, deltaTime);
        }
    }
}

// ------------------------------------------------------------------- lifecycle

TEST(ScriptEngineTest, StartsUninitialised)
{
    ScriptEngine engine;

    EXPECT_FALSE(engine.IsInitialised());
    EXPECT_EQ(engine.GetState(), nullptr);
}

TEST(ScriptEngineTest, InitialiseCreatesAState)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());

    EXPECT_TRUE(fixture.Get().IsInitialised());
    EXPECT_NE(fixture.Get().GetState(), nullptr);
}

TEST(ScriptEngineTest, InitialiseIsIdempotent)
{
    ScriptEngine engine;
    ASSERT_TRUE(engine.Initialise().IsSuccess());
    lua_State* state = engine.GetState();

    EXPECT_TRUE(engine.Initialise().IsSuccess());
    EXPECT_EQ(engine.GetState(), state);

    engine.Shutdown();
}

TEST(ScriptEngineTest, ShutdownIsIdempotent)
{
    ScriptEngine engine;
    ASSERT_TRUE(engine.Initialise().IsSuccess());

    engine.Shutdown();
    engine.Shutdown();

    EXPECT_FALSE(engine.IsInitialised());
}

TEST(ScriptEngineTest, OperationsBeforeInitialiseFail)
{
    ScriptEngine engine;

    EXPECT_TRUE(engine.LoadScript("anything.lua").IsFailure());
    EXPECT_TRUE(engine.ExecuteString("x = 1").IsFailure());
    EXPECT_TRUE(engine.ExecuteFile("anything.lua").IsFailure());
}

TEST(ScriptEngineTest, ShutdownClearsScriptsAndErrors)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    World world;
    ScriptComponent script;
    script.ScriptPath = directory.Write("counter.lua", "counter = 0");
    world.AddComponent(world.CreateEntity(), script);

    engine.Start(world);
    engine.Update(world, 0.016f);
    ASSERT_EQ(engine.GetActiveScriptCount(), 1u);

    engine.Stop(world);

    engine.Shutdown();

    EXPECT_EQ(engine.GetActiveScriptCount(), 0u);
    EXPECT_TRUE(engine.GetErrors().empty());
}

// -------------------------------------------------------------- execution

TEST(ScriptEngineTest, ExecutesAString)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());

    EXPECT_TRUE(fixture.Get().ExecuteString("answer = 42").IsSuccess());
    EXPECT_DOUBLE_EQ(fixture.Get().GetGlobalNumber("answer"), 42.0);
}

TEST(ScriptEngineTest, ReportsASyntaxError)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();

    const Result<void> result = engine.ExecuteString("this is not lua");

    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::ScriptError);
    EXPECT_FALSE(engine.GetErrors().empty());
}

TEST(ScriptEngineTest, ReportsARuntimeError)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();

    const Result<void> result = engine.ExecuteString("error('boom')");

    ASSERT_TRUE(result.IsFailure());
    EXPECT_FALSE(engine.GetErrors().empty());
}

TEST(ScriptEngineTest, ErrorMessageNamesTheChunk)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();

    const Result<void> result = engine.ExecuteString("error('boom')", "=my-script");

    ASSERT_TRUE(result.IsFailure());
    EXPECT_NE(engine.GetErrors().front().find("my-script"), std::string::npos);
}

TEST(ScriptEngineTest, ClearErrorsEmptiesTheList)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();

    engine.ExecuteString("error('boom')");
    ASSERT_FALSE(engine.GetErrors().empty());

    engine.ClearErrors();
    EXPECT_TRUE(engine.GetErrors().empty());
}

TEST(ScriptEngineTest, GlobalAccessorsFallBackWhenAbsent)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();

    EXPECT_DOUBLE_EQ(engine.GetGlobalNumber("missing", 7.0), 7.0);
    EXPECT_TRUE(engine.GetGlobalString("missing").empty());
    EXPECT_TRUE(engine.GetGlobalBool("missing", true));
    EXPECT_EQ(engine.GetGlobalTableLength("missing"), 0u);
}

TEST(ScriptEngineTest, GlobalAccessorsReadTheRightTypes)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();

    ASSERT_TRUE(engine.ExecuteString(R"(
        aNumber = 1.5
        aString = "text"
        aBoolean = true
        aTable = {1, 2, 3}
    )").IsSuccess());

    EXPECT_DOUBLE_EQ(engine.GetGlobalNumber("aNumber"), 1.5);
    EXPECT_EQ(engine.GetGlobalString("aString"), "text");
    EXPECT_TRUE(engine.GetGlobalBool("aBoolean"));
    EXPECT_EQ(engine.GetGlobalTableLength("aTable"), 3u);

    // Reading a global as the wrong type falls back rather than misreading it:
    // a number must not come back as though it were a string.
    EXPECT_TRUE(engine.GetGlobalString("aNumber").empty());
    EXPECT_TRUE(engine.GetGlobalString("aString").empty() != true);
    EXPECT_TRUE(engine.GetGlobalString("noSuchGlobal").empty());
}

TEST(ScriptEngineTest, ExecutesAFile)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("setup.lua", "fromFile = 'loaded'");

    ASSERT_TRUE(engine.ExecuteFile(name).IsSuccess());
    EXPECT_EQ(engine.GetGlobalString("fromFile"), "loaded");
}

TEST(ScriptEngineTest, ExecutingAMissingFileFails)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    fixture.Get().SetScriptDirectory(directory.Path());

    EXPECT_TRUE(fixture.Get().ExecuteFile("absent.lua").IsFailure());
}

TEST(ScriptEngineTest, ResolvesRelativePathsAgainstTheScriptDirectory)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());
    EXPECT_FALSE(directory.Write("relative.lua", "value = 5").empty());

    ASSERT_TRUE(engine.ExecuteFile("relative.lua").IsSuccess());
    EXPECT_DOUBLE_EQ(engine.GetGlobalNumber("value"), 5.0);
}

// -------------------------------------------------------------------- loading

TEST(ScriptEngineTest, LoadScriptOfAMissingFileFails)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const Result<ScriptSource*> source = engine.LoadScript("absent.lua");

    ASSERT_TRUE(source.IsFailure());
    EXPECT_EQ(source.GetError().Code, ErrorCode::FileNotFound);
}

TEST(ScriptEngineTest, CompileErrorIsReportedOnceAndTheScriptDoesNotStart)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("broken.lua", "function ( ");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    world.AddComponent(world.CreateEntity(), script);

    engine.Start(world);

    EXPECT_EQ(engine.GetActiveScriptCount(), 0u);
    ASSERT_EQ(engine.GetErrors().size(), 1u);
    EXPECT_NE(engine.GetErrors().front().find("broken.lua"), std::string::npos);

    // The failure is reported once, not on every frame for every entity.
    engine.Start(world);
    engine.Start(world);
    EXPECT_EQ(engine.GetErrors().size(), 1u);
}

// ------------------------------------------------------------- entry points

TEST(ScriptEngineTest, CallsOnStartOncePerInstance)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("start.lua", "starts = 0\nfunction OnStart(e) starts = starts + 1 end");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, script);

    engine.Start(world);
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(entity, "starts"), 1.0);

    // Starting again must not re-run OnStart for an already-started script.
    engine.Start(world);
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(entity, "starts"), 1.0);
}

TEST(ScriptEngineTest, CallsOnUpdateWithDeltaTime)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("update.lua", R"(
        frames = 0
        total = 0
        function OnUpdate(e, dt) frames = frames + 1 total = total + dt end
    )");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    world.AddComponent(world.CreateEntity(), script);

    const Entity entity = world.GetEntities().front();
    RunFrame(engine, world, 0.5f, 3);

    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(entity, "frames"), 3.0);
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(entity, "total"), 1.5);
}

TEST(ScriptEngineTest, CallsOnDestroyWhenStopped)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("destroy.lua", "destroys = 0\nfunction OnDestroy(e) destroys = destroys + 1 end");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    world.AddComponent(world.CreateEntity(), script);

    const Entity entity = world.GetEntities().front();
    engine.Start(world);
    engine.Stop(world);

    // The instance is gone, so its globals are no longer readable; the count is
    // what proves OnDestroy ran.
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(entity, "destroys"), 0.0);
    EXPECT_EQ(engine.GetActiveScriptCount(), 0u);
}

TEST(ScriptEngineTest, MissingEntryPointsAreNotAnError)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("bare.lua", "ran = true");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    world.AddComponent(world.CreateEntity(), script);

    const Entity entity = world.GetEntities().front();
    RunFrame(engine, world);

    // The script's body ran even though it defines no entry points at all.
    EXPECT_TRUE(engine.GetInstanceGlobalBool(entity, "ran"));
    EXPECT_TRUE(engine.GetErrors().empty());
}

TEST(ScriptEngineTest, DisabledScriptIsNotStarted)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("disabled.lua", "function OnStart(e) ran = true end");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    script.Enabled = false;
    world.AddComponent(world.CreateEntity(), script);

    engine.Start(world);

    EXPECT_EQ(engine.GetActiveScriptCount(), 0u);
    EXPECT_FALSE(engine.GetGlobalBool("ran"));
}

TEST(ScriptEngineTest, DisabledScriptIsNotUpdated)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("disabled.lua", "function OnUpdate(e, dt) ran = true end");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    world.AddComponent(world.CreateEntity(), script);

    engine.Start(world);
    ASSERT_EQ(engine.GetActiveScriptCount(), 1u);

    script.Enabled = false;
    world.GetStorage<ScriptComponent>().Find(world.GetEntities().front().Index)->Enabled = false;

    engine.Update(world, 0.016f);

    EXPECT_FALSE(engine.GetGlobalBool("ran"));
}

TEST(ScriptEngineTest, EntityWithoutAPathIsNotStarted)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();

    World world;
    world.AddComponent(world.CreateEntity(), ScriptComponent{});

    engine.Start(world);

    EXPECT_EQ(engine.GetActiveScriptCount(), 0u);
}

TEST(ScriptEngineTest, TwoEntitiesGetIndependentGlobals)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("shared.lua", R"(
        starts = 0
        function OnStart(e) starts = starts + 1 end
    )");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    const Entity first = world.CreateEntity();
    const Entity second = world.CreateEntity();
    world.AddComponent(first, script);
    world.AddComponent(second, script);

    engine.Start(world);

    ASSERT_EQ(engine.GetActiveScriptCount(), 2u);

    // Each entity has its own environment, so the file's top-level assignment ran
    // once per instance and neither instance sees the other's counter.
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(first, "starts"), 1.0);
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(second, "starts"), 1.0);
}

TEST(ScriptEngineTest, InstancesShareTheCompiledSource)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("cached.lua", "value = 1");

    Result<ScriptSource*> first = engine.LoadScript(name);
    Result<ScriptSource*> second = engine.LoadScript(name);

    ASSERT_TRUE(first.IsSuccess());
    ASSERT_TRUE(second.IsSuccess());
    EXPECT_EQ(first.Value(), second.Value());
    EXPECT_TRUE(first.Value()->IsValid());
    EXPECT_EQ(first.Value()->GetPath(), name);
}

TEST(ScriptEngineTest, UnloadScriptForcesAReread)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("edited.lua", "value = 1");
    ASSERT_TRUE(engine.LoadScript(name).IsSuccess());

    ASSERT_TRUE(FileSystem::WriteTextFile(FilePath(directory.Path()) / name, "value = 2").IsSuccess());

    EXPECT_EQ(engine.LoadScript(name).Value()->GetText(), "value = 1");

    engine.UnloadScript(name);

    EXPECT_EQ(engine.LoadScript(name).Value()->GetText(), "value = 2");
}

TEST(ScriptEngineTest, ScriptErrorDisablesOnlyThatScript)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string bad = directory.Write("bad.lua", "function OnUpdate(e, dt) error('boom') end");
    const std::string good = directory.Write("good.lua", "updates = 0\nfunction OnUpdate(e, dt) updates = updates + 1 end");

    World world;
    ScriptComponent failing;
    failing.ScriptPath = bad;
    world.AddComponent(world.CreateEntity(), failing);

    ScriptComponent working;
    working.ScriptPath = good;
    const Entity healthy = world.CreateEntity();
    world.AddComponent(healthy, working);

    engine.Start(world);
    engine.Update(world, 0.016f);
    engine.Update(world, 0.016f);

    // The failing script stops being called; the healthy one keeps running.
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(healthy, "updates"), 2.0);
    EXPECT_FALSE(engine.GetErrors().empty());
}

TEST(ScriptEngineTest, StartErrorIsReportedAndScriptIsNotUpdated)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("badstart.lua", R"(
        updates = 0
        function OnStart(e) error('cannot start') end
        function OnUpdate(e, dt) updates = updates + 1 end
    )");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, script);

    engine.Start(world);
    engine.Update(world, 0.016f);

    EXPECT_EQ(engine.GetActiveScriptCount(), 0u);
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(entity, "updates"), 0.0);
    EXPECT_FALSE(engine.GetErrors().empty());
}

TEST(ScriptEngineTest, ScriptErrorNamesTheEntity)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("boom.lua", "function OnUpdate(e, dt) error('boom') end");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    const Entity victim = world.CreateEntity("Victim");
    world.AddComponent(victim, script);

    engine.Start(world);
    engine.Update(world, 0.016f);

    ASSERT_FALSE(engine.GetErrors().empty());
    EXPECT_NE(engine.GetErrors().front().find("Victim"), std::string::npos);
}

TEST(ScriptEngineTest, StartIsIdempotentPerEntity)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("counted.lua", "starts = 0\nfunction OnStart(e) starts = starts + 1 end");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, script);

    engine.Start(world);
    engine.Start(world);
    engine.Start(world);

    EXPECT_EQ(engine.GetActiveScriptCount(), 1u);
    EXPECT_DOUBLE_EQ(engine.GetInstanceGlobalNumber(entity, "starts"), 1.0);
}

TEST(ScriptEngineTest, IsScriptActiveTracksState)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("tracked.lua", "function OnUpdate(e, dt) end");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, script);

    EXPECT_FALSE(engine.IsScriptActive(entity));

    engine.Start(world);
    EXPECT_TRUE(engine.IsScriptActive(entity));

    engine.Stop(world);
    EXPECT_FALSE(engine.IsScriptActive(entity));
}

TEST(ScriptEngineTest, ResetClearsActiveScripts)
{
    ScriptDirectory directory;
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();
    engine.SetScriptDirectory(directory.Path());

    const std::string name = directory.Write("reset.lua", "function OnUpdate(e, dt) end");

    World world;
    ScriptComponent script;
    script.ScriptPath = name;
    world.AddComponent(world.CreateEntity(), script);

    engine.Start(world);
    ASSERT_EQ(engine.GetActiveScriptCount(), 1u);

    engine.Reset();

    EXPECT_EQ(engine.GetActiveScriptCount(), 0u);
}

TEST(ScriptEngineTest, FrameTimeIsExposedToScripts)
{
    EngineFixture fixture;
    ASSERT_TRUE(fixture.Initialise());
    ScriptEngine& engine = fixture.Get();

    engine.SetElapsedTime(12.5);
    ASSERT_TRUE(engine.ExecuteString("function readTime() return time.now(), time.delta() end").IsSuccess());

    World world;
    world.AddComponent(world.CreateEntity(), ScriptComponent{});
    engine.Start(world);
    engine.Update(world, 0.25f);

    EXPECT_DOUBLE_EQ(engine.GetElapsedTime(), 12.5);
    EXPECT_FLOAT_EQ(engine.GetDeltaTime(), 0.25f);
}
