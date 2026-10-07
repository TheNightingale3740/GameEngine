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
    ///   Log.info(...)            writes an info-level log line
    ///   Log.warn(...)            writes a warning
    ///   Log.error(...)           writes an error
    ///
    ///   Time.now()               seconds since the engine started
    ///   Time.delta()             seconds since the previous frame
    ///
    ///   Entity.is_valid(e)       whether a handle refers to a live entity
    ///   Entity.name(e)           the entity's name
    ///   Entity.set_name(e, name)
    ///   Entity.destroy(e)        destroys an entity and its subtree
    ///   Entity.children(e)       array of child handles
    ///   Entity.parent(e)         parent handle, or nil
    ///   Entity.set_parent(child, parent)
    ///   Entity.create(name)      creates an entity, returning its handle
    ///
    ///   Component.has(e, name)   whether the entity has that component
    ///   Component.get(e, name)   the component as a table, or nil
    ///   Component.set(e, name, table)
    ///   Component.remove(e, name)
    ///   Component.types(e)       array of component names on the entity
    ///
    ///   Math.vec3(x, y, z)       a table with x, y, z
    ///   Math.quat(x, y, z, w)    a table with x, y, z, w
    ///   Math.length(x, y, z)     length of a vector
    ///   Math.normalize(x, y, z)  a unit vector
    ///
    /// The tables are capitalised, as Lua's own library tables are. A script's
    /// entry points take parameters named after them, and a script whose parameter
    /// is called `entity` would shadow the table it is trying to call.
    ///
    /// A component table uses the field names from the component's registration,
    /// with vectors as `{x=, y=, z=}` tables and enums as their string names.
    void RegisterScriptBindings(ScriptEngine& engine);
}
