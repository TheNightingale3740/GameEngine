#[[
    EmberRuntime - the shippable game executable.

    Loads an exported game and runs it. It contains no editing code and does not
    link any editor module, so a shipped build cannot be talked into changing the
    game it is running.
]]

add_executable(EmberRuntime
    Source/Main.cpp
)

target_link_libraries(EmberRuntime PRIVATE EmberCore)

ember_set_warnings(EmberRuntime)
