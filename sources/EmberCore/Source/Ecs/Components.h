// Ecs/Components.h
//
// The component set every Ember scene can use.
//
// Components live in the core library rather than in a game project so that a
// saved scene never references a type the runtime cannot resolve: whatever the
// editor can author, the shipped runtime can also load and run.

#pragma once

#include "Ecs/Reflection.h"

namespace Ember
{
    /// Position, orientation and scale in the entity's parent space.
    ///
    /// Rotation is stored as a quaternion because an inspector cannot meaningfully
    /// edit Euler angles: they have multiple valid spellings for one orientation,
    /// and interpolating between them takes the long way round.
    struct TransformComponent
    {
        Vec3 Position = Vec3(0.0f);
        Quat Rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Vec3 Scale = Vec3(1.0f);

        /// Returns the composed world-relative transform matrix.
        [[nodiscard]] Mat4 ToMatrix() const noexcept;

        /// Decomposes a transform matrix into position, rotation and scale.
        ///
        /// Use this when adopting an externally produced matrix, such as one from
        /// a physics body, so that the result stays free of shear.
        void FromMatrix(const Mat4& matrix) noexcept;
    };

    /// A renderer mesh: an index into the mesh asset library plus material slots.
    struct MeshComponent
    {
        /// Asset id of the mesh, or 0 for none.
        std::uint32_t MeshId = 0;

        /// Asset id of the material, or 0 for none.
        std::uint32_t MaterialId = 0;

        /// Index of the first vertex to draw. Supports slicing a shared mesh.
        std::uint32_t FirstIndex = 0;

        /// Number of indices to draw. 0 means draw the whole mesh.
        std::uint32_t IndexCount = 0;

        /// Whether this entity should be submitted to the renderer at all.
        bool Visible = true;

        /// Renders both faces. Used for foliage and thin geometry.
        bool DoubleSided = false;
    };

    /// Directional light with a shadow-casting cone.
    struct LightComponent
    {
        enum class Type : std::int32_t
        {
            Directional = 0,
            Point = 1,
            Spot = 2
        };

        Type LightType = Type::Directional;

        /// Light colour in linear space. Values above 1 are allowed.
        Vec3 Color = Vec3(1.0f);

        /// Intensity in the engine's arbitrary linear units.
        float Intensity = 1.0f;

        /// Half-angle of a spot light's cone, in degrees.
        float InnerAngleDegrees = 25.0f;

        /// Distance past which a point or spot light falls off to nothing.
        float Range = 20.0f;

        /// Whether this light casts shadows.
        bool CastsShadows = true;

        /// Shadow map resolution along one edge.
        std::uint32_t ShadowResolution = 2048;
    };

    /// A camera. Exactly one enabled camera in a scene is the render camera.
    struct CameraComponent
    {
        float FieldOfViewDegrees = 60.0f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        bool Orthographic = false;

        /// Half-height of the orthographic view volume, in world units.
        float OrthographicSize = 10.0f;
    };

    /// Static or dynamic geometry for the physics simulation.
    struct ColliderComponent
    {
        enum class Shape : std::int32_t
        {
            Box = 0,
            Sphere = 1,
            Capsule = 2,
            Plane = 3
        };

        enum class Mode : std::int32_t
        {
            Static = 0,
            Dynamic = 1,
            Kinematic = 2
        };

        Shape BodyShape = Shape::Box;
        Mode BodyMode = Mode::Static;

        /// Half-extents for a box, or the radius for a sphere or capsule.
        Vec3 Extents = Vec3(0.5f, 0.5f, 0.5f);

        /// Capsule radius and half-height of the inner cylinder.
        float Radius = 0.5f;
        float HalfHeight = 1.0f;

        /// Restitution: 0 does not bounce, 1 bounces perfectly.
        float Restitution = 0.0f;

        /// Friction in [0, 1].
        float Friction = 0.5f;

        /// Fraction of gravity applied to this body, in [0, 1].
        float GravityScale = 1.0f;

        /// Mass in kilograms. Ignored for static and kinematic bodies.
        float Mass = 1.0f;

        /// Whether the body may be put to sleep when it comes to rest.
        bool AllowSleeping = true;
    };

    /// An audio source that plays a sound asset at a position.
    struct AudioSourceComponent
    {
        /// Asset id of the sound, or 0 for none.
        std::uint32_t SoundId = 0;

        /// Playback volume multiplier.
        float Volume = 1.0f;

        /// Playback pitch multiplier.
        float Pitch = 1.0f;

        /// Whether playback loops until stopped.
        bool Looping = false;

        /// Whether this source has been started and not yet stopped.
        bool Playing = false;
    };

    /// A Lua behaviour attached to an entity.
    struct ScriptComponent
    {
        /// Path to the script, relative to the project's script directory.
        std::string ScriptPath;

        /// Whether the script's update callback runs this frame.
        bool Enabled = true;
    };

    /// Marks an entity as a spawnable prefab instance root.
    struct PrefabComponent
    {
        /// Path of the prefab this instance was created from, relative to the
        /// project's prefab directory. Empty for a prefab's own definition.
        std::string SourcePrefab;

        /// Stable identifier of this instance, preserved across saves so that
        /// external references to it survive a reload.
        std::string InstanceId;
    };
}

namespace Ember::Ecs
{
    /// Registers every built-in component with the engine's component registry.
    ///
    /// Must be called once before any `World` is created. Calling it more than
    /// once is harmless; later calls are ignored.
    ///
    /// Registration is an explicit call rather than a namespace-scope static
    /// initialiser because the definitions live in a static library, where the
    /// linker discards an object file whose symbols nothing references. A static
    /// initialiser would therefore vanish from any build that did not happen to
    /// call into this translation unit.
    void RegisterBuiltinComponents();

    /// Registers one component type. Used by `RegisterBuiltinComponents`.
    ///
    /// Declared here so that the built-in set can be spelled out in one place
    /// without repeating the registration macro's internals.
    template <typename T>
    void RegisterBuiltin(std::string_view name, std::vector<Property> properties);
}
