#[[
    Ember test suite.

    Every test binary links EmberCore and GoogleTest. Tests are grouped by
    subsystem so that a failure names the area that broke, and each binary is
    registered with CTest individually.
]]

set(EMBER_TEST_SUITES
    Core
    Math
    Logging
    Memory
    FileSystem
    Ecs
    Serialization
    Scripting
    Physics
    Scene
    Audio
)

foreach(suite IN LISTS EMBER_TEST_SUITES)
    file(GLOB suite_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Unit/${suite}/*.cpp")

    if(NOT suite_sources)
        continue()
    endif()

    add_executable(EmberTest${suite} ${suite_sources})

    target_include_directories(EmberTest${suite} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

    target_link_libraries(EmberTest${suite} PRIVATE
        EmberCore
        GTest::gtest_main
    )

    ember_set_warnings(EmberTest${suite})

    add_test(NAME ${suite} COMMAND EmberTest${suite})
endforeach()
