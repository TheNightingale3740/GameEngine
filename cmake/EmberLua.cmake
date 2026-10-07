#[[
    EmberLua

    Lua upstream ships no CMake build. This defines the `lua::lua` interface
    target from Lua's documented source list.

    Only the interpreter library is built. `lua.c` (the standalone interpreter)
    and `luac.c` (the compiler front-end) are excluded because both define a
    `main`, which would collide with the editor and runtime entry points.
]]

# The core interpreter, then the auxiliary library. The core alone provides
# lua_newstate and nothing else: every binding helper used by the engine lives in
# lauxlib, and the standard library modules in lbaselib, ltablib, lstrlib, loslib,
# lcorolib and loadlib.
set(EMBER_LUA_SOURCES
    lapi.c lcode.c lctype.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c
    lmem.c lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c
    lundump.c lvm.c lzio.c
    lauxlib.c lbaselib.c lcorolib.c linit.c loadlib.c loslib.c ltablib.c
    lstrlib.c lutf8lib.c lmathlib.c liolib.c ldblib.c
)

add_library(lua STATIC)

foreach(source IN LISTS EMBER_LUA_SOURCES)
    target_sources(lua PRIVATE "${lua_SOURCE_DIR}/${source}")
endforeach()

add_library(lua::lua ALIAS lua)

target_include_directories(lua SYSTEM INTERFACE
    "${lua_SOURCE_DIR}"
)

target_compile_features(lua INTERFACE c_std_11)

# Lua's public headers deliberately use constructs that trip the engine's strict
# warning set (unused parameters in its macros, in particular). They are third
# party code and are compiled as system headers rather than being suppressed.
target_compile_definitions(lua INTERFACE
    $<BUILD_INTERFACE:LUA_USE_MACOSX>
)

set_target_properties(lua PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
