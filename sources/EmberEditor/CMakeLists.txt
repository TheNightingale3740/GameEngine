#[[
    EmberEditor - the authoring executable.

    Owns the interactive viewport, the inspector and the scene hierarchy, and
    drives the export pipeline that produces a shipped EmberRuntime build.
]]

add_executable(EmberEditor
    Source/Main.cpp
)

target_link_libraries(EmberEditor PRIVATE EmberCore)

ember_set_warnings(EmberEditor)
