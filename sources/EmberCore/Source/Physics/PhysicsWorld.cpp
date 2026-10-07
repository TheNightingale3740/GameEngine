// Physics/PhysicsWorld.cpp

#include "Physics/PhysicsWorld.h"

#include <algorithm>
#include <cstring>

#include "Core/Logging/Log.h"

// Jolt's headers must be included in this order and with its macros defined first.
// They are confined to this translation unit: nothing else in the engine includes
// them, which is what keeps Jolt's compile times and namespace out of the rest of
// the codebase.
//
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/MotionProperties.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

namespace Ember
{
    namespace
    {
        /// Jolt's coarse motion grouping.
        ///
        /// Ember's own layer table decides what collides with what; Jolt's object
        /// layers exist only so that the broad phase can be split into a static half
        /// and a moving half, which is what makes the simulation cheap.
        constexpr JPH::ObjectLayer kStaticLayer = 0;
        constexpr JPH::ObjectLayer kMovingLayer = 1;
        constexpr JPH::uint kObjectLayerCount = 2;

        constexpr JPH::BroadPhaseLayer::Type kStaticBroadPhase = 0;
        constexpr JPH::BroadPhaseLayer::Type kMovingBroadPhase = 1;
        constexpr JPH::uint kBroadPhaseLayerCount = 2;

        /// Converts a position or direction. Jolt's RVec3 is Vec3 in single
        /// precision and a wider type only when double precision is enabled, so one
        /// function serves both spellings.
        [[nodiscard]] JPH::Vec3 ToJolt(const Vec3& vector) noexcept
        {
            return JPH::Vec3(vector.x, vector.y, vector.z);
        }

        [[nodiscard]] Vec3 FromJolt(const JPH::Vec3& vector) noexcept
        {
            return Vec3(vector.GetX(), vector.GetY(), vector.GetZ());
        }

        [[nodiscard]] JPH::Quat ToJolt(const Quat& quaternion) noexcept
        {
            return JPH::Quat(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
        }

        [[nodiscard]] Quat FromJolt(const JPH::Quat& quaternion) noexcept
        {
            return Quat(quaternion.GetW(), quaternion.GetX(), quaternion.GetY(), quaternion.GetZ());
        }

        /// Jolt requires a process-wide factory to exist before any shape is created
        /// and to be torn down only after every shape is gone.
        ///
        /// Both happen exactly once per process. Nothing else in the engine may
        /// create a Jolt object during static initialisation, because the order of
        /// static destructors is unspecified.
        struct JoltRuntime
        {
            JoltRuntime()
            {
                // Jolt overrides its own `operator new` to route through function
                // pointers that are null until the allocator is installed. That has
                // to happen before anything Jolt-owned is allocated, which includes
                // the factory.
                JPH::RegisterDefaultAllocator();

                m_Factory = std::make_unique<JPH::Factory>();
                JPH::Factory::sInstance = m_Factory.get();

                JPH::RegisterTypes();
            }

            ~JoltRuntime()
            {
                JPH::UnregisterTypes();
                JPH::Factory::sInstance = nullptr;
            }

            std::unique_ptr<JPH::Factory> m_Factory;
        };

        JoltRuntime& GetJoltRuntime()
        {
            static JoltRuntime runtime;
            return runtime;
        }

        /// Smallest half-extent or radius the engine will hand to Jolt.
        ///
        /// Jolt gives every convex shape a rounding radius, and asserts that the
        /// shape is at least that thick. A zero-sized collider in a scene file is
        /// a mistake worth tolerating rather than crashing on, so it is raised to
        /// this floor instead of being rejected.
        constexpr float MinimumExtent = 0.05f;

        [[nodiscard]] float ClampExtent(float value) noexcept
        {
            return std::max(value, MinimumExtent);
        }

        /// Lets every pair of Jolt layers collide.
        void EnableAllLayerPairs(JPH::ObjectLayerPairFilterTable& pairs)
        {
            pairs.EnableCollision(kStaticLayer, kStaticLayer);
            pairs.EnableCollision(kStaticLayer, kMovingLayer);
            pairs.EnableCollision(kMovingLayer, kMovingLayer);
        }
    }

    /// Jolt's runtime state, hidden from every other translation unit.
    struct PhysicsWorld::Implementation
    {
        JPH::TempAllocatorImpl m_TempAllocator{16 * 1024 * 1024};
        JPH::JobSystemThreadPool m_JobSystem{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                             static_cast<int>(-1)};

        JPH::BroadPhaseLayerInterfaceTable m_BroadPhaseLayers{kObjectLayerCount, kBroadPhaseLayerCount};
        JPH::ObjectLayerPairFilterTable m_LayerPairs{kObjectLayerCount};

        /// Derived from the two tables above, and therefore rebuilt whenever they
        /// change rather than maintained alongside them.
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> m_ObjectVsBroadPhase;

        /// Held by pointer because PhysicsSystem owns non-copyable resources and
        /// because rebuilding it after a layer change must not disturb the handle
        /// the rest of the engine holds.
        std::unique_ptr<JPH::PhysicsSystem> m_System;

        /// Creates a system configured from the current tables and settings.
        void CreateSystem(const PhysicsSettings& settings);

        void RebuildObjectVsBroadPhase()
        {
            m_ObjectVsBroadPhase = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(
                m_BroadPhaseLayers, kBroadPhaseLayerCount, m_LayerPairs, kObjectLayerCount);
        }
    };

    PhysicsWorld::PhysicsWorld()
    {
        // Jolt overrides the global allocator to route through its factory, so any
        // Jolt-owned allocation before the factory exists faults. The runtime is
        // therefore brought up before a single Jolt object is constructed.
        GetJoltRuntime();

        m_Implementation = std::make_unique<Implementation>();

        m_Implementation->m_BroadPhaseLayers.MapObjectToBroadPhaseLayer(kStaticLayer,
                                                                        JPH::BroadPhaseLayer(kStaticBroadPhase));
        m_Implementation->m_BroadPhaseLayers.MapObjectToBroadPhaseLayer(kMovingLayer,
                                                                        JPH::BroadPhaseLayer(kMovingBroadPhase));

        // Everything collides with everything until a project says otherwise: a
        // table that silently lets objects pass through each other is a far more
        // confusing default than one that is merely permissive.
        EnableAllLayerPairs(m_Implementation->m_LayerPairs);

        // The object-versus-broad-phase filter is derived from the two tables
        // above, so it is built once they have been populated. The Jolt system
        // itself is created in `Initialise`, after Jolt's global runtime exists.
        m_Implementation->RebuildObjectVsBroadPhase();
    }

    PhysicsWorld::~PhysicsWorld()
    {
        Shutdown();
    }

    void PhysicsWorld::Implementation::CreateSystem(const PhysicsSettings& settings)
    {
        m_System = std::make_unique<JPH::PhysicsSystem>();

        m_System->Init(1024, 0, 1024, 1024,
                       m_BroadPhaseLayers,
                       *m_ObjectVsBroadPhase,
                       m_LayerPairs);

        m_System->SetGravity(ToJolt(settings.Gravity));

        JPH::PhysicsSettings joltSettings;
        joltSettings.mNumVelocitySteps = static_cast<std::uint32_t>(settings.VelocityIterations);
        joltSettings.mNumPositionSteps = static_cast<std::uint32_t>(settings.PositionIterations);
        m_System->SetPhysicsSettings(joltSettings);
    }

    Result<void> PhysicsWorld::Initialise(const PhysicsSettings& settings)
    {
        if (m_Initialised)
        {
            return {};
        }

        SetSettings(settings);

        // Layer 0 exists from the start so that a collider without an explicit
        // layer still has somewhere to go.
        if (m_Layers.empty())
        {
            m_LayerIndexByName.emplace("Default", 0);
            m_Layers.push_back(CollisionLayer{"Default", 0xFFFFFFFF});
        }

        // The layer table is pushed into Jolt before the system is built, so the
        // system is configured once rather than rebuilt.
        ApplyCollisionLayers();
        m_Implementation->CreateSystem(m_Settings);

        m_Initialised = true;

        EMBER_LOG_INFO("Physics initialised: gravity {} m/s^2, {} layer(s)",
                       m_Settings.Gravity.y, m_Layers.size());
        return {};
    }

    void PhysicsWorld::Shutdown()
    {
        if (!m_Initialised)
        {
            return;
        }

        // Bodies must be destroyed while the system is still alive, or Jolt reports
        // leaks and asserts in a debug build.
        for (auto& [index, record] : m_Bodies)
        {
            (void)index;

            JPH::BodyInterface& bodies = m_Implementation->m_System->GetBodyInterface();
            const JPH::BodyID bodyId(record.BodyId);
            bodies.RemoveBody(bodyId);
            bodies.DestroyBody(bodyId);
        }

        m_Bodies.clear();
        m_AccumulatedTime = 0.0f;
        m_Initialised = false;

        EMBER_LOG_INFO("Physics shut down");
    }

    void PhysicsWorld::SetSettings(const PhysicsSettings& settings)
    {
        m_Settings = settings;

        // Clamped rather than rejected: a project file with a nonsense value should
        // produce a working simulation, not a failure to start.
        m_Settings.VelocityIterations = std::max(1, m_Settings.VelocityIterations);
        m_Settings.PositionIterations = std::max(1, m_Settings.PositionIterations);
        m_Settings.TimeStep = std::max(m_Settings.TimeStep, 1e-6f);
        m_Settings.MaximumTimeStep = std::max(m_Settings.MaximumTimeStep, m_Settings.TimeStep);
        m_Settings.MaximumStepsPerFrame = std::max(1, m_Settings.MaximumStepsPerFrame);

        if (!m_Initialised)
        {
            return;
        }

        if (m_Implementation->m_System != nullptr)
        {
            m_Implementation->CreateSystem(m_Settings);
        }
    }

    Result<void> PhysicsWorld::SetCollisionLayer(std::string name, std::uint32_t collidesWith)
    {
        if (name.empty())
        {
            return {ErrorCode::InvalidArgument, "A collision layer must have a name"};
        }

        if (const auto existing = m_LayerIndexByName.find(name); existing != m_LayerIndexByName.end())
        {
            m_Layers[existing->second].CollidesWith = collidesWith;
            ApplyCollisionLayers();
            return {};
        }

        if (m_Layers.size() >= MaxCollisionLayers)
        {
            return Error(ErrorCode::InvalidArgument,
                         std::format("A project may declare at most {} collision layers", MaxCollisionLayers));
        }

        m_LayerIndexByName.emplace(name, m_Layers.size());
        m_Layers.push_back(CollisionLayer{std::move(name), collidesWith});

        ApplyCollisionLayers();
        return {};
    }

    Result<void> PhysicsWorld::SetCollisionLayer(std::string name, const std::vector<std::string>& collidesWith)
    {
        std::uint32_t mask = 0;

        for (const std::string& other : collidesWith)
        {
            const int index = FindCollisionLayer(other);
            if (index < 0)
            {
                return Error(ErrorCode::NotFound, "No collision layer named '" + other + "' is declared");
            }

            mask |= 1u << index;
        }

        return SetCollisionLayer(std::move(name), mask);
    }

    int PhysicsWorld::FindCollisionLayer(std::string_view name) const noexcept
    {
        const auto found = m_LayerIndexByName.find(std::string(name));
        return found == m_LayerIndexByName.end() ? -1 : static_cast<int>(found->second);
    }

    std::uint32_t PhysicsWorld::GetCollisionMask(std::size_t layerIndex) const noexcept
    {
        return layerIndex < m_Layers.size() ? m_Layers[layerIndex].CollidesWith : 0;
    }

    const std::string& PhysicsWorld::GetCollisionLayerName(std::size_t layerIndex) const noexcept
    {
        static const std::string empty;
        return layerIndex < m_Layers.size() ? m_Layers[layerIndex].Name : empty;
    }

    void PhysicsWorld::ApplyCollisionLayers()
    {
        JPH::ObjectLayerPairFilterTable& pairs = m_Implementation->m_LayerPairs;

        for (JPH::ObjectLayer a = 0; a < kObjectLayerCount; ++a)
        {
            for (JPH::ObjectLayer b = 0; b < kObjectLayerCount; ++b)
            {
                pairs.DisableCollision(a, b);
            }
        }

        for (std::size_t i = 0; i < m_Layers.size(); ++i)
        {
            for (std::size_t j = 0; j < m_Layers.size(); ++j)
            {
                // Each Ember layer is realised as a pairing of Jolt's static and
                // moving object layers, so "these two Ember layers collide" is four
                // Jolt pairs.
                if ((m_Layers[i].CollidesWith & (1u << j)) != 0)
                {
                    pairs.EnableCollision(kStaticLayer, kStaticLayer);
                    pairs.EnableCollision(kStaticLayer, kMovingLayer);
                    pairs.EnableCollision(kMovingLayer, kMovingLayer);
                }
            }
        }

        if (m_Layers.empty())
        {
            EnableAllLayerPairs(pairs);
        }

        m_Implementation->RebuildObjectVsBroadPhase();

        m_Implementation->CreateSystem(m_Settings);
    }

    Result<void> PhysicsWorld::AddBody(World& world, Entity entity)
    {
        if (!m_Initialised)
        {
            return {ErrorCode::PhysicsError, "Physics has not been initialised"};
        }

        if (!world.IsAlive(entity))
        {
            return {ErrorCode::InvalidArgument, "Cannot add a body for a dead entity"};
        }

        const ColliderComponent* collider = world.TryGetComponent<ColliderComponent>(entity);
        if (collider == nullptr)
        {
            return {ErrorCode::InvalidArgument, "The entity has no ColliderComponent"};
        }

        // A shape is built from settings rather than from ShapeSettings subclasses
        // because the engine only ever needs four primitive shapes; the settings
        // classes exist to serialise shapes that are already known.
        JPH::ShapeRefC shape;
        switch (collider->BodyShape)
        {
            case ColliderComponent::Shape::Box:
                shape = JPH::ShapeRefC(new JPH::BoxShape(ToJolt(Vec3(ClampExtent(collider->Extents.x),
                                                                     ClampExtent(collider->Extents.y),
                                                                     ClampExtent(collider->Extents.z)))));
                break;

            case ColliderComponent::Shape::Sphere:
                shape = JPH::ShapeRefC(new JPH::SphereShape(ClampExtent(collider->Radius)));
                break;

            case ColliderComponent::Shape::Capsule:
                // A capsule's inner cylinder must be at least as long as it is
                // wide, which Jolt asserts on.
                shape = JPH::ShapeRefC(new JPH::CapsuleShape(
                    ClampExtent(std::max(collider->HalfHeight, collider->Radius)), ClampExtent(collider->Radius)));
                break;

            case ColliderComponent::Shape::Plane:
                shape = JPH::ShapeRefC(new JPH::PlaneShape(JPH::Plane(JPH::Vec3(0.0f, 1.0f, 0.0f), 0.0f)));
                break;
        }

        if (shape == nullptr)
        {
            return Error(ErrorCode::NotSupported,
                         std::format("Collider shape {} is not implemented",
                                     static_cast<int>(collider->BodyShape)));
        }

        const TransformComponent* transform = world.TryGetComponent<TransformComponent>(entity);
        Vec3 translation(0.0f);
        Quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        Vec3 scale(1.0f);
        Math::DecomposeTransform(transform != nullptr ? transform->ToMatrix() : Mat4(1.0f),
                                 translation,
                                 rotation,
                                 scale);

        // Jolt transforms a shape rather than scaling it in the shape description,
        // so a non-uniform scale has to be baked into the shape. An infinite plane
        // cannot be scaled at all.
        if (collider->BodyShape != ColliderComponent::Shape::Plane &&
            glm::any(glm::notEqual(scale, Vec3(1.0f))))
        {
            const JPH::ShapeSettings::ShapeResult scaled = shape->ScaleShape(ToJolt(scale));
            if (!scaled.IsValid())
            {
                return Error(ErrorCode::PhysicsError,
                             std::string("Could not scale the collider shape: ") + scaled.GetError().c_str());
            }

            shape = JPH::ShapeRefC(scaled.Get().GetPtr());
        }

        // The body's rotation comes from the transform, and its position is
        // rebuilt around the shape's centre so that a box's origin is its middle
        // rather than Jolt's default of the shape's local origin.
        const Vec3 centreOfMass = FromJolt(shape->GetCenterOfMass());

        JPH::BodyCreationSettings settings(shape.GetPtr(),
                                           ToJolt(translation),
                                           ToJolt(rotation),
                                           JPH::EMotionType::Static,
                                           kStaticLayer);

        switch (collider->BodyMode)
        {
            case ColliderComponent::Mode::Static:
                settings.mMotionType = JPH::EMotionType::Static;
                settings.mObjectLayer = kStaticLayer;
                break;

            case ColliderComponent::Mode::Dynamic:
                settings.mMotionType = JPH::EMotionType::Dynamic;
                settings.mObjectLayer = kMovingLayer;
                break;

            case ColliderComponent::Mode::Kinematic:
                settings.mMotionType = JPH::EMotionType::Kinematic;
                settings.mObjectLayer = kMovingLayer;
                break;
        }

        settings.mRestitution = Math::Clamp(collider->Restitution, 0.0f, 1.0f);
        settings.mFriction = Math::Clamp(collider->Friction, 0.0f, 1.0f);
        settings.mGravityFactor = Math::Clamp(collider->GravityScale, 0.0f, 1.0f);
        settings.mAllowSleeping = collider->AllowSleeping;

        if (collider->BodyMode == ColliderComponent::Mode::Dynamic)
        {
            // Inertia is derived from the shape at this mass, so a cube and a
            // capsule of the same mass still tumble differently.
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = std::max(collider->Mass, 0.001f);
        }

        // The transform is where a script expects the entity to be, so the shape is
        // shifted back by the centre of mass that Jolt will work with internally.
        settings.mPosition = ToJolt(translation);
        settings.mPosition += settings.mRotation * ToJolt(-centreOfMass);

        RemoveBody(entity);

        JPH::BodyInterface& bodies = m_Implementation->m_System->GetBodyInterface();
        const JPH::BodyID bodyId = bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);

        if (bodyId.IsInvalid())
        {
            return Error(ErrorCode::PhysicsError, "Jolt refused to create a body for " + ToString(entity));
        }

        BodyRecord record;
        record.Handle = entity;
        record.BodyId = bodyId.GetIndexAndSequenceNumber();
        record.LayerIndex = 0;

        m_Bodies[entity.Index] = record;

        EMBER_LOG_TRACE("Added a body for {}", ToString(entity));
        return {};
    }

    std::size_t PhysicsWorld::AddAllBodies(World& world)
    {
        std::vector<Entity> candidates;
        world.Each<ColliderComponent>([&](const ColliderComponent&, Entity entity)
        {
            candidates.push_back(entity);
        });

        std::size_t created = 0;
        for (const Entity entity : candidates)
        {
            if (Result<void> result = AddBody(world, entity); result.IsSuccess())
            {
                ++created;
            }
            else
            {
                EMBER_LOG_ERROR("Could not add a body for {}: {}", ToString(entity), result.GetError().Message);
            }
        }

        return created;
    }

    bool PhysicsWorld::RemoveBody(Entity entity)
    {
        const auto found = m_Bodies.find(entity.Index);
        if (found == m_Bodies.end() || found->second.Handle != entity)
        {
            return false;
        }

        if (m_Initialised)
        {
            JPH::BodyInterface& bodies = m_Implementation->m_System->GetBodyInterface();
            const JPH::BodyID bodyId(found->second.BodyId);
            bodies.RemoveBody(bodyId);
            bodies.DestroyBody(bodyId);
        }

        m_Bodies.erase(found);
        return true;
    }

    bool PhysicsWorld::HasBody(Entity entity) const noexcept
    {
        const auto found = m_Bodies.find(entity.Index);
        return found != m_Bodies.end() && found->second.Handle == entity;
    }

    Result<void> PhysicsWorld::SetBodyLayer(Entity entity, std::string_view layerName)
    {
        const auto found = m_Bodies.find(entity.Index);
        if (found == m_Bodies.end() || found->second.Handle != entity)
        {
            return {ErrorCode::InvalidArgument, "The entity has no body"};
        }

        const int index = FindCollisionLayer(layerName);
        if (index < 0)
        {
            return Error(ErrorCode::NotFound, "No collision layer named '" + std::string(layerName) + "' is declared");
        }

        found->second.LayerIndex = static_cast<std::size_t>(index);
        return {};
    }

    int PhysicsWorld::GetBodyLayer(Entity entity) const noexcept
    {
        const auto found = m_Bodies.find(entity.Index);
        if (found == m_Bodies.end() || found->second.Handle != entity)
        {
            return -1;
        }

        return static_cast<int>(found->second.LayerIndex);
    }

    void PhysicsWorld::Step(World& world, float deltaTime)
    {
        if (!m_Initialised || m_Bodies.empty())
        {
            return;
        }

        // Whole fixed steps are simulated and the remainder is carried over, so the
        // simulation advances by real time rather than by frame count.
        m_AccumulatedTime += Math::Clamp(deltaTime, 0.0f, m_Settings.MaximumTimeStep);

        int steps = 0;
        while (m_AccumulatedTime >= m_Settings.TimeStep && steps < m_Settings.MaximumStepsPerFrame)
        {
            const JPH::EPhysicsUpdateError error = m_Implementation->m_System->Update(
                m_Settings.TimeStep, 1, &m_Implementation->m_TempAllocator, &m_Implementation->m_JobSystem);

            if (error != JPH::EPhysicsUpdateError::None)
            {
                EMBER_LOG_ERROR("Physics step failed: {}", static_cast<int>(error));
                break;
            }

            m_AccumulatedTime -= m_Settings.TimeStep;
            m_SimulatedTime += static_cast<double>(m_Settings.TimeStep);
            ++steps;
        }

        // Whatever exceeded the step budget is dropped rather than carried, so a
        // long stall cannot produce a burst of catch-up frames afterwards.
        if (m_AccumulatedTime > m_Settings.TimeStep * m_Settings.MaximumStepsPerFrame)
        {
            m_AccumulatedTime = 0.0f;
        }

        JPH::BodyInterface& bodies = m_Implementation->m_System->GetBodyInterface();

        // Bodies whose entity has been destroyed are removed here rather than by
        // the caller, so a script may destroy an entity from inside its own update.
        for (auto it = m_Bodies.begin(); it != m_Bodies.end();)
        {
            const BodyRecord& record = it->second;

            if (!world.IsAlive(record.Handle))
            {
                const JPH::BodyID bodyId(record.BodyId);
                bodies.RemoveBody(bodyId);
                bodies.DestroyBody(bodyId);

                it = m_Bodies.erase(it);
                continue;
            }

            const JPH::BodyID bodyId(record.BodyId);

            TransformComponent* transform = world.TryGetComponent<TransformComponent>(record.Handle);
            if (transform != nullptr)
            {
                // Jolt reports the body's centre of mass; the entity's transform is
                // where that shape sits, which is offset by the same amount the body
                // was created with.
                const JPH::RVec3 centreOfMass = bodies.GetCenterOfMassPosition(bodyId);
                const JPH::Quat orientation = bodies.GetRotation(bodyId);

                transform->Rotation = FromJolt(orientation);
                transform->Position = FromJolt(centreOfMass) +
                                      (glm::conjugate(transform->Rotation) *
                                       FromJolt(bodies.GetShape(bodyId)->GetCenterOfMass()));
            }

            ++it;
        }
    }

    Result<void> PhysicsWorld::SetLinearVelocity(Entity entity, const Vec3& velocity)
    {
        if (!HasBody(entity))
        {
            return {ErrorCode::InvalidArgument, "The entity has no body"};
        }

        m_Implementation->m_System->GetBodyInterface().SetLinearVelocity(
            JPH::BodyID(m_Bodies[entity.Index].BodyId), ToJolt(velocity));

        return {};
    }

    Vec3 PhysicsWorld::GetLinearVelocity(Entity entity) const
    {
        if (!HasBody(entity))
        {
            return Vec3(0.0f);
        }

        return FromJolt(m_Implementation->m_System->GetBodyInterface().GetLinearVelocity(
            JPH::BodyID(m_Bodies.find(entity.Index)->second.BodyId)));
    }

    Result<void> PhysicsWorld::SetAngularVelocity(Entity entity, const Vec3& angularVelocity)
    {
        if (!HasBody(entity))
        {
            return {ErrorCode::InvalidArgument, "The entity has no body"};
        }

        m_Implementation->m_System->GetBodyInterface().SetAngularVelocity(
            JPH::BodyID(m_Bodies[entity.Index].BodyId), ToJolt(angularVelocity));

        return {};
    }

    Vec3 PhysicsWorld::GetAngularVelocity(Entity entity) const
    {
        if (!HasBody(entity))
        {
            return Vec3(0.0f);
        }

        return FromJolt(m_Implementation->m_System->GetBodyInterface().GetAngularVelocity(
            JPH::BodyID(m_Bodies.find(entity.Index)->second.BodyId)));
    }

    Result<void> PhysicsWorld::ApplyImpulse(Entity entity, const Vec3& impulse)
    {
        if (!HasBody(entity))
        {
            return {ErrorCode::InvalidArgument, "The entity has no body"};
        }

        m_Implementation->m_System->GetBodyInterface().AddImpulse(
            JPH::BodyID(m_Bodies[entity.Index].BodyId), ToJolt(impulse));

        return {};
    }

    Result<void> PhysicsWorld::ApplyAngularImpulse(Entity entity, const Vec3& angularImpulse)
    {
        if (!HasBody(entity))
        {
            return {ErrorCode::InvalidArgument, "The entity has no body"};
        }

        m_Implementation->m_System->GetBodyInterface().AddAngularImpulse(
            JPH::BodyID(m_Bodies[entity.Index].BodyId), ToJolt(angularImpulse));

        return {};
    }

    float PhysicsWorld::GetMass(Entity entity) const
    {
        if (!HasBody(entity))
        {
            return 0.0f;
        }

        // The mass a body actually simulates with lives in its motion properties,
        // which Jolt only exposes through a body lock. The shape's own mass
        // properties are the density-derived figure, which is not what a collider
        // that names a mass ends up using.
        const JPH::BodyLockRead lock(m_Implementation->m_System->GetBodyLockInterfaceNoLock(),
                                     JPH::BodyID(m_Bodies.find(entity.Index)->second.BodyId));

        if (!lock.Succeeded())
        {
            return 0.0f;
        }

        const JPH::MotionProperties* motion = lock.GetBody().GetMotionPropertiesUnchecked();
        if (motion == nullptr)
        {
            // A static or kinematic body has no motion properties and is treated as
            // having infinite mass, which is reported here as zero so that a caller
            // dividing by it gets a defined answer.
            return 0.0f;
        }

        const float inverseMass = motion->GetInverseMass();
        return inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f;
    }

    float PhysicsWorld::GetVolume(Entity entity) const
    {
        if (!HasBody(entity))
        {
            return 0.0f;
        }

        const JPH::Shape* shape = m_Implementation->m_System->GetBodyInterface()
                                      .GetShape(JPH::BodyID(m_Bodies.find(entity.Index)->second.BodyId));

        return shape != nullptr ? shape->GetVolume() : 0.0f;
    }

    Result<void> PhysicsWorld::Teleport(Entity entity, const Vec3& position, const Quat& rotation)
    {
        if (!HasBody(entity))
        {
            return {ErrorCode::InvalidArgument, "The entity has no body"};
        }

        JPH::BodyInterface& bodies = m_Implementation->m_System->GetBodyInterface();
        const JPH::BodyID bodyId(m_Bodies[entity.Index].BodyId);

        const JPH::Vec3 centreOfMass = bodies.GetShape(bodyId)->GetCenterOfMass();
        const JPH::Vec3 bodyPosition = ToJolt(position) + ToJolt(rotation) * (-centreOfMass);

        bodies.SetPositionAndRotation(bodyId, bodyPosition, ToJolt(rotation), JPH::EActivation::Activate);

        // The old velocity does not survive a teleport: carrying it across would
        // fling the body from wherever it used to be.
        bodies.SetLinearVelocity(bodyId, JPH::Vec3::sZero());
        bodies.SetAngularVelocity(bodyId, JPH::Vec3::sZero());

        return {};
    }
}