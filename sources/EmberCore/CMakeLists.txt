#[[
    EmberCore - the static engine library shared by the editor and the runtime.

    Everything under Source/ is grouped into the core static library. The editor
    and the runtime are thin executables on top of it, which is what makes the
    two agree on scene format, script bindings and asset layout by construction.
]]

file(GLOB_RECURSE EMBER_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp")

add_library(EmberCore STATIC ${EMBER_CORE_SOURCES})

target_include_directories(EmberCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

# Jolt's headers are included by exactly one engine translation unit, and their
# warnings are its own business rather than the engine's: without this they are
# compiled with the engine's strict warning set and fail the build on code the
# engine cannot change.
target_include_directories(EmberCore SYSTEM PRIVATE "${jolt_SOURCE_DIR}")

# miniaudio is a single header with its implementation compiled into one
# translation unit. It is included as a system header for the same reason Jolt's
# is: its warnings are its own, and the engine cannot fix them.
target_include_directories(EmberCore SYSTEM PRIVATE "${miniaudio_SOURCE_DIR}")

target_link_libraries(EmberCore
    PUBLIC
        glm::glm
        lua::lua
        nvrhi
    PRIVATE
        Jolt
        glfw
        nvrhi_vk
)

target_link_libraries(EmberCore PUBLIC ${CMAKE_DL_LIBS})

# glm gates its `gtx` headers behind an opt-in. The engine uses
# `glm::normalize` and `glm::angleAxis` from that set.
target_compile_definitions(EmberCore PUBLIC GLM_ENABLE_EXPERIMENTAL)

if(APPLE)
    target_link_libraries(EmberCore PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
    )
endif()

ember_set_warnings(EmberCore)
