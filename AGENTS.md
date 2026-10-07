# AGENTS.md

Guidance for anyone — human or agent — changing this repository.

Ember is a 3D game engine: a C++20 static library (`EmberCore`) plus two thin
executables on top of it. Read this before changing anything.

## The one-line summary

**Every subsystem is written so that the part that can be wrong without a
graphics device is separated from the part that cannot.** That separation is the
whole reason the engine is testable, and it is what new code should follow.

## Layout

```
cmake/                    Build configuration and dependency pinning
sources/EmberCore/        The engine: everything below Source/
  Source/Core/            Logging, math, JSON, filesystem, arena, Result
  Source/Ecs/             Entity handles, component registry, storage, World
  Source/Scene/           Scene and prefab serialisation
  Source/Scripting/       Lua engine and the engine's bindings
  Source/Physics/         Jolt-backed rigid bodies
  Source/Audio/           Decoding and mixing
  Source/Renderer/        Meshes, materials, PBR maths, the render queue
  Source/Engine/          Project layout, export, frame loop, commands
sources/EmberEditor/      The authoring executable
sources/EmberRuntime/     The shippable executable
tests/Unit/<Suite>/       One test binary per subsystem
```

`EmberCore` is a static library and `EmberEditor` and `EmberRuntime` both link
it. Neither executable links the other: the runtime contains no editing code, so a
shipped build cannot be talked into changing the game it is running.

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
cd build && ctest --output-on-failure
```

Every dependency is fetched by `FetchContent` and pinned to an exact tag or
commit — there is no vendored code and no system package to install. The first
configure downloads several hundred megabytes and takes a few minutes; later
configures are fast.

To build with warnings as errors, which is what CI does and what new code should
pass:

```sh
cmake -S . -B build -DEMBER_WARNINGS_AS_ERRORS=ON
```

### Adding a test

Put it in `tests/Unit/<Suite>/`, in a file matching the suite. The suite list is
in `tests/CMakeLists.txt`; add a name there for a new subsystem and CMake will
build and register a binary for it.

Tests are grouped by subsystem so a failure names the area that broke.

**A test needs no graphics device, no audio device and no window.** If you find
yourself wanting one, that is usually a sign the thing under test belongs in the
device-independent layer. That layer is where the bugs that hide are.

## Rules

### Naming

Follow [Hazel's style](https://docs.hazelengine.com/HazelForEngine/DeveloperGuide#naming),
which this repository follows throughout.

| Thing | Convention | Example |
| --- | --- | --- |
| Type | `PascalCase` | `RenderQueue`, `TransformComponent` |
| Function | `PascalCase` | `BuildDirectionalShadow` |
| Variable | `camelCase` | `drawItems` |
| Private member | `m_CamelCase` | `m_Camera` |
| Constant | `SCREAMING_SNAKE_CASE` | `MaxCollisionLayers` |
| Namespace | `PascalCase` | `Ember::Pbr` |
| Enum value | `PascalCase` | `LightComponent::Type::Point` |
| File | matches its type | `RenderQueue.cpp` |

A file's top comment names the file it belongs to, then explains what it is for
and anything surprising about it. Comments explain *why*; the code already says
what.

### Error handling

- Fallible work returns `Result<T>` with a stable `ErrorCode` and a message a
  person can act on. Codes are in `Core/Result.h`.
- Log with `EMBER_LOG_*`. Do not write to `std::cout`/`std::cerr` from library
  code; only the two executables print.
- Never throw across a module boundary. `Result` exists so that it does not have
  to be.
- Never `abort` on bad input from a file. A malformed scene is a load failure with
  a message, not a crash.

### Threading

A `World`, a `PhysicsWorld` and a `ScriptEngine` are each owned by one thread and
are not thread safe. Parallelism inside a system is that system's own business
and must be stated in a comment where it is introduced.

### Components

A component is a trivially constructible, trivially destructible struct that
declares its fields once:

```cpp
struct HealthComponent
{
    float Current = 100.0f;
    float Maximum = 100.0f;
};

EMBER_COMPONENT(HealthComponent, "Health",
    EMBER_FIELD(HealthComponent, float, Current),
    EMBER_FIELD(HealthComponent, float, Maximum))
```

Two things catch people out:

- The component type is passed **unqualified**. The macro pastes it into an
  identifier, so `Ember::HealthComponent` does not paste.
- Every field macro repeats the component type. A field macro's arguments are
  expanded before `EMBER_COMPONENT` substitutes its own parameters, so a field
  cannot refer to the enclosing component's name.

Components live in `EmberCore`, not in a game project, so that a saved scene
never names a type the runtime cannot resolve.

### Registration is explicit

`Ecs::RegisterBuiltinComponents()` must run before a `World` is built. It is an
explicit call rather than a static initialiser because the component definitions
live in a static library, where the linker discards an object file that nothing
references — a static initialiser would vanish from any build that did not
happen to call into that translation unit. `Application::Initialise` does it.

The same applies to Jolt and miniaudio: both need an explicit, ordered start-up,
and Jolt asserts on each step of its order.

### Third-party code

Third-party headers are included with `SYSTEM` in `sources/EmberCore/CMakeLists.txt`.
Their warnings are their own, and `-Werror` on them would fail a build over code
this repository cannot change.

Do not add `using namespace` for a third-party namespace, and do not let one leak
into an engine header. Jolt's headers are confined to `Physics/PhysicsWorld.cpp`
for the same reason.

## Things that have already gone wrong

Each of these was a real bug during development. They are listed because the
mistake is easy to repeat and hard to spot.

- **Frustum planes assume Vulkan's `[0, 1]` depth.** A near plane taken from the
  combined matrix's `row 2` is only correct for that convention. Assuming
  `[-1, 1]` puts it halfway into the frustum.
- **Screen-space reconstruction interpolates in clip space.** Clip depth is not
  linear in distance, so interpolating world positions with a pixel's own depth
  places it wrongly by a margin that grows with distance.
- **Shadow map centres snap to whole texels.** Without that, a sub-texel shift in
  the projection moves every shadow edge — the most visible artefact in a shadow
  map and the hardest to trace.
- **A winding convention that faces inwards is invisible.** A back-facing triangle
  is not something the eye reliably catches. The mesh tests check every
  primitive's winding against its own normals; keep that when adding a primitive.
- **Caps need their own vertices.** Sharing a cylinder's side ring for its cap
  gives a cap face normal of ±Y meeting a vertex normal that is radial.
- **Tangents are orthogonalised against the normal.** A tangent basis that is not
  orthonormal makes a normal map shade wrongly, subtly.
- **A Smith visibility term is a weighting, not an occlusion.** The correlated
  form already divides out the cosines, so it does not collapse at grazing angles
  the way the raw geometry term does. Its cosines are floored anyway: at exactly
  zero the term is unbounded, and a surface seen edge-on becomes a white speck.
- **Lua's `entity` table was renamed `Entity` because scripts shadow it.** An
  entry point taking a parameter called `entity` shadows the global table it is
  trying to call. Library tables are capitalised, as Lua's own are.
- **A script gets its own globals per entity.** Two entities running one script
  must not share a counter.

## What is not here yet

Being explicit about this is part of the job. The following are designed but not
implemented, and nothing in the tree pretends otherwise:

- **The Vulkan backend.** The renderer builds its draw list, sorts it, culls it and
  computes shadow bounds and screen-space positions — all tested — but nothing
  uploads to a GPU. That part needs a machine with a Vulkan runtime to validate,
  and shipping an untested backend would be worse than not shipping one. The
  NVRHI dependency is wired up and ready for it.
- **glTF import.** `Renderer/Mesh` and `MeshFactory` produce the same geometry the
  importer would produce, and are tested; the importer itself is not written.
- **IBL prefiltering.** The split-sum shading maths is in `Renderer/Material.h`
  and tested; generating the irradiance and prefiltered radiance textures from an
  HDRI is not.
- **A windowed editor UI.** The editor is a command surface, which is what makes
  it drivable by an agent and testable. There is no viewport, inspector or
  hierarchy panel yet.

## Reviewing a change

Before committing:

1. `cmake --build build -j` is clean, with no new warnings.
2. `ctest --output-on-failure` passes.
3. `-DEMBER_WARNINGS_AS_ERRORS=ON` builds.
4. New behaviour has a test that fails without the change.
5. Public headers say *why*, not just *what*.
6. Nothing in the list above was silently skipped — if a change leaves part of the
   task undone, say so rather than letting it look finished.