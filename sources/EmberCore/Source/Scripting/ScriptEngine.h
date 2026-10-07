// Scripting/ScriptEngine.h
//
// Lua scripting bound to the engine's world.
//
// A script attached to an entity is a plain Lua file. The engine calls three
// optional entry points on it:
//
//     function OnStart(entity) end
//     function OnUpdate(entity, deltaTime) end
//     function OnDestroy(entity) end
//
// A script cannot reach the engine except through the bindings in
// ScriptBindings.h, which expose world queries, component access, entity
// creation and destruction, logging and time. Everything a script can do to a
// running game is therefore visible in one place and can be tested.
//
// Instance isolation: two entities running the same script do not share Lua
// globals. Each started script gets its own environment table that inherits the
// engine's, so `counter = 0` in a script file means a fresh counter per entity
// rather than one counter shared by every entity that attached the file.
//
// Lifetime: an entity's `ScriptComponent` names a file. The file's source is
// read once and cached; the chunk is compiled per started script, because a
// compiled closure captures its environment and cannot be shared between
// instances that need separate globals. A script is started once, when its entity
// first becomes live, and stopped once, before the entity is destroyed.
//
// Errors: a script error is reported with the file, the line and the entity, and
// then that script is disabled. One bad script must not take down the game.

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/FileSystem.h"
#include "Core/Result.h"
#include "Ecs/Entity.h"
#include "Ecs/World.h"

struct lua_State;

namespace Ember
{
    /// Lua's "no reference" sentinel for `luaL_ref`.
    ///
    /// Spelled out here rather than including Lua's header, so that a file using
    /// the scripting engine does not pull the interpreter's API into its namespace.
    inline constexpr int LuaNoRef = -2;
}

namespace Ember
{
    /// A script file's source, read once and shared by every entity using it.
    class ScriptSource
    {
    public:
        ScriptSource() = default;

        [[nodiscard]] bool IsValid() const noexcept { return m_Valid; }
        [[nodiscard]] const std::string& GetPath() const noexcept { return m_Path; }
        [[nodiscard]] const std::string& GetText() const noexcept { return m_Text; }

        /// The chunk name Lua reports in errors, naming the file and its path.
        [[nodiscard]] const std::string& GetChunkName() const noexcept { return m_ChunkName; }

    private:
        friend class ScriptEngine;

        std::string m_Path;
        std::string m_ChunkName;
        std::string m_Text;

        /// Why the file could not be used. Set alongside a false `m_Valid`.
        std::string m_Failure;

        bool m_Valid = false;
    };

    /// Loads, runs and updates the scripts attached to a world's entities.
    class ScriptEngine
    {
    public:
        ScriptEngine();
        ~ScriptEngine();

        ScriptEngine(const ScriptEngine&) = delete;
        ScriptEngine& operator=(const ScriptEngine&) = delete;
        ScriptEngine(ScriptEngine&&) = delete;
        ScriptEngine& operator=(ScriptEngine&&) = delete;

        /// Opens the Lua state and installs the engine's bindings.
        ///
        /// Fails if Lua cannot be initialised, which leaves the engine inert but
        /// otherwise usable: a project with no scripts still runs.
        Result<void> Initialise();

        /// Closes the Lua state. Safe to call more than once.
        void Shutdown();

        [[nodiscard]] bool IsInitialised() const noexcept { return m_State != nullptr; }

        /// The underlying Lua state, for the bindings and for tests.
        [[nodiscard]] lua_State* GetState() const noexcept { return m_State; }

        /// Reads a script file, caching its source by path.
        ///
        /// Fails with FileNotFound or FileRead if the file cannot be read. A file
        /// that was read once is never read again, even if it later changes; call
        /// `UnloadScript` to pick up an edit.
        Result<ScriptSource*> LoadScript(std::string_view path);

        /// Drops a script from the cache so the next load re-reads it from disk.
        ///
        /// Scripts already running from that file keep running; the change takes
        /// effect the next time an entity starts the script.
        void UnloadScript(std::string_view path);

        /// Runs a script file with no entity bound. Used to define globals.
        Result<void> ExecuteFile(std::string_view path);

        /// Runs a Lua string. Used by tests and by the editor's console.
        Result<void> ExecuteString(std::string_view code, std::string_view chunkName = "=(console)");

        /// Resolves the script directory used to interpret relative paths.
        void SetScriptDirectory(std::string directory) { m_ScriptDirectory = std::move(directory); }
        [[nodiscard]] const std::string& GetScriptDirectory() const noexcept { return m_ScriptDirectory; }

        // -------------------------------------------------------------- per-frame

        /// Starts every script whose entity has just gained a `ScriptComponent`.
        ///
        /// Called once per frame before `Update`. Starting separately from
        /// updating means a script cannot observe itself half-initialised.
        void Start(World& world);

        /// Calls `OnUpdate` on every started script.
        ///
        /// `deltaTime` is in seconds. A script that raises an error is disabled for
        /// the rest of the run and reported through `GetErrors()`.
        void Update(World& world, float deltaTime);

        /// Calls `OnDestroy` on every started script, oldest first.
        ///
        /// Scripts are stopped before their entities are torn down so that a
        /// script may still read its own components while shutting down.
        void Stop(World& world);

        /// Stops and forgets every script, leaving the world untouched.
        void Reset();

        // ------------------------------------------------------------ diagnostics

        /// Errors raised since the last `ClearErrors`, oldest first.
        ///
        /// The engine logs each of these as it happens; the list exists so that the
        /// editor's console and the test suite can assert on them.
        [[nodiscard]] const std::vector<std::string>& GetErrors() const noexcept { return m_Errors; }

        void ClearErrors() { m_Errors.clear(); }

        /// Number of scripts currently started.
        [[nodiscard]] std::size_t GetActiveScriptCount() const noexcept { return m_Active.size(); }

        /// True when the script on an entity has been started and not yet stopped.
        [[nodiscard]] bool IsScriptActive(Entity entity) const noexcept;

        // -------------------------------------------------------- global accessors

        /// Reads a global from the shared engine environment.
        [[nodiscard]] double GetGlobalNumber(std::string_view name, double fallback = 0.0) const;

        /// Reads a shared global as a string.
        ///
        /// Returns "" when the global is absent. A number is stringified, because
        /// that is what a script would observe when it concatenated the value.
        [[nodiscard]] std::string GetGlobalString(std::string_view name) const;

        /// Reads a shared global as a boolean.
        [[nodiscard]] bool GetGlobalBool(std::string_view name, bool fallback = false) const;

        /// Reads a shared global as a count of table entries.
        [[nodiscard]] std::size_t GetGlobalTableLength(std::string_view name) const;

        /// Reads a global from one script instance's own environment.
        ///
        /// Returns the fallback when the instance or the name is absent.
        [[nodiscard]] double GetInstanceGlobalNumber(Entity entity, std::string_view name,
                                                    double fallback = 0.0) const;

        /// Reads a string global from one script instance's environment.
        [[nodiscard]] std::string GetInstanceGlobalString(Entity entity, std::string_view name) const;

        /// Reads a boolean global from one script instance's environment.
        [[nodiscard]] bool GetInstanceGlobalBool(Entity entity, std::string_view name, bool fallback = false) const;

        // ------------------------------------------------------- state for bindings

        /// The world scripts are currently bound to, or nullptr between frames.
        [[nodiscard]] World* GetBoundWorld() const noexcept { return m_BoundWorld; }

        /// Seconds since the previous frame, or 0 before the first frame.
        [[nodiscard]] float GetDeltaTime() const noexcept { return m_DeltaTime; }

        /// Sets the engine-wide clock that `time.now()` reports.
        ///
        /// The engine drives this from its frame loop; scripts never set it.
        void SetElapsedTime(double seconds) noexcept { m_ElapsedTime = seconds; }

        /// Seconds the engine has been running, as `time.now()` reports them.
        [[nodiscard]] double GetElapsedTime() const noexcept { return m_ElapsedTime; }

        /// Pushes an entity handle onto the Lua stack.
        ///
        /// The bindings use this to hand scripts an `entity` value. The pushed value
        /// is userdata holding a byte-for-byte copy of the handle, so a script
        /// observes exactly the handle's layout and no allocation happens per call.
        void PushEntity(Entity entity);

    private:
        /// A script that has been started and is waiting to be updated.
        struct ActiveScript
        {
            Entity Entity;
            std::string Path;

            /// Reference to this instance's private Lua environment. Each started
            /// script gets its own, so globals are not shared between entities.
            int EnvironmentRef = LuaNoRef;

            bool Enabled = true;
        };

        /// Converts a Lua error into a reportable message and clears the error.
        [[nodiscard]] std::string TakeError();

        /// Resolves a script path against the script directory.
        [[nodiscard]] FilePath ResolvePath(std::string_view path) const;

        /// Records an error against a script, logs it and returns it.
        ///
        /// The entity's name is preferred over its handle when it has one: a person
        /// reading the editor's console is looking for the object they can select,
        /// not for a slot number. The returned error carries the same text for
        /// callers that propagate it; sites that have already handled the failure
        /// discard it.
        Error ReportError(std::string_view path, Entity entity, std::string_view message);

        /// Starts the script named by an entity's `ScriptComponent`.
        void StartScript(Entity entity, const std::string& path);

        /// Calls one entry point on a script instance, if it defines it.
        ///
        /// Returns false if the instance does not define `entryPoint` or if calling
        /// it raised an error, which the caller reports.
        bool CallEntryPoint(ActiveScript& script, const char* entryPoint, int argumentCount,
                            const char* firstArgumentKind);

        /// Looks up `name` in a script instance's environment and leaves it on the
        /// stack. Returns false if the instance has stopped.
        bool PushInstanceGlobal(ActiveScript& script, const char* name);

        lua_State* m_State = nullptr;
        std::string m_ScriptDirectory;
        std::unordered_map<std::string, std::unique_ptr<ScriptSource>> m_Sources;
        std::vector<ActiveScript> m_Active;
        std::vector<std::string> m_Errors;

        /// The world scripts are currently bound to.
        World* m_BoundWorld = nullptr;

        /// Seconds since the previous frame, reported to scripts as `time.delta()`.
        float m_DeltaTime = 0.0f;

        /// Seconds since the engine started, reported to scripts as `time.now()`.
        double m_ElapsedTime = 0.0;
    };

    /// Returns the engine whose state the current thread is running scripts for.
    ///
    /// There is exactly one script engine per process, so a single pointer is
    /// sufficient. The bindings use it to reach the engine without every binding
    /// function taking an extra hidden parameter.
    [[nodiscard]] ScriptEngine* GetActiveEngine() noexcept;

    /// Names of the Lua functions the engine calls on a script.
    namespace ScriptEntryPoints
    {
        inline constexpr const char* OnStart = "OnStart";
        inline constexpr const char* OnUpdate = "OnUpdate";
        inline constexpr const char* OnDestroy = "OnDestroy";
    }
}
