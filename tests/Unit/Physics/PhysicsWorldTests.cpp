#include <gtest/gtest.h>

#include "Ecs/Components.h"
#include "Physics/PhysicsWorld.h"

using namespace Ember;

namespace
{
    /// Registers the built-in component set exactly once per process.
    struct BuiltinComponentRegistration
    {
        BuiltinComponentRegistration()
        {
            Ember::Ecs::RegisterBuiltinComponents();
        }
    };

    const BuiltinComponentRegistration s_BuiltinComponentRegistration;

    /// An initialised physics world, shut down when the test ends.
    class PhysicsFixture
    {
    public:
        PhysicsFixture()
        {
            // No gravity by default: most tests assert on positions and velocities,
            // and a gravity-on default would make each of them say so again.
            m_Settings.Gravity = Vec3(0.0f);
            Initialise();
        }

        ~PhysicsFixture() = default;

        PhysicsFixture(const PhysicsFixture&) = delete;
        PhysicsFixture& operator=(const PhysicsFixture&) = delete;

        /// Starts the simulation, or records why it could not.
        void Initialise()
        {
            m_Result = m_Physics.Initialise(m_Settings);
        }

        [[nodiscard]] const Result<void>& GetResult() const { return m_Result; }

        /// Creates an entity carrying a transform and a collider.
        ///
        /// Not nodiscard: several tests set a scene up and only some of the bodies
        /// are referred to afterwards.
        Entity CreateBody(World& world,
                                        const Vec3& position,
                                        ColliderComponent collider = ColliderComponent{})
        {
            const Entity entity = world.CreateEntity();

            TransformComponent transform;
            transform.Position = position;
            world.AddComponent(entity, transform);
            world.AddComponent(entity, collider);

            return entity;
        }

        [[nodiscard]] PhysicsWorld& Get() { return m_Physics; }
        [[nodiscard]] PhysicsSettings& Settings() { return m_Settings; }

    private:
        PhysicsWorld m_Physics;
        PhysicsSettings m_Settings;
        Result<void> m_Result;
    };

    /// Advances the simulation by `seconds` at a rate that divides it evenly, so
    /// tests do not depend on how the fixed step happens to land.
    void Advance(PhysicsWorld& physics, World& world, float seconds, float step = 1.0f / 60.0f)
    {
        const int frames = static_cast<int>(std::round(seconds / step));
        for (int i = 0; i < frames; ++i)
        {
            physics.Step(world, step);
        }
    }

    ColliderComponent MakeBox(ColliderComponent::Mode mode, float halfExtent = 0.5f)
    {
        ColliderComponent collider;
        collider.BodyShape = ColliderComponent::Shape::Box;
        collider.BodyMode = mode;
        collider.Extents = Vec3(halfExtent);
        return collider;
    }
}

// ------------------------------------------------------------------- lifecycle

TEST(PhysicsWorldTest, StartsUninitialised)
{
    PhysicsWorld physics;

    EXPECT_FALSE(physics.IsInitialised());
}

TEST(PhysicsWorldTest, InitialiseCreatesASimulation)
{
    PhysicsFixture fixture;

    EXPECT_TRUE(fixture.Get().IsInitialised());
    EXPECT_TRUE(fixture.GetResult().IsSuccess());
}

TEST(PhysicsWorldTest, InitialiseIsIdempotent)
{
    PhysicsFixture fixture;
    ASSERT_TRUE(fixture.GetResult().IsSuccess());

    EXPECT_TRUE(fixture.Get().Initialise(fixture.Settings()).IsSuccess());
    EXPECT_TRUE(fixture.Get().IsInitialised());
}

TEST(PhysicsWorldTest, ShutdownIsIdempotent)
{
    PhysicsWorld physics;
    ASSERT_TRUE(physics.Initialise(PhysicsSettings{}).IsSuccess());

    physics.Shutdown();
    physics.Shutdown();

    EXPECT_FALSE(physics.IsInitialised());
}

TEST(PhysicsWorldTest, AddingABodyBeforeInitialiseFails)
{
    PhysicsWorld physics;
    World world;

    const Result<void> result = physics.AddBody(world, world.CreateEntity());

    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::PhysicsError);
}

TEST(PhysicsWorldTest, DefaultSettingsAreClampedIntoRange)
{
    PhysicsFixture fixture;

    PhysicsSettings settings;
    settings.VelocityIterations = 0;
    settings.PositionIterations = -5;
    settings.TimeStep = 0.0f;
    settings.MaximumTimeStep = 0.0f;
    settings.MaximumStepsPerFrame = 0;

    fixture.Get().SetSettings(settings);

    const PhysicsSettings applied = fixture.Get().GetSettings();
    EXPECT_GE(applied.VelocityIterations, 1);
    EXPECT_GE(applied.PositionIterations, 1);
    EXPECT_GT(applied.TimeStep, 0.0f);
    EXPECT_GE(applied.MaximumTimeStep, applied.TimeStep);
    EXPECT_GE(applied.MaximumStepsPerFrame, 1);
}

// ------------------------------------------------------------- collision layers

TEST(CollisionLayerTest, DefaultLayerExistsAfterInitialise)
{
    PhysicsFixture fixture;

    EXPECT_EQ(fixture.Get().GetCollisionLayerCount(), 1u);
    EXPECT_EQ(fixture.Get().FindCollisionLayer("Default"), 0);
}

TEST(CollisionLayerTest, LayersAreFoundByName)
{
    PhysicsFixture fixture;
    PhysicsWorld& physics = fixture.Get();

    ASSERT_TRUE(physics.SetCollisionLayer("Player", 0xFFFFFFFF).IsSuccess());
    ASSERT_TRUE(physics.SetCollisionLayer("Enemy", 0).IsSuccess());

    EXPECT_EQ(physics.FindCollisionLayer("Player"), 1);
    EXPECT_EQ(physics.FindCollisionLayer("Enemy"), 2);
    EXPECT_EQ(physics.FindCollisionLayer("Missing"), -1);
}

TEST(CollisionLayerTest, RedefiningALayerUpdatesItsMask)
{
    PhysicsFixture fixture;
    PhysicsWorld& physics = fixture.Get();

    ASSERT_TRUE(physics.SetCollisionLayer("Player", 0xFFFFFFFF).IsSuccess());
    const int index = physics.FindCollisionLayer("Player");
    ASSERT_GE(index, 0);

    ASSERT_TRUE(physics.SetCollisionLayer("Player", 0).IsSuccess());

    EXPECT_EQ(physics.GetCollisionLayerCount(), 2u);
    EXPECT_EQ(physics.GetCollisionMask(static_cast<std::size_t>(index)), 0u);
}

TEST(CollisionLayerTest, LayersCanReferenceEachOtherByName)
{
    PhysicsFixture fixture;
    PhysicsWorld& physics = fixture.Get();

    ASSERT_TRUE(physics.SetCollisionLayer("Player", std::vector<std::string>{"Default"}).IsSuccess());

    const int player = physics.FindCollisionLayer("Player");
    const int defaultLayer = physics.FindCollisionLayer("Default");
    ASSERT_GE(player, 0);

    EXPECT_NE(physics.GetCollisionMask(static_cast<std::size_t>(player)) & (1u << defaultLayer), 0u);
}

TEST(CollisionLayerTest, ReferencingAnUndeclaredLayerFails)
{
    PhysicsFixture fixture;

    const Result<void> result =
        fixture.Get().SetCollisionLayer("Player", std::vector<std::string>{"Nowhere"});

    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::NotFound);
}

TEST(CollisionLayerTest, UnnamedLayerIsRejected)
{
    PhysicsFixture fixture;

    EXPECT_TRUE(fixture.Get().SetCollisionLayer("", 0).IsFailure());
}

TEST(CollisionLayerTest, OutOfRangeLookupsAreSafe)
{
    PhysicsFixture fixture;
    PhysicsWorld& physics = fixture.Get();

    EXPECT_EQ(physics.GetCollisionMask(99), 0u);
    EXPECT_TRUE(physics.GetCollisionLayerName(99).empty());
}

TEST(CollisionLayerTest, LayerTableIsCapped)
{
    PhysicsFixture fixture;
    PhysicsWorld& physics = fixture.Get();

    for (std::size_t i = 1; i < MaxCollisionLayers; ++i)
    {
        ASSERT_TRUE(physics.SetCollisionLayer("Layer" + std::to_string(i), 0).IsSuccess());
    }

    EXPECT_EQ(physics.GetCollisionLayerCount(), MaxCollisionLayers);

    const Result<void> overflow = physics.SetCollisionLayer("OneTooMany", 0);
    ASSERT_TRUE(overflow.IsFailure());
    EXPECT_EQ(overflow.GetError().Code, ErrorCode::InvalidArgument);
}

// --------------------------------------------------------------------- bodies

TEST(PhysicsBodyTest, AddBodyRegistersIt)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));

    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());
    EXPECT_TRUE(physics.HasBody(entity));
    EXPECT_EQ(physics.GetBodyCount(), 1u);
}

TEST(PhysicsBodyTest, AddBodyFailsForADeadEntity)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = world.CreateEntity();
    world.DestroyEntity(entity);

    const Result<void> result = physics.AddBody(world, entity);
    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::InvalidArgument);
}

TEST(PhysicsBodyTest, AddBodyFailsWithoutACollider)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = world.CreateEntity();
    world.AddComponent(entity, TransformComponent{});

    const Result<void> result = physics.AddBody(world, entity);
    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::InvalidArgument);
}

TEST(PhysicsBodyTest, RemoveBodyReportsWhetherThereWasOne)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    EXPECT_TRUE(physics.RemoveBody(entity));
    EXPECT_FALSE(physics.RemoveBody(entity));
    EXPECT_FALSE(physics.HasBody(entity));
}

TEST(PhysicsBodyTest, AddBodyReplacesAnExistingOne)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));

    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    EXPECT_EQ(physics.GetBodyCount(), 1u);
}

TEST(PhysicsBodyTest, EveryShapeIsAccepted)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const ColliderComponent::Shape shapes[]{
        ColliderComponent::Shape::Box,
        ColliderComponent::Shape::Sphere,
        ColliderComponent::Shape::Capsule,
        ColliderComponent::Shape::Plane,
    };

    for (const ColliderComponent::Shape shape : shapes)
    {
        ColliderComponent collider;
        collider.BodyShape = shape;
        collider.BodyMode = ColliderComponent::Mode::Static;

        const Entity entity = fixture.CreateBody(world, Vec3(0.0f), collider);
        EXPECT_TRUE(physics.AddBody(world, entity).IsSuccess());
    }

    EXPECT_EQ(physics.GetBodyCount(), 4u);
}

TEST(PhysicsBodyTest, ZeroSizedShapesAreClampedRatherThanRejected)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    ColliderComponent collider = MakeBox(ColliderComponent::Mode::Static);
    collider.Extents = Vec3(0.0f);
    collider.Radius = 0.0f;
    collider.HalfHeight = 0.0f;

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), collider);
    EXPECT_TRUE(physics.AddBody(world, entity).IsSuccess());
}

TEST(PhysicsBodyTest, AddAllBodiesSkipsEntitiesWithoutColliders)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));
    fixture.CreateBody(world, Vec3(2.0f), MakeBox(ColliderComponent::Mode::Static));
    world.CreateEntity();

    EXPECT_EQ(physics.AddAllBodies(world), 2u);
    EXPECT_EQ(physics.GetBodyCount(), 2u);
}

TEST(PhysicsBodyTest, BodyLayerIsAssignedByName)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    ASSERT_TRUE(physics.SetCollisionLayer("Player", 0).IsSuccess());

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    EXPECT_EQ(physics.GetBodyLayer(entity), 0);
    const Result<void> layered = physics.SetBodyLayer(entity, "Player");
    ASSERT_TRUE(layered.IsSuccess());
    EXPECT_EQ(physics.GetBodyLayer(entity), 1);
}

TEST(PhysicsBodyTest, AssigningAnUnknownLayerFails)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    const Result<void> result = physics.SetBodyLayer(entity, "Nowhere");
    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::NotFound);
}

TEST(PhysicsBodyTest, StaleHandleHasNoBody)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    world.DestroyEntity(entity);

    // The body is released on the next step rather than the moment the entity
    // dies, so that a script may destroy an entity from inside its own update.
    EXPECT_TRUE(physics.HasBody(entity));
    physics.Step(world, 0.016f);

    EXPECT_FALSE(physics.HasBody(entity));
    EXPECT_EQ(physics.GetBodyLayer(entity), -1);
    EXPECT_TRUE(physics.SetLinearVelocity(entity, Vec3(1.0f)).IsFailure());
    EXPECT_TRUE(physics.ApplyImpulse(entity, Vec3(1.0f)).IsFailure());
}

// ------------------------------------------------------------------- stepping

TEST(PhysicsStepTest, StaticBodiesDoNotMove)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(1.0f, 2.0f, 3.0f), MakeBox(ColliderComponent::Mode::Static));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    Advance(physics, world, 1.0f);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(world.GetComponent<TransformComponent>(entity).Position,
                                           Vec3(1.0f, 2.0f, 3.0f), 0.01f)));
}

TEST(PhysicsStepTest, DynamicBodiesFallUnderGravity)
{
    PhysicsFixture fixture;
    fixture.Settings().Gravity = Vec3(0.0f, -10.0f, 0.0f);
    fixture.Get().SetSettings(fixture.Settings());

    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f, 10.0f, 0.0f), MakeBox(ColliderComponent::Mode::Dynamic));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    Advance(physics, world, 1.0f);

    EXPECT_LT(world.GetComponent<TransformComponent>(entity).Position.y, 10.0f);
}

TEST(PhysicsStepTest, GravityScaleIsRespected)
{
    PhysicsFixture fixture;
    fixture.Settings().Gravity = Vec3(0.0f, -10.0f, 0.0f);
    fixture.Get().SetSettings(fixture.Settings());

    World world;
    PhysicsWorld& physics = fixture.Get();

    ColliderComponent collider = MakeBox(ColliderComponent::Mode::Dynamic);
    collider.GravityScale = 0.0f;

    const Entity floating = fixture.CreateBody(world, Vec3(0.0f, 10.0f, 0.0f), collider);
    ASSERT_TRUE(physics.AddBody(world, floating).IsSuccess());

    Advance(physics, world, 1.0f);

    EXPECT_NEAR(world.GetComponent<TransformComponent>(floating).Position.y, 10.0f, 0.01f);
}

TEST(PhysicsStepTest, ZeroDeltaTimeDoesNotAdvanceTime)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));
    ASSERT_EQ(physics.AddAllBodies(world), 1u);

    physics.Step(world, 0.0f);

    EXPECT_DOUBLE_EQ(physics.GetSimulatedTime(), 0.0);
}

TEST(PhysicsStepTest, SteppingAccumulatesSimulatedTime)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));
    ASSERT_EQ(physics.AddAllBodies(world), 1u);

    Advance(physics, world, 0.5f);

    EXPECT_NEAR(physics.GetSimulatedTime(), 0.5, 0.02);
}

TEST(PhysicsStepTest, APartialStepIsCarriedToTheNextFrame)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static));
    ASSERT_EQ(physics.AddAllBodies(world), 1u);

    // The fixed step is 1/60s, so half of one is carried rather than rounded.
    physics.Step(world, fixture.Settings().TimeStep * 0.5f);
    EXPECT_DOUBLE_EQ(physics.GetSimulatedTime(), 0.0);

    physics.Step(world, fixture.Settings().TimeStep * 0.5f);
    EXPECT_NEAR(physics.GetSimulatedTime(), fixture.Settings().TimeStep, 1e-6);
}

TEST(PhysicsStepTest, SteppingWithoutBodiesIsHarmless)
{
    PhysicsFixture fixture;
    World world;

    fixture.Get().Step(world, 0.016f);

    EXPECT_DOUBLE_EQ(fixture.Get().GetSimulatedTime(), 0.0);
}

TEST(PhysicsStepTest, DestroyingAnEntityRemovesItsBody)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Dynamic));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());
    ASSERT_EQ(physics.GetBodyCount(), 1u);

    world.DestroyEntity(entity);
    physics.Step(world, 0.016f);

    EXPECT_EQ(physics.GetBodyCount(), 0u);
}

TEST(PhysicsStepTest, ADynamicBodyLandsOnAStaticFloor)
{
    PhysicsFixture fixture;
    fixture.Settings().Gravity = Vec3(0.0f, -10.0f, 0.0f);
    fixture.Get().SetSettings(fixture.Settings());

    World world;
    PhysicsWorld& physics = fixture.Get();

    // A wide, thin slab rather than a big cube: a cube of this size would swallow
    // the falling box, and the simulation would push it back out rather than
    // letting it land.
    ColliderComponent floor = MakeBox(ColliderComponent::Mode::Static, 10.0f);
    floor.Extents = Vec3(10.0f, 0.5f, 10.0f);
    const Entity ground = fixture.CreateBody(world, Vec3(0.0f, 0.0f, 0.0f), floor);
    ASSERT_TRUE(physics.AddBody(world, ground).IsSuccess());

    const Entity falling = fixture.CreateBody(world, Vec3(0.0f, 5.0f, 0.0f), MakeBox(ColliderComponent::Mode::Dynamic));
    ASSERT_TRUE(physics.AddBody(world, falling).IsSuccess());

    Advance(physics, world, 3.0f);

    // The floor's top face is at y = 0.5, and the falling box's half-height is 0.5,
    // so it comes to rest resting on it rather than passing through.
    const float restingHeight = world.GetComponent<TransformComponent>(falling).Position.y;
    EXPECT_GT(restingHeight, 0.8f);
    EXPECT_LT(restingHeight, 1.2f);
}

// ------------------------------------------------------------------- velocity

TEST(PhysicsVelocityTest, SetAndGetLinearVelocity)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Dynamic));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    ASSERT_TRUE(physics.SetLinearVelocity(entity, Vec3(1.0f, 2.0f, 3.0f)).IsSuccess());

    EXPECT_TRUE(glm::all(glm::epsilonEqual(physics.GetLinearVelocity(entity), Vec3(1.0f, 2.0f, 3.0f), 1e-5f)));
}

TEST(PhysicsVelocityTest, VelocityOfABodylessEntityIsZero)
{
    PhysicsFixture fixture;
    World world;

    EXPECT_TRUE(glm::all(glm::epsilonEqual(fixture.Get().GetLinearVelocity(world.CreateEntity()), Vec3(0.0f), 1e-6f)));
}

TEST(PhysicsVelocityTest, SetAndGetAngularVelocity)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Dynamic));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    ASSERT_TRUE(physics.SetAngularVelocity(entity, Vec3(0.0f, 4.0f, 0.0f)).IsSuccess());

    EXPECT_NEAR(physics.GetAngularVelocity(entity).y, 4.0f, 1e-4);
}

TEST(PhysicsVelocityTest, ImpulseChangesVelocity)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    ColliderComponent collider = MakeBox(ColliderComponent::Mode::Dynamic);
    collider.Mass = 2.0f;
    collider.GravityScale = 0.0f;

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), collider);
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    ASSERT_TRUE(physics.ApplyImpulse(entity, Vec3(4.0f, 0.0f, 0.0f)).IsSuccess());
    Advance(physics, world, 0.1f);

    // An impulse changes momentum, so a heavier body ends up slower.
    EXPECT_GT(physics.GetLinearVelocity(entity).x, 0.5f);
}

TEST(PhysicsVelocityTest, AngularImpulseSpinsABody)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    ColliderComponent collider = MakeBox(ColliderComponent::Mode::Dynamic);
    collider.GravityScale = 0.0f;

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), collider);
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    ASSERT_TRUE(physics.ApplyAngularImpulse(entity, Vec3(0.0f, 10.0f, 0.0f)).IsSuccess());
    Advance(physics, world, 0.1f);

    EXPECT_NE(physics.GetAngularVelocity(entity).y, 0.0f);
}

TEST(PhysicsVelocityTest, MassComesFromTheCollider)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    ColliderComponent collider = MakeBox(ColliderComponent::Mode::Dynamic);
    collider.Mass = 7.5f;

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), collider);
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    EXPECT_NEAR(physics.GetMass(entity), 7.5f, 0.01);
}

TEST(PhysicsVelocityTest, VolumeReflectsTheShape)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity box = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Static, 1.0f));
    ASSERT_TRUE(physics.AddBody(world, box).IsSuccess());

    // A box with half-extents of one is two units on a side.
    EXPECT_NEAR(physics.GetVolume(box), 8.0f, 0.01);
}

TEST(PhysicsVelocityTest, VolumeOfABodylessEntityIsZero)
{
    PhysicsFixture fixture;
    World world;

    EXPECT_FLOAT_EQ(fixture.Get().GetVolume(world.CreateEntity()), 0.0f);
}

TEST(PhysicsTeleportTest, TeleportMovesTheBodyAndClearsItsVelocity)
{
    PhysicsFixture fixture;
    World world;
    PhysicsWorld& physics = fixture.Get();

    const Entity entity = fixture.CreateBody(world, Vec3(0.0f), MakeBox(ColliderComponent::Mode::Dynamic));
    ASSERT_TRUE(physics.AddBody(world, entity).IsSuccess());

    ASSERT_TRUE(physics.SetLinearVelocity(entity, Vec3(10.0f, 0.0f, 0.0f)).IsSuccess());
    ASSERT_TRUE(physics.Teleport(entity, Vec3(5.0f, 6.0f, 7.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f)).IsSuccess());

    // The transform follows the simulation, so the teleport shows up on the next
    // step rather than the moment it is requested.
    Advance(physics, world, 0.05f);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(world.GetComponent<TransformComponent>(entity).Position,
                                           Vec3(5.0f, 6.0f, 7.0f), 0.01f)));

    Advance(physics, world, 0.05f);

    // The old velocity is not carried across, so the body stays where it was put.
    EXPECT_TRUE(glm::all(glm::epsilonEqual(world.GetComponent<TransformComponent>(entity).Position,
                                           Vec3(5.0f, 6.0f, 7.0f), 0.05f)));
}

TEST(PhysicsTeleportTest, TeleportOfABodylessEntityFails)
{
    PhysicsFixture fixture;
    World world;

    EXPECT_TRUE(fixture.Get()
                    .Teleport(world.CreateEntity(), Vec3(1.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f))
                    .IsFailure());
}