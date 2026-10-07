# EmberDependencies
#
# All third-party code is fetched at configure time with FetchContent and pinned
# to an exact git tag (or, for repositories without tags, an exact commit SHA).
# No dependency is ever vendored by hand, and no system package is assumed.

include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

# ----------------------------------------------------------------------------
# glm - mathematics
# ----------------------------------------------------------------------------

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.2
    GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(glm)

# ----------------------------------------------------------------------------
# Lua - scripting
# ----------------------------------------------------------------------------

# Lua has no CMakeLists.txt of its own; ember-no-cmake-build fetches the sources
# and EmberLua.cmake compiles them.
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
    GIT_SHALLOW    OFF
    SOURCE_SUBDIR  ember-no-cmake-build
)
FetchContent_MakeAvailable(lua)

include(EmberLua)

# ----------------------------------------------------------------------------
# Jolt Physics - physics simulation
# ----------------------------------------------------------------------------

set(JPH_ENABLE_64BIT OFF CACHE BOOL "" FORCE)
set(JPH_PROFILE_ENABLED OFF CACHE BOOL "" FORCE)
set(JPH_DEBUG_RENDERER OFF CACHE BOOL "" FORCE)
set(JPH_OBJECT_LAYER_BITS_16 OFF CACHE BOOL "" FORCE)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(USE_LARGE_PAGES OFF CACHE BOOL "" FORCE)
set(JPH_CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)

# Jolt keeps its CMake build under `Build/` rather than at the repository root.
FetchContent_Declare(jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.4.0
    GIT_SHALLOW    OFF
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(jolt)

# ----------------------------------------------------------------------------
# GoogleTest - unit testing
# ----------------------------------------------------------------------------

if(EMBER_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
        GIT_SHALLOW    OFF
    )
    FetchContent_MakeAvailable(googletest)
endif()

# ----------------------------------------------------------------------------
# miniaudio - audio decoding and playback
#
# miniaudio is a single-header library. Only its headers are fetched; the
# implementation is compiled in exactly one translation unit (Audio/AudioDevice.cpp)
# so that the global state it defines exists once per binary.
# ----------------------------------------------------------------------------

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.24
    GIT_SHALLOW    OFF
    SOURCE_SUBDIR  ember-no-cmake-build
)
FetchContent_MakeAvailable(miniaudio)

# ----------------------------------------------------------------------------
# GLFW - windowing and input
# ----------------------------------------------------------------------------

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(glfw)

# ----------------------------------------------------------------------------
# Vulkan Headers - required by NVRHI's Vulkan backend
# ----------------------------------------------------------------------------

FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.4.319
    GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(vulkan_headers)

# ----------------------------------------------------------------------------
# NVRHI - rendering abstraction over Vulkan
# ----------------------------------------------------------------------------

# The engine renders through Vulkan on every platform, so only NVRHI's Vulkan
# backend is built. The D3D backends and the validation layer would add build
# time and link-time weight for code the engine can never call.
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG        6b96fb03e07539f08327aea76c56d55f1de9d906
    GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(nvrhi)
