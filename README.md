# Ember

A 3D game engine in C++20, for Windows, macOS and Linux.

Ember is a static engine library (`EmberCore`) with two executables on top of it:

- **EmberEditor** — the authoring tool. It is a command surface rather than a
  windowed application, which is what makes it drivable by an AI agent and
  testable without a display.
- **EmberRuntime** — the shippable game. It plays an exported build and contains
  no editing code.

## Status

This is an engine under construction. The subsystems listed under "Status" in
[AGENTS.md](AGENTS.md#what-is-not-here-yet) are designed but not implemented —
in particular the Vulkan backend is not written, because it needs a machine with
a Vulkan runtime to validate. What *is* written is written to be production grade
and is covered by tests.

## Building

Requires CMake 3.24 or newer and a C++20 compiler. Every dependency is fetched
at configure time and pinned to an exact tag, so nothing needs installing first.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The first configure downloads a few hundred megabytes; later ones are fast.

## A first project

```sh
./build/bin/EmberEditor scripts/demo.cmd
./build/bin/EmberRuntime /tmp/emberdemo/dist
```

`scripts/demo.cmd` builds a project in `/tmp/emberdemo`, writes the scene that
exercises every engine feature into it, runs it for two seconds and exports a
shippable build to `/tmp/emberdemo/dist`. `EmberEditor` prints its full command
list when run with no arguments.

`EmberRuntime` takes the build directory, then optionally a frame count and the
seconds each frame advances:

```sh
./build/bin/EmberRuntime /tmp/emberdemo/dist 300 0.016
```

## Layout

| Path | What is in it |
| --- | --- |
| `sources/EmberCore/Source/Core` | Logging, math, JSON, filesystem, memory |
| `sources/EmberCore/Source/Ecs` | Entity handles, component registry, world |
| `sources/EmberCore/Source/Scene` | Scene and prefab serialisation |
| `sources/EmberCore/Source/Scripting` | Lua scripting and its bindings |
| `sources/EmberCore/Source/Physics` | Rigid bodies, on Jolt |
| `sources/EmberCore/Source/Audio` | Decoding and mixing, on miniaudio |
| `sources/EmberCore/Source/Renderer` | Meshes, materials, PBR maths, render queue |
| `sources/EmberCore/Source/Engine` | Project layout, export, frame loop, commands |
| `tests/Unit` | One test binary per subsystem |

## Reading further

[AGENTS.md](AGENTS.md) is the working guide: naming, the rules the code follows,
the bugs that have already been made here, and what is not implemented yet.

## Licence

MIT. See [LICENSE](LICENSE).
