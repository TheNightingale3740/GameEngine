// Scripting/ScriptBindings.cpp

#include "Scripting/ScriptBindings.h"

#include <cstring>
#include <string>

#include "Core/Logging/Log.h"
#include "Ecs/ComponentStorage.h"

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
        constexpr const char* EntityUserdataName = "ember.entity";

        /// Reads a script argument as an entity handle.
        ///
        /// The userdata is a byte-for-byte copy of the handle, so a script observes
        /// exactly the handle's own layout with nothing padded in between.
        [[nodiscard]] Entity CheckedEntity(lua_State* state, int index)
        {
            const void* value = luaL_checkudata(state, index, EntityUserdataName);

            Entity handle{};
            std::memcpy(&handle, value, sizeof(Entity));

            return handle;
        }

        /// Reads a script argument as a non-null entity handle.
        [[nodiscard]] Entity CheckedLiveEntity(lua_State* state, int index)
        {
            const Entity entity = CheckedEntity(state, index);

            ScriptEngine* engine = GetActiveEngine();
            World* world = engine != nullptr ? engine->GetBoundWorld() : nullptr;

            if (world == nullptr || !world->IsAlive(entity))
            {
                luaL_error(state, "argument #%d is not a live entity", index);
            }

            return entity;
        }

        [[nodiscard]] World* BoundWorld() noexcept
        {
            ScriptEngine* engine = GetActiveEngine();
            return engine != nullptr ? engine->GetBoundWorld() : nullptr;
        }

        /// Pushes a Lua number. Lua's only numeric type is a double, so an engine
        /// float is widened on the way in; the round trip back to float happens in
        /// `ReadVec3` and friends.
        void PushNumber(lua_State* state, float value)
        {
            lua_pushnumber(state, static_cast<lua_Number>(value));
        }

        void PushVec3(lua_State* state, const Vec3& value)
        {
            lua_createtable(state, 0, 3);

            PushNumber(state, value.x);
            lua_setfield(state, -2, "x");
            PushNumber(state, value.y);
            lua_setfield(state, -2, "y");
            PushNumber(state, value.z);
            lua_setfield(state, -2, "z");
        }

        void PushQuat(lua_State* state, const Quat& value)
        {
            lua_createtable(state, 0, 4);

            PushNumber(state, value.x);
            lua_setfield(state, -2, "x");
            PushNumber(state, value.y);
            lua_setfield(state, -2, "y");
            PushNumber(state, value.z);
            lua_setfield(state, -2, "z");
            PushNumber(state, value.w);
            lua_setfield(state, -2, "w");
        }

        /// Reads a table with x, y, z members as a vector.
        [[nodiscard]] Vec3 ReadVec3(lua_State* state, int index, Vec3 fallback)
        {
            if (lua_isnoneornil(state, index))
            {
                return fallback;
            }

            luaL_checktype(state, index, LUA_TTABLE);

            lua_getfield(state, index, "x");
            lua_getfield(state, index, "y");
            lua_getfield(state, index, "z");

            const Vec3 result(static_cast<float>(lua_tonumber(state, -3)),
                              static_cast<float>(lua_tonumber(state, -2)),
                              static_cast<float>(lua_tonumber(state, -1)));

            lua_pop(state, 3);
            return result;
        }

        [[nodiscard]] Quat ReadQuat(lua_State* state, int index, Quat fallback)
        {
            if (lua_isnoneornil(state, index))
            {
                return fallback;
            }

            luaL_checktype(state, index, LUA_TTABLE);

            lua_getfield(state, index, "x");
            lua_getfield(state, index, "y");
            lua_getfield(state, index, "z");
            lua_getfield(state, index, "w");

            const Quat result(static_cast<float>(lua_tonumber(state, -4)),
                              static_cast<float>(lua_tonumber(state, -3)),
                              static_cast<float>(lua_tonumber(state, -2)),
                              static_cast<float>(lua_tonumber(state, -1)));

            lua_pop(state, 4);
            return result;
        }

        /// Pushes a component's reflected fields as a table.
        ///
        /// Vectors become `{x=, y=, z=}` tables and enums become their string
        /// names, which is the same shape `component.set` accepts.
        void PushComponent(lua_State* state, const ComponentTypeInfo& info, const void* component)
        {
            lua_createtable(state, 0, static_cast<int>(info.Properties.size()));

            for (const Property& property : info.Properties)
            {
                const auto* base = static_cast<const std::byte*>(component) + property.Offset;

                switch (property.Type)
                {
                    case PropertyType::Bool:
                        lua_pushboolean(state, *reinterpret_cast<const bool*>(base));
                        break;

                    case PropertyType::Int:
                        lua_pushinteger(state, static_cast<lua_Integer>(*reinterpret_cast<const std::int32_t*>(base)));
                        break;

                    case PropertyType::Float:
                        PushNumber(state, *reinterpret_cast<const float*>(base));
                        break;

                    case PropertyType::Vec2:
                    {
                        const Vec2& value = *reinterpret_cast<const Vec2*>(base);
                        PushVec3(state, Vec3(value.x, value.y, 0.0f));
                        break;
                    }

                    case PropertyType::Vec3:
                        PushVec3(state, *reinterpret_cast<const Vec3*>(base));
                        break;

                    case PropertyType::Vec4:
                    {
                        const Vec4& value = *reinterpret_cast<const Vec4*>(base);
                        PushVec3(state, Vec3(value.x, value.y, value.z));
                        break;
                    }

                    case PropertyType::Quat:
                        PushQuat(state, *reinterpret_cast<const Quat*>(base));
                        break;

                    case PropertyType::String:
                    {
                        const std::string& text = *reinterpret_cast<const std::string*>(base);
                        lua_pushlstring(state, text.data(), text.size());
                        break;
                    }

                    case PropertyType::Enum:
                    {
                        const std::int32_t value = *reinterpret_cast<const std::int32_t*>(base);
                        const std::string_view name = FindEnumName(property.EnumValues, value);
                        lua_pushlstring(state, name.data(), name.size());
                        break;
                    }
                }

                lua_setfield(state, -2, property.Name.c_str());
            }
        }

        /// Applies a Lua table's fields to a component.
        ///
        /// Unknown fields are reported and skipped, so a typo in a script produces
        /// a message rather than a silently ignored assignment.
        void ReadIntoComponent(lua_State* state, int index, const ComponentTypeInfo& info, void* component)
        {
            luaL_checktype(state, index, LUA_TTABLE);

            for (const Property& property : info.Properties)
            {
                lua_getfield(state, index, property.Name.c_str());
                if (lua_isnil(state, -1))
                {
                    lua_pop(state, 1);
                    continue;
                }

                auto* base = static_cast<std::byte*>(component) + property.Offset;

                switch (property.Type)
                {
                    case PropertyType::Bool:
                        *reinterpret_cast<bool*>(base) = lua_toboolean(state, -1) != 0;
                        break;

                    case PropertyType::Int:
                        *reinterpret_cast<std::int32_t*>(base) =
                            static_cast<std::int32_t>(static_cast<std::int64_t>(lua_tointeger(state, -1)));
                        break;

                    case PropertyType::Float:
                        *reinterpret_cast<float*>(base) = static_cast<float>(lua_tonumber(state, -1));
                        break;

                    case PropertyType::Vec2:
                    {
                        const Vec3 read = ReadVec3(state, lua_gettop(state), Vec3());
                        *reinterpret_cast<Vec2*>(base) = Vec2(read.x, read.y);
                        break;
                    }

                    case PropertyType::Vec3:
                        *reinterpret_cast<Vec3*>(base) = ReadVec3(state, lua_gettop(state), Vec3());
                        break;

                    case PropertyType::Vec4:
                    {
                        const Vec3 read = ReadVec3(state, lua_gettop(state), Vec3());
                        *reinterpret_cast<Vec4*>(base) = Vec4(read.x, read.y, read.z, 0.0f);
                        break;
                    }

                    case PropertyType::Quat:
                        *reinterpret_cast<Quat*>(base) = ReadQuat(state, lua_gettop(state), Quat(1.0f, 0.0f, 0.0f, 0.0f));
                        break;

                    case PropertyType::String:
                    {
                        std::size_t length = 0;
                        const char* text = lua_tolstring(state, -1, &length);
                        *reinterpret_cast<std::string*>(base) = text != nullptr ? std::string(text, length) : std::string();
                        break;
                    }

                    case PropertyType::Enum:
                    {
                        const char* text = lua_tostring(state, -1);
                        const int resolved = text != nullptr ? FindEnumValue(property.EnumValues, text) : -1;

                        if (resolved < 0)
                        {
                            luaL_error(state, "'%s' is not a valid value for field '%s'",
                                       text != nullptr ? text : "?", property.Name.c_str());
                        }

                        *reinterpret_cast<std::int32_t*>(base) = resolved;
                        break;
                    }
                }

                lua_pop(state, 1);
            }

            lua_pushnil(state);
            while (lua_next(state, index) != 0)
            {
                const char* key = lua_tostring(state, -2);
                if (key != nullptr && info.FindProperty(key) == nullptr)
                {
                    luaL_error(state, "component '%s' has no field named '%s'", info.Name.c_str(), key);
                }

                lua_pop(state, 1);
            }
        }

        // ------------------------------------------------------------------- log

        /// Joins every argument into one message and logs it at `level`.
        ///
        /// Lua has no variadic logging, so the arguments are stringified and
        /// concatenated the way `print` would, with spaces between them.
        int JoinAndLog(lua_State* state, LogLevel level)
        {
            std::string message;
            const int count = lua_gettop(state);

            for (int i = 1; i <= count; ++i)
            {
                if (i > 1)
                {
                    message += ' ';
                }

                std::size_t length = 0;
                const char* text = lua_tolstring(state, i, &length);
                if (text != nullptr)
                {
                    message.append(text, length);
                    lua_pop(state, 1);
                }
            }

            Log::Get().Write(level, message);
            return 0;
        }

        int LogInfo(lua_State* state)
        {
            return JoinAndLog(state, LogLevel::Info);
        }

        int LogWarn(lua_State* state)
        {
            return JoinAndLog(state, LogLevel::Warn);
        }

        int LogError(lua_State* state)
        {
            return JoinAndLog(state, LogLevel::Error);
        }

        // ------------------------------------------------------------------ time

        int TimeNow(lua_State* state)
        {
            ScriptEngine* engine = GetActiveEngine();
            lua_pushnumber(state, static_cast<lua_Number>(engine != nullptr ? engine->GetElapsedTime() : 0.0));
            return 1;
        }

        int TimeDelta(lua_State* state)
        {
            ScriptEngine* engine = GetActiveEngine();
            const float delta = engine != nullptr ? engine->GetDeltaTime() : 0.0f;
            lua_pushnumber(state, static_cast<lua_Number>(delta));
            return 1;
        }

        // ---------------------------------------------------------------- entity

        int EntityIsValid(lua_State* state)
        {
            World* world = BoundWorld();
            lua_pushboolean(state, world != nullptr && world->IsAlive(CheckedEntity(state, 1)));
            return 1;
        }

        int EntityName(lua_State* state)
        {
            World* world = BoundWorld();
            const std::string name = world != nullptr ? world->GetName(CheckedEntity(state, 1)) : std::string();
            lua_pushlstring(state, name.data(), name.size());
            return 1;
        }

        int EntitySetName(lua_State* state)
        {
            World* world = BoundWorld();
            if (world != nullptr)
            {
                std::size_t length = 0;
                const char* text = luaL_checklstring(state, 2, &length);
                world->SetName(CheckedEntity(state, 1), std::string(text, length));
            }

            return 0;
        }

        int EntityDestroy(lua_State* state)
        {
            World* world = BoundWorld();
            if (world != nullptr)
            {
                world->DestroyEntity(CheckedEntity(state, 1));
            }

            return 0;
        }

        int EntityCreate(lua_State* state)
        {
            World* world = BoundWorld();
            if (world == nullptr)
            {
                luaL_error(state, "no world is bound; scripts can only create entities during a frame");
            }

            std::string name;
            if (!lua_isnoneornil(state, 1))
            {
                std::size_t length = 0;
                const char* text = luaL_checklstring(state, 1, &length);
                name.assign(text, length);
            }

            ScriptEngine* engine = GetActiveEngine();
            const Entity entity = world->CreateEntity(std::move(name));
            engine->PushEntity(entity);
            return 1;
        }

        int EntityChildren(lua_State* state)
        {
            World* world = BoundWorld();
            const std::vector<Entity> children = world != nullptr ? world->GetChildren(CheckedEntity(state, 1))
                                                                  : std::vector<Entity>();

            ScriptEngine* engine = GetActiveEngine();
            lua_createtable(state, static_cast<int>(children.size()), 0);

            for (std::size_t i = 0; i < children.size(); ++i)
            {
                engine->PushEntity(children[i]);
                lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
            }

            return 1;
        }

        int EntityParent(lua_State* state)
        {
            World* world = BoundWorld();
            const Entity parent = world != nullptr ? world->GetParent(CheckedEntity(state, 1)) : Entity::Null();

            if (parent == Entity::Null())
            {
                lua_pushnil(state);
                return 1;
            }

            GetActiveEngine()->PushEntity(parent);
            return 1;
        }

        int EntitySetParent(lua_State* state)
        {
            World* world = BoundWorld();
            if (world != nullptr)
            {
                const Entity child = CheckedEntity(state, 1);
                const Entity parent = lua_isnoneornil(state, 2) ? Entity::Null() : CheckedEntity(state, 2);
                world->SetParent(child, parent);
            }

            return 0;
        }

        // -------------------------------------------------------------- component

        int ComponentHas(lua_State* state)
        {
            World* world = BoundWorld();
            const Entity entity = CheckedEntity(state, 1);
            const ComponentTypeId type = ComponentRegistry::Get().FindByName(luaL_checkstring(state, 2));

            lua_pushboolean(state, type != InvalidComponentType && world != nullptr &&
                                        world->HasComponentByType(entity, type));
            return 1;
        }

        int ComponentGet(lua_State* state)
        {
            World* world = BoundWorld();
            const Entity entity = CheckedEntity(state, 1);
            const ComponentTypeId type = ComponentRegistry::Get().FindByName(luaL_checkstring(state, 2));

            const ComponentTypeInfo* info = ComponentRegistry::Get().Find(type);
            const void* component = (world != nullptr && info != nullptr) ? world->GetComponentByType(entity, type)
                                                                          : nullptr;

            if (component == nullptr)
            {
                lua_pushnil(state);
                return 1;
            }

            PushComponent(state, *info, component);
            return 1;
        }

        int ComponentSet(lua_State* state)
        {
            World* world = BoundWorld();
            if (world == nullptr)
            {
                luaL_error(state, "no world is bound; components can only be set during a frame");
            }

            const Entity entity = CheckedLiveEntity(state, 1);
            const ComponentTypeId type = ComponentRegistry::Get().FindByName(luaL_checkstring(state, 2));

            const ComponentTypeInfo* info = ComponentRegistry::Get().Find(type);
            if (info == nullptr)
            {
                luaL_error(state, "no component type named '%s' is registered", lua_tostring(state, 2));
            }

            void* component = world->GetComponentByType(entity, type);
            if (component == nullptr)
            {
                // Assigning to a component the entity does not have creates it,
                // matching the inspector's behaviour of adding on first edit.
                component = world->AddComponentFromJson(entity, info->Name, JsonValue(JsonValue::Object{}))
                                .IsSuccess()
                                ? world->GetComponentByType(entity, type)
                                : nullptr;

                if (component == nullptr)
                                {
                                    luaL_error(state, "could not add component '%s' to the entity", info->Name.c_str());
                                }
            }

            ReadIntoComponent(state, 3, *info, component);
            return 0;
        }

        int ComponentRemove(lua_State* state)
        {
            World* world = BoundWorld();
            const Entity entity = CheckedEntity(state, 1);
            const ComponentTypeId type = ComponentRegistry::Get().FindByName(luaL_checkstring(state, 2));

            lua_pushboolean(state, world != nullptr && world->RemoveComponentByType(entity, type));
            return 1;
        }

        int ComponentTypes(lua_State* state)
        {
            World* world = BoundWorld();
            const Entity entity = CheckedEntity(state, 1);
            const std::vector<ComponentTypeId> types = world != nullptr ? world->GetComponentTypes(entity)
                                                                        : std::vector<ComponentTypeId>();

            lua_createtable(state, static_cast<int>(types.size()), 0);
            for (std::size_t i = 0; i < types.size(); ++i)
            {
                const ComponentTypeInfo* info = ComponentRegistry::Get().Find(types[i]);
                if (info == nullptr)
                {
                    continue;
                }

                lua_pushlstring(state, info->Name.data(), info->Name.size());
                lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
            }

            return 1;
        }

        // ------------------------------------------------------------------ math

        int MathVec3(lua_State* state)
        {
            PushVec3(state, Vec3(static_cast<float>(luaL_checknumber(state, 1)),
                                 static_cast<float>(luaL_checknumber(state, 2)),
                                 static_cast<float>(luaL_checknumber(state, 3))));
            return 1;
        }

        int MathQuat(lua_State* state)
        {
            PushQuat(state, Quat(static_cast<float>(luaL_checknumber(state, 1)),
                                 static_cast<float>(luaL_checknumber(state, 2)),
                                 static_cast<float>(luaL_checknumber(state, 3)),
                                 static_cast<float>(luaL_checknumber(state, 4))));
            return 1;
        }

        int MathLength(lua_State* state)
        {
            const float x = static_cast<float>(luaL_checknumber(state, 1));
            const float y = static_cast<float>(luaL_checknumber(state, 2));
            const float z = static_cast<float>(luaL_checknumber(state, 3));
            lua_pushnumber(state, static_cast<lua_Number>(glm::length(Vec3(x, y, z))));
            return 1;
        }

        int MathNormalize(lua_State* state)
        {
            const Vec3 vector(static_cast<float>(luaL_checknumber(state, 1)),
                              static_cast<float>(luaL_checknumber(state, 2)),
                              static_cast<float>(luaL_checknumber(state, 3)));

            const float magnitude = glm::length(vector);
            if (magnitude <= 0.0f)
            {
                luaL_error(state, "cannot normalize a zero-length vector");
            }

            PushVec3(state, vector / magnitude);
            return 1;
        }

        /// Registers a table of C functions under `name` as a global.
        ///
        /// The tables are capitalised to follow Lua's own convention for library
        /// tables, and because a script's entry points take parameters named after
        /// them: a script that calls its parameter `entity` would otherwise shadow
        /// the table it is trying to call.
        void RegisterTable(lua_State* state, const char* name, const luaL_Reg* functions)
        {
            lua_newtable(state);
            luaL_setfuncs(state, functions, 0);
            lua_setglobal(state, name);
        }
    }

    void RegisterScriptBindings(ScriptEngine& engine)
    {
        lua_State* state = engine.GetState();
        if (state == nullptr)
        {
            return;
        }

        static const luaL_Reg logFunctions[]{
            {"info", LogInfo},
            {"warn", LogWarn},
            {"error", LogError},
            {nullptr, nullptr}
        };

        static const luaL_Reg timeFunctions[]{
            {"now", TimeNow},
            {"delta", TimeDelta},
            {nullptr, nullptr}
        };

        static const luaL_Reg entityFunctions[]{
            {"is_valid", EntityIsValid},
            {"name", EntityName},
            {"set_name", EntitySetName},
            {"destroy", EntityDestroy},
            {"create", EntityCreate},
            {"children", EntityChildren},
            {"parent", EntityParent},
            {"set_parent", EntitySetParent},
            {nullptr, nullptr}
        };

        static const luaL_Reg componentFunctions[]{
            {"has", ComponentHas},
            {"get", ComponentGet},
            {"set", ComponentSet},
            {"remove", ComponentRemove},
            {"types", ComponentTypes},
            {nullptr, nullptr}
        };

        static const luaL_Reg mathFunctions[]{
            {"vec3", MathVec3},
            {"quat", MathQuat},
            {"length", MathLength},
            {"normalize", MathNormalize},
            {nullptr, nullptr}
        };

        RegisterTable(state, "Log", logFunctions);
        RegisterTable(state, "Time", timeFunctions);
        RegisterTable(state, "Entity", entityFunctions);
        RegisterTable(state, "Component", componentFunctions);
        RegisterTable(state, "Math", mathFunctions);
    }

    void ScriptEngine::PushEntity(Entity entity)
    {
        if (m_State == nullptr)
        {
            return;
        }

        void* userdata = lua_newuserdatauv(m_State, sizeof(Entity), 0);
        std::memcpy(userdata, &entity, sizeof(Entity));

        // The metatable gives the handle a readable type name, so a script can
        // write `type(entity) == "ember.entity"`.
        luaL_getmetatable(m_State, EntityUserdataName);
        if (lua_isnil(m_State, -1))
        {
            lua_pop(m_State, 1);
            luaL_newmetatable(m_State, EntityUserdataName);

            lua_pushlstring(m_State, EntityUserdataName, std::strlen(EntityUserdataName));
            lua_setfield(m_State, -2, "__name");

            lua_pop(m_State, 1);
            luaL_getmetatable(m_State, EntityUserdataName);
        }

        lua_setmetatable(m_State, -2);
    }
}
