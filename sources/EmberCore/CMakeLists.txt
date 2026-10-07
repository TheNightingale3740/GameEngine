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
        "${miniaudio_SOURCE_DIR}"
)

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
