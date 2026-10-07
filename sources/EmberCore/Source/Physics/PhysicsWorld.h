// Physics/PhysicsWorld.h
//
// Rigid body simulation backed by Jolt Physics.
//
// The engine does not expose Jolt's types. A `ColliderComponent` on an entity is
// the single source of truth; the physics world mirrors it into Jolt bodies and
// copies the results back into the entity's `TransformComponent`. A game never
// has to know a physics engine exists, and a scene saved from the editor contains
// no physics-engine-specific data.
//
// Collision layers: bodies belong to named layers, and each layer names the
// layers it collides with. The layer table is part of the project rather than of
// an entity, so changing it changes the whole simulation consistently. Layers map
// onto Jolt's object layers, which is why the table is limited to 32 entries: a
// project's layers must fit in one bit each.
//
// Threading: not thread safe. `Step` runs on the thread that owns the world.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Math/Math.h"
#include "Core/Result.h"
#include "Ecs/Components.h"
#include "Ecs/Entity.h"
#include "Ecs/World.h"

namespace Ember
{
    /// Largest number of collision layers a project may declare.
    ///
    /// Each layer occupies one bit of a 32-bit mask, so this is a hard limit rather
    /// than an arbitrary one: a project that declares a 33rd layer would silently
    /// lose collisions.
    inline constexpr std::size_t MaxCollisionLayers = 32;

    /// One entry in the collision layer table.
    struct CollisionLayer
    {
        std::string Name;

        /// Bit mask naming the layers this one collides with. A layer always
        /// collides with itself.
        std::uint32_t CollidesWith = 0xFFFFFFFF;
    };

    /// Everything the simulation needs that is not per-body.
    struct PhysicsSettings
    {
        /// Fixed simulation step, in seconds.
        ///
        /// Jolt integrates in fixed steps, so this is a property of stability
        /// rather than of frame rate: a game that varied it per frame would get
        /// different physics on different machines.
        float TimeStep = 1.0f / 60.0f;

        /// Largest amount of simulation time a single frame may advance, in
        /// seconds. Anything beyond this is discarded rather than simulated.
        float MaximumTimeStep = 0.25f;

        /// Most fixed steps a single frame may take. Bounding this keeps a long
        /// stall from turning into a spiral of further stalls.
        int MaximumStepsPerFrame = 8;

        /// Upward acceleration, in metres per second squared.
        Vec3 Gravity = Vec3(0.0f, -9.81f, 0.0f);

        /// Solver velocity iterations. Higher is more accurate and slower.
        int VelocityIterations = 10;

        /// Solver position iterations. Higher is more accurate and slower.
        int PositionIterations = 2;
    };

    /// Runs rigid body simulation for a world's collider components.
    class PhysicsWorld
    {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&&) = delete;
        PhysicsWorld& operator=(PhysicsWorld&&) = delete;

        /// Creates the simulation with `settings`. Fails if Jolt cannot start.
        Result<void> Initialise(const PhysicsSettings& settings);

        /// Destroys the simulation. Safe to call more than once.
        void Shutdown();

        [[nodiscard]] bool IsInitialised() const noexcept { return m_Initialised; }

        [[nodiscard]] const PhysicsSettings& GetSettings() const noexcept { return m_Settings; }

        /// Replaces the settings. Takes effect from the next `Step`.
        void SetSettings(const PhysicsSettings& settings);

        // ------------------------------------------------------------ collision layers

        /// Declares a collision layer, or updates one of the same name.
        ///
        /// Fails with InvalidArgument past `MaxCollisionLayers`, and with
        /// AlreadyExists when the name is taken by a different layer.
        Result<void> SetCollisionLayer(std::string name, std::uint32_t collidesWith);

        /// Declares a layer that collides only with the layers named in
        /// `collidesWith`, which is a list of layer names.
        ///
        /// Every named layer must already exist. This is the form a project's
        /// settings file uses, because it survives a layer being renamed.
        Result<void> SetCollisionLayer(std::string name, const std::vector<std::string>& collidesWith);

        /// Number of declared layers. The first declared layer is at index 0.
        [[nodiscard]] std::size_t GetCollisionLayerCount() const noexcept { return m_Layers.size(); }

        /// Returns the index of a layer by name, or -1.
        [[nodiscard]] int FindCollisionLayer(std::string_view name) const noexcept;

        /// Returns a layer's collision mask, or 0 if the index is out of range.
        [[nodiscard]] std::uint32_t GetCollisionMask(std::size_t layerIndex) const noexcept;

        /// Returns a layer's name, or "" if the index is out of range.
        [[nodiscard]] const std::string& GetCollisionLayerName(std::size_t layerIndex) const noexcept;

        /// Applies the layer table to the running simulation.
        ///
        /// Called automatically when a layer changes; exposed so that a project can
        /// rebuild the table in one pass and apply it once.
        void ApplyCollisionLayers();

        // --------------------------------------------------------------------- bodies

        /// Adds a body for an entity, replacing any existing one.
        ///
        /// Fails with InvalidArgument for a dead entity or an entity with no
        /// collider, and with NotSupported for a collider shape the engine does not
        /// implement.
        Result<void> AddBody(World& world, Entity entity);

        /// Removes an entity's body. Returns false if it had none.
        bool RemoveBody(Entity entity);

        /// True when the entity has a body in the simulation.
        [[nodiscard]] bool HasBody(Entity entity) const noexcept;

        /// Number of bodies in the simulation.
        [[nodiscard]] std::size_t GetBodyCount() const noexcept { return m_Bodies.size(); }

        /// Adds a body for every entity carrying a `ColliderComponent`.
        ///
        /// Returns the number of bodies created; entities whose collider could not
        /// be turned into a body are reported and skipped.
        std::size_t AddAllBodies(World& world);

        /// Assigns an entity's body to a collision layer by name.
        ///
        /// Fails with NotFound when no such layer is declared, which is what makes
        /// a typo in a scene file visible rather than silent.
        Result<void> SetBodyLayer(Entity entity, std::string_view layerName);

        /// Returns an entity's body layer index, or -1.
        [[nodiscard]] int GetBodyLayer(Entity entity) const noexcept;

        // ---------------------------------------------------------------------- stepping

        /// Advances the simulation by `deltaTime` seconds and writes the results
        /// back into each body's `TransformComponent`.
        ///
        /// Whole fixed steps are simulated and the remainder is carried to the
        /// next frame, so the result does not depend on the frame rate.
        void Step(World& world, float deltaTime);

        /// Simulation time stepped since the world was created, in seconds.
        [[nodiscard]] double GetSimulatedTime() const noexcept { return m_SimulatedTime; }

        // --------------------------------------------------------------------- velocity

        /// Overrides a body's linear velocity, in metres per second.
        Result<void> SetLinearVelocity(Entity entity, const Vec3& velocity);

        /// Reads a body's linear velocity, in metres per second.
        ///
        /// Returns zero for an entity with no body, so a script reading the velocity
        /// of something just destroyed does not have to check first.
        [[nodiscard]] Vec3 GetLinearVelocity(Entity entity) const;

        /// Overrides a body's angular velocity, in radians per second.
        Result<void> SetAngularVelocity(Entity entity, const Vec3& angularVelocity);

        [[nodiscard]] Vec3 GetAngularVelocity(Entity entity) const;

        /// Applies an instantaneous change to a body's linear velocity.
        ///
        /// The change is divided by the body's mass, so the same impulse moves a
        /// light body further than a heavy one.
        Result<void> ApplyImpulse(Entity entity, const Vec3& impulse);

        /// Applies an instantaneous change to a body's angular velocity.
        Result<void> ApplyAngularImpulse(Entity entity, const Vec3& angularImpulse);

        /// Reads a body's total mass, in kilograms. Zero for an entity with no body.
        [[nodiscard]] float GetMass(Entity entity) const;

        /// Reads a body's shape volume, in cubic metres. Zero for no body.
        [[nodiscard]] float GetVolume(Entity entity) const;

        /// Moves a body to a pose immediately, without simulating the motion.
        ///
        /// Used when an entity is teleported. The body's velocity is cleared, so
        /// nothing is carried across from where it used to be. The entity's
        /// `TransformComponent` reflects the new pose from the next `Step`, as it
        /// does for any other motion.
        Result<void> Teleport(Entity entity, const Vec3& position, const Quat& rotation);

    private:
        /// Per-entity bookkeeping, keyed by entity index.
        struct BodyRecord
        {
            Entity Handle;
            std::uint32_t BodyId = 0;
            std::size_t LayerIndex = 0;
        };

        /// Jolt's own objects, hidden behind a pimpl because Jolt's headers are
        /// verbose and would otherwise appear in every translation unit that
        /// includes this one.
        struct Implementation;
        std::unique_ptr<Implementation> m_Implementation;

        PhysicsSettings m_Settings;
        std::vector<CollisionLayer> m_Layers;
        std::unordered_map<std::string, std::size_t> m_LayerIndexByName;
        std::unordered_map<EntityIndex, BodyRecord> m_Bodies;

        double m_SimulatedTime = 0.0;
        float m_AccumulatedTime = 0.0f;
        bool m_Initialised = false;
    };
}