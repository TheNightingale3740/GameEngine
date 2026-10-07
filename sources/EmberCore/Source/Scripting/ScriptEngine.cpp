// Scripting/ScriptEngine.cpp

#include "Scripting/ScriptEngine.h"

#include <algorithm>

#include "Core/Logging/Log.h"
#include "Ecs/ComponentStorage.h"
#include "Ecs/Components.h"
#include "Scripting/ScriptBindings.h"

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace Ember
{
    namespace
    {
        /// The engine whose state the current thread is running scripts for.
        ///
        /// A Lua state has no user-data slot the engine owns, and the bindings need
        /// to reach the engine to log and to time. There is exactly one script
        /// engine per process, so a single pointer is sufficient and keeps the
        /// binding signatures to plain Lua arguments.
        ScriptEngine* s_ActiveEngine = nullptr;

        /// Key under which each instance's compiled chunk is stored in its own
        /// environment. Keeping the chunk there is what keeps it alive: Lua holds a
        /// reference through the table, so the engine never has to manage it.
        constexpr const char* ChunkKey = "__ember_chunk";
    }

    ScriptEngine* GetActiveEngine() noexcept
    {
        return s_ActiveEngine;
    }

    ScriptEngine::ScriptEngine() = default;

    ScriptEngine::~ScriptEngine()
    {
        Shutdown();
    }

    Result<void> ScriptEngine::Initialise()
    {
        if (m_State != nullptr)
        {
            return {};
        }

        m_State = luaL_newstate();
        if (m_State == nullptr)
        {
            return {ErrorCode::ScriptError, "Could not create a Lua state"};
        }

        luaL_openlibs(m_State);

        s_ActiveEngine = this;
        RegisterScriptBindings(*this);

        EMBER_LOG_DEBUG("Scripting initialised (Lua {})", LUA_VERSION_MAJOR "." LUA_VERSION_MINOR);
        return {};
    }

    void ScriptEngine::Shutdown()
    {
        Reset();

        if (m_State == nullptr)
        {
            return;
        }

        if (s_ActiveEngine == this)
        {
            s_ActiveEngine = nullptr;
        }

        lua_close(m_State);
        m_State = nullptr;
    }

    FilePath ScriptEngine::ResolvePath(std::string_view path) const
    {
        const FilePath candidate(path);
        if (candidate.is_absolute() || m_ScriptDirectory.empty())
        {
            return candidate;
        }

        return FilePath(m_ScriptDirectory) / candidate;
    }

    std::string ScriptEngine::TakeError()
    {
        if (m_State == nullptr)
        {
            return "scripting is not initialised";
        }

        const char* message = lua_tostring(m_State, -1);
        const std::string text = message != nullptr ? message : "unknown Lua error";

        lua_pop(m_State, 1);
        return text;
    }

    Error ScriptEngine::ReportError(std::string_view path, Entity entity, std::string_view message)
    {
        std::string subject(path);

        if (World* world = m_BoundWorld; entity.IsValid() && world != nullptr && world->IsAlive(entity))
        {
            const std::string& name = world->GetName(entity);
            subject += " (" + (name.empty() ? ToString(entity) : name) + ")";
        }
        else if (entity.IsValid())
        {
            subject += " (" + ToString(entity) + ")";
        }

        const std::string report = subject + ": " + std::string(message);

        EMBER_LOG_ERROR("{}", report);
        m_Errors.push_back(report);

        return Error(ErrorCode::ScriptError, report);
    }

    Result<ScriptSource*> ScriptEngine::LoadScript(std::string_view path)
    {
        if (m_State == nullptr)
        {
            return {ErrorCode::ScriptError, "Scripting is not initialised"};
        }

        const std::string key(path);

        if (const auto cached = m_Sources.find(key); cached != m_Sources.end())
        {
            if (!cached->second->IsValid())
            {
                // The failure was already reported when the file was first read.
                // Reporting it again on every lookup would fill the console with the
                // same line once per entity per frame.
                return Error(ErrorCode::FileRead, cached->second->m_Failure);
            }

            return cached->second.get();
        }

        auto source = std::make_unique<ScriptSource>();
        source->m_Path = key;

        const FilePath resolved = ResolvePath(path);
        Result<std::string> text = FileSystem::ReadTextFile(resolved);

        if (text.IsFailure())
        {
            // The entry is cached as failed, with the reason, so that the file is
            // not re-read from disk on every frame for every entity that names it
            // and the reason is not re-reported every time.
            source->m_Failure = text.GetError().Message;
            m_Sources.emplace(key, std::move(source));

            EMBER_LOG_ERROR("{}: {}", key, text.GetError().Message);
            m_Errors.push_back(key + ": " + text.GetError().Message);

            // The caller sees the underlying I/O failure, so that a project can tell
            // "no such script" apart from "the script is broken".
            return text.GetError();
        }

        source->m_Text = std::move(text).Value();
        source->m_ChunkName = "@" + FileSystem::ToString(resolved);
        source->m_Valid = true;

        ScriptSource* cached = source.get();
        m_Sources.emplace(key, std::move(source));

        EMBER_LOG_DEBUG("Read script '{}'", key);
        return cached;
    }

    void ScriptEngine::UnloadScript(std::string_view path)
    {
        m_Sources.erase(std::string(path));
    }

    Result<void> ScriptEngine::ExecuteFile(std::string_view path)
    {
        if (m_State == nullptr)
        {
            return {ErrorCode::ScriptError, "Scripting is not initialised"};
        }

        Result<ScriptSource*> source = LoadScript(path);
        if (source.IsFailure())
        {
            return source.GetError();
        }

        const ScriptSource& script = *source.Value();
        const std::string chunkName = script.GetChunkName();

        if (luaL_loadbuffer(m_State, script.GetText().data(), script.GetText().size(), chunkName.c_str()) != LUA_OK)
        {
            return ReportError(path, Entity::Null(), TakeError());
        }

        if (lua_pcall(m_State, 0, 0, 0) != LUA_OK)
        {
            return ReportError(path, Entity::Null(), TakeError());
        }

        return {};
    }

    Result<void> ScriptEngine::ExecuteString(std::string_view code, std::string_view chunkName)
    {
        if (m_State == nullptr)
        {
            return {ErrorCode::ScriptError, "Scripting is not initialised"};
        }

        const std::string name(chunkName);

        if (luaL_loadbuffer(m_State, code.data(), code.size(), name.c_str()) != LUA_OK)
        {
            const std::string error = TakeError();
            EMBER_LOG_ERROR("{}", error);
            m_Errors.push_back(error);
            return Error(ErrorCode::ScriptError, error);
        }

        if (lua_pcall(m_State, 0, 0, 0) != LUA_OK)
        {
            const std::string error = TakeError();
            EMBER_LOG_ERROR("{}", error);
            m_Errors.push_back(error);
            return Error(ErrorCode::ScriptError, error);
        }

        return {};
    }

    bool ScriptEngine::IsScriptActive(Entity entity) const noexcept
    {
        return std::any_of(m_Active.begin(), m_Active.end(),
                           [entity](const ActiveScript& script) { return script.Entity == entity; });
    }

    void ScriptEngine::Start(World& world)
    {
        if (m_State == nullptr)
        {
            return;
        }

        m_BoundWorld = &world;

        // Candidates are collected first: starting a script runs arbitrary Lua,
        // which may itself attach or detach scripts and so invalidate an
        // in-progress iteration over the world's components.
        std::vector<Entity> candidates;
        world.Each<ScriptComponent>([&](const ScriptComponent& script, Entity entity)
        {
            if (!script.Enabled || script.ScriptPath.empty() || IsScriptActive(entity))
            {
                return;
            }

            candidates.push_back(entity);
        });

        for (const Entity entity : candidates)
        {
            // The component may have been removed by an earlier script in this same
            // batch, so it is re-read rather than assumed.
            const ScriptComponent* script = world.TryGetComponent<ScriptComponent>(entity);
            if (script != nullptr)
            {
                StartScript(entity, script->ScriptPath);
            }
        }
    }

    void ScriptEngine::StartScript(Entity entity, const std::string& path)
    {
        Result<ScriptSource*> source = LoadScript(path);
        if (source.IsFailure())
        {
            return;
        }

        const ScriptSource& script = *source.Value();

        // A fresh environment per instance: it inherits the engine's globals
        // through a metatable, so bindings and standard library are reachable,
        // but anything the script assigns is private to this entity.
        lua_newtable(m_State);

        lua_newtable(m_State);
        lua_pushglobaltable(m_State);
        lua_setfield(m_State, -2, "__index");
        lua_setmetatable(m_State, -2);

        ActiveScript active;
        active.Entity = entity;
        active.Path = path;
        active.EnvironmentRef = luaL_ref(m_State, LUA_REGISTRYINDEX);

        m_Active.push_back(active);
        ActiveScript& instance = m_Active.back();

        const std::string chunkName = script.GetChunkName();
        if (luaL_loadbuffer(m_State, script.GetText().data(), script.GetText().size(), chunkName.c_str()) != LUA_OK)
        {
            const std::string error = TakeError();

            // Compilation fails for the file, not for the instance, so the file is
            // marked unreadable. Every later entity that names it fails the same way
            // without the console repeating the line.
            EMBER_LOG_ERROR("{}: {}", path, error);
            m_Errors.push_back(std::string(path) + ": " + error);
            m_Sources[path]->m_Valid = false;
            m_Sources[path]->m_Failure = error;

            m_Active.pop_back();
            luaL_unref(m_State, LUA_REGISTRYINDEX, instance.EnvironmentRef);
            return;
        }

        // Store the chunk inside its own environment first. The environment table
        // now holds the only reference to the chunk, so it stays alive for as long
        // as the instance runs without the engine managing its lifetime.
        //
        // Stack: [chunk] -> [chunk, env] -> [chunk, env, chunk] -> [chunk, env]
        lua_rawgeti(m_State, LUA_REGISTRYINDEX, instance.EnvironmentRef);
        lua_pushvalue(m_State, -2);
        lua_setfield(m_State, -2, ChunkKey);

        // Point the chunk's _ENV upvalue at that environment, so the script's global
        // assignments land in the instance's table rather than the engine's.
        // Stack: [chunk, env] -> [chunk]
        if (lua_setupvalue(m_State, -2, 1) == nullptr)
        {
            lua_pop(m_State, 2);
            luaL_unref(m_State, LUA_REGISTRYINDEX, instance.EnvironmentRef);
            m_Active.pop_back();
            ReportError(path, entity, "the chunk has no environment upvalue");
            return;
        }

        if (lua_pcall(m_State, 0, 0, 0) != LUA_OK)
        {
            const std::string error = TakeError();
            (void)ReportError(path, entity, error);

            // A script that cannot run its body is not left half-started: it is
            // dropped so that it is not updated either.
            luaL_unref(m_State, LUA_REGISTRYINDEX, instance.EnvironmentRef);
            m_Active.pop_back();
            return;
        }

        // A script with no OnStart is still started: it may define OnUpdate or
        // OnDestroy, and its body has already run.
        if (PushInstanceGlobal(instance, ScriptEntryPoints::OnStart))
        {
            PushEntity(entity);

            if (lua_pcall(m_State, 1, 0, 0) != LUA_OK)
            {
                const std::string error = TakeError();
                (void)ReportError(path, entity, error);

                luaL_unref(m_State, LUA_REGISTRYINDEX, instance.EnvironmentRef);
                m_Active.pop_back();
            }
        }
    }

    bool ScriptEngine::PushInstanceGlobal(ActiveScript& script, const char* name)
    {
        if (m_State == nullptr || script.EnvironmentRef == LUA_NOREF)
        {
            return false;
        }

        lua_rawgeti(m_State, LUA_REGISTRYINDEX, script.EnvironmentRef);
        if (!lua_istable(m_State, -1))
        {
            lua_pop(m_State, 1);
            return false;
        }

        lua_getfield(m_State, -1, name);
        const bool isFunction = lua_isfunction(m_State, -1) != 0;

        // The environment table is left on the stack as the lookup context for the
        // call that follows, so that a function's own _ENV still resolves.
        if (!isFunction)
        {
            lua_pop(m_State, 2);
            return false;
        }

        lua_remove(m_State, -2);
        return true;
    }

    void ScriptEngine::Update(World& world, float deltaTime)
    {
        if (m_State == nullptr)
        {
            return;
        }

        m_BoundWorld = &world;
        m_DeltaTime = deltaTime;

        // A script may spawn or destroy entities, which can add to or remove from
        // the active list. The count is captured once so that scripts started by
        // this frame's own updates do not run until the next frame.
        const std::size_t count = m_Active.size();

        for (std::size_t i = 0; i < count; ++i)
        {
            ActiveScript& script = m_Active[i];

            if (!script.Enabled || !world.IsAlive(script.Entity))
            {
                continue;
            }

            const ScriptComponent* component = world.TryGetComponent<ScriptComponent>(script.Entity);
            if (component == nullptr || !component->Enabled)
            {
                continue;
            }

            if (!PushInstanceGlobal(script, ScriptEntryPoints::OnUpdate))
            {
                continue;
            }

            PushEntity(script.Entity);
            lua_pushnumber(m_State, static_cast<lua_Number>(deltaTime));

            if (lua_pcall(m_State, 2, 0, 0) != LUA_OK)
            {
                const std::string error = TakeError();
                (void)ReportError(script.Path, script.Entity, error);

                // The script is disabled rather than removed, so it stops running
                // but its entity and its other components are untouched.
                script.Enabled = false;
            }
        }
    }

    void ScriptEngine::Stop(World& world)
    {
        m_BoundWorld = &world;

        // Oldest first, so a script that spawned another entity stops the parent
        // before the child, matching the order in which they started.
        for (ActiveScript& script : m_Active)
        {
            if (m_State == nullptr)
            {
                break;
            }

            if (PushInstanceGlobal(script, ScriptEntryPoints::OnDestroy))
            {
                PushEntity(script.Entity);

                if (lua_pcall(m_State, 1, 0, 0) != LUA_OK)
                {
                    (void)ReportError(script.Path, script.Entity, TakeError());
                }
            }

            if (script.EnvironmentRef != LuaNoRef)
            {
                luaL_unref(m_State, LUA_REGISTRYINDEX, script.EnvironmentRef);
                script.EnvironmentRef = LUA_NOREF;
            }
        }

        m_Active.clear();
    }

    void ScriptEngine::Reset()
    {
        if (m_State != nullptr)
        {
            for (ActiveScript& script : m_Active)
            {
                if (script.EnvironmentRef != LuaNoRef)
                {
                    luaL_unref(m_State, LUA_REGISTRYINDEX, script.EnvironmentRef);
                }
            }
        }

        m_Active.clear();
        m_Sources.clear();
        m_Errors.clear();
        m_BoundWorld = nullptr;
        m_DeltaTime = 0.0f;
    }

    namespace
    {
        /// Pushes a value from a Lua global table, returning false when absent.
        ///
        /// `index` is a stack-relative table reference; the value is left on top.
        bool PushGlobalValue(lua_State* state, int index, const std::string& name)
        {
            lua_getfield(state, index, name.c_str());
            if (lua_isnil(state, -1))
            {
                lua_pop(state, 1);
                return false;
            }

            return true;
        }
    }

    double ScriptEngine::GetGlobalNumber(std::string_view name, double fallback) const
    {
        if (m_State == nullptr)
        {
            return fallback;
        }

        lua_pushglobaltable(m_State);

        const bool present = PushGlobalValue(m_State, -1, std::string(name));
        const bool isNumber = present && lua_isnumber(m_State, -1) != 0;
        const double value = isNumber ? lua_tonumber(m_State, -1) : fallback;

        if (present)
        {
            lua_pop(m_State, 1);
        }

        lua_pop(m_State, 1);
        return value;
    }

    std::string ScriptEngine::GetGlobalString(std::string_view name) const
    {
        if (m_State == nullptr)
        {
            return {};
        }

        lua_pushglobaltable(m_State);

        std::string value;
        if (PushGlobalValue(m_State, -1, std::string(name)) && lua_type(m_State, -1) == LUA_TSTRING)
        {
            std::size_t length = 0;
            const char* text = lua_tolstring(m_State, -1, &length);
            value.assign(text != nullptr ? text : "", length);
            lua_pop(m_State, 1);
        }

        lua_pop(m_State, 1);
        return value;
    }

    bool ScriptEngine::GetGlobalBool(std::string_view name, bool fallback) const
    {
        if (m_State == nullptr)
        {
            return fallback;
        }

        lua_pushglobaltable(m_State);

        bool value = fallback;
        if (PushGlobalValue(m_State, -1, std::string(name)) && lua_type(m_State, -1) == LUA_TBOOLEAN)
        {
            value = lua_toboolean(m_State, -1) != 0;
            lua_pop(m_State, 1);
        }

        lua_pop(m_State, 1);
        return value;
    }

    std::size_t ScriptEngine::GetGlobalTableLength(std::string_view name) const
    {
        if (m_State == nullptr)
        {
            return 0;
        }

        lua_pushglobaltable(m_State);

        std::size_t length = 0;
        if (PushGlobalValue(m_State, -1, std::string(name)) && lua_istable(m_State, -1))
        {
            length = static_cast<std::size_t>(lua_rawlen(m_State, -1));
            lua_pop(m_State, 1);
        }

        lua_pop(m_State, 1);
        return length;
    }

    bool ScriptEngine::GetInstanceGlobalBool(Entity entity, std::string_view name, bool fallback) const
    {
        const auto found = std::find_if(m_Active.begin(), m_Active.end(),
                                        [entity](const ActiveScript& script) { return script.Entity == entity; });

        if (m_State == nullptr || found == m_Active.end() || found->EnvironmentRef == LuaNoRef)
        {
            return fallback;
        }

        lua_rawgeti(m_State, LUA_REGISTRYINDEX, found->EnvironmentRef);

        bool value = fallback;
        if (PushGlobalValue(m_State, -1, std::string(name)))
        {
            if (lua_type(m_State, -1) == LUA_TBOOLEAN)
            {
                value = lua_toboolean(m_State, -1) != 0;
            }

            lua_pop(m_State, 1);
        }

        lua_pop(m_State, 1);
        return value;
    }

    double ScriptEngine::GetInstanceGlobalNumber(Entity entity, std::string_view name, double fallback) const
    {
        const auto found = std::find_if(m_Active.begin(), m_Active.end(),
                                        [entity](const ActiveScript& script) { return script.Entity == entity; });

        if (m_State == nullptr || found == m_Active.end() || found->EnvironmentRef == LuaNoRef)
        {
            return fallback;
        }

        lua_rawgeti(m_State, LUA_REGISTRYINDEX, found->EnvironmentRef);

        double value = fallback;
        if (PushGlobalValue(m_State, -1, std::string(name)))
        {
            if (lua_isnumber(m_State, -1))
            {
                value = lua_tonumber(m_State, -1);
            }

            lua_pop(m_State, 1);
        }

        lua_pop(m_State, 1);
        return value;
    }

    std::string ScriptEngine::GetInstanceGlobalString(Entity entity, std::string_view name) const
    {
        const auto found = std::find_if(m_Active.begin(), m_Active.end(),
                                        [entity](const ActiveScript& script) { return script.Entity == entity; });

        if (m_State == nullptr || found == m_Active.end() || found->EnvironmentRef == LuaNoRef)
        {
            return {};
        }

        lua_rawgeti(m_State, LUA_REGISTRYINDEX, found->EnvironmentRef);

        std::string value;
        if (PushGlobalValue(m_State, -1, std::string(name)) && lua_type(m_State, -1) == LUA_TSTRING)
        {
            std::size_t length = 0;
            const char* text = lua_tolstring(m_State, -1, &length);
            value.assign(text != nullptr ? text : "", length);
            lua_pop(m_State, 1);
        }

        lua_pop(m_State, 1);
        return value;
    }
}
