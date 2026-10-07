// Scripting/ScriptBindings.h
//
// The Lua API available to game scripts.
//
// Everything a script can do to the engine is registered here, which means the
// scripting surface can be read in one file and tested as a unit. Adding a
// capability means adding it to this header; nothing else exposes the engine to
// Lua.
//
// Scripts are handed an `entity` userdata as their first argument. The tables
// below operate on the world that owns that entity.

#pragma once

#include "Scripting/ScriptEngine.h"

namespace Ember
{
    /// Installs the engine's Lua API into a script engine's state.
    ///
    /// Defines:
    ///
    ///   log.info(...)            writes an info-level log line
    ///   log.warn(...)            writes a warning
    ///   log.error(...)           writes an error
    ///
    ///   time.now()               seconds since the engine started
    ///   time.delta()             seconds since the previous frame
    ///
    ///   entity.is_valid(e)       whether a handle refers to a live entity
    ///   entity.name(e)           the entity's name
    ///   entity.set_name(e, name)
    ///   entity.destroy(e)        destroys an entity and its subtree
    ///   entity.children(e)       array of child handles
    ///   entity.parent(e)         parent handle, or nil
    ///   entity.set_parent(child, parent)
    ///   entity.create(name)      creates an entity, returning its handle
    ///
    ///   component.has(e, name)   whether the entity has that component
    ///   component.get(e, name)   the component as a table, or nil
    ///   component.set(e, name, table)
    ///   component.remove(e, name)
    ///   component.types(e)       array of component names on the entity
    ///
    ///   math.vec3(x, y, z)      a table with x, y, z
    ///   math.quat(x, y, z, w)    a table with x, y, z, w
    ///   math.length(x, y, z)     length of a vector
    ///   math.normalize(x, y, z)  a unit vector
    ///
    /// A component table uses the field names from the component's registration,
    /// with vectors as `{x=, y=, z=}` tables and enums as their string names.
    void RegisterScriptBindings(ScriptEngine& engine);
}
