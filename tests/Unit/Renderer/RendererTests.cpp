#include <gtest/gtest.h>

#include <cmath>

#include "Ecs/Components.h"
#include "Renderer/Renderer.h"

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

    /// A camera looking down the -Z axis from the origin's positive side.
    [[nodiscard]] Camera DefaultCamera()
    {
        Camera camera;
        camera.Position = Vec3(0.0f, 0.0f, 10.0f);
        camera.Target = Vec3(0.0f);
        camera.Up = Vec3(0.0f, 1.0f, 0.0f);
        camera.FieldOfViewDegrees = 60.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 100.0f;
        camera.AspectRatio = 1.0f;

        return camera;
    }

    /// Adds a mesh entity with a unit box's bounds.
    ///
    /// Not nodiscard: most tests set a scene up and refer to the entities only
    /// some of the time.
    Entity AddMesh(World& world, RenderQueue& queue, const Vec3& position,
                   bool visible = true, bool doubleSided = false)
    {
        const Entity entity = world.CreateEntity();

        TransformComponent transform;
        transform.Position = position;
        world.AddComponent(entity, transform);

        MeshComponent mesh;
        mesh.MeshId = 1;
        mesh.Visible = visible;
        mesh.DoubleSided = doubleSided;
        world.AddComponent(entity, mesh);

        queue.SetMeshBounds(1, Vec3(-0.5f), Vec3(0.5f));
        return entity;
    }
}

// ------------------------------------------------------------------- matrices

TEST(CameraTest, ViewPlacesTheTargetInFrontOfTheCamera)
{
    const Camera camera = DefaultCamera();

    const Vec4 targetInView = camera.GetViewMatrix() * Vec4(camera.Target, 1.0f);

    // A right-handed camera looks down -Z.
    EXPECT_NEAR(targetInView.z, -10.0f, 1e-4f);
}

TEST(CameraTest, ProjectionMapsTheClipRangeToZeroOne)
{
    const Camera camera = DefaultCamera();
    const Mat4 projection = camera.GetProjectionMatrix();

    const Vec4 atNear = projection * Vec4(0.0f, 0.0f, -camera.NearPlane, 1.0f);
    const Vec4 atFar = projection * Vec4(0.0f, 0.0f, -camera.FarPlane, 1.0f);

    EXPECT_NEAR(atNear.z / atNear.w, 0.0f, 1e-4f);
    EXPECT_NEAR(atFar.z / atFar.w, 1.0f, 1e-4f);
}

TEST(CameraTest, ViewProjectionMapsVisiblePointsInsideClipSpace)
{
    const Camera camera = DefaultCamera();
    const Mat4 viewProjection = camera.GetViewProjectionMatrix();

    const Vec4 centre = viewProjection * Vec4(camera.Target, 1.0f);
    EXPECT_NEAR(centre.x / centre.w, 0.0f, 1e-4f);
    EXPECT_NEAR(centre.y / centre.w, 0.0f, 1e-4f);
    EXPECT_GT(centre.z / centre.w, 0.0f);
    EXPECT_LT(centre.z / centre.w, 1.0f);
}

TEST(CameraTest, OrthographicCameraKeepsTheSameDepthRange)
{
    Camera camera = DefaultCamera();
    camera.Orthographic = true;
    camera.OrthographicSize = 5.0f;

    const Mat4 viewProjection = camera.GetViewProjectionMatrix();
    const Vec3 forward = glm::normalize(camera.Target - camera.Position);

    // An orthographic depth is linear in distance, so the near plane is at 0 and
    // the far plane at 1 just as a perspective one is. The points are measured
    // along the camera's forward axis rather than world Z, because that is the
    // axis the near and far planes are defined against.
    const Vec4 atNear = viewProjection * Vec4(camera.Position + forward * camera.NearPlane, 1.0f);
    const Vec4 atFar = viewProjection * Vec4(camera.Position + forward * camera.FarPlane, 1.0f);

    EXPECT_NEAR(atNear.z / atNear.w, 0.0f, 1e-4f);
    EXPECT_NEAR(atFar.z / atFar.w, 1.0f, 1e-4f);
}

// ------------------------------------------------------------- frustum planes

TEST(FrustumTest, TheLookAtPointIsInsideEveryPlane)
{
    const Camera camera = DefaultCamera();
    const std::array<Plane, 6> frustum = camera.GetFrustumPlanes();

    EXPECT_TRUE(IsInside(frustum, camera.Target, 0.01f));
}

TEST(FrustumTest, APointBehindTheCameraIsOutside)
{
    const Camera camera = DefaultCamera();
    const std::array<Plane, 6> frustum = camera.GetFrustumPlanes();

    EXPECT_FALSE(IsInside(frustum, Vec3(0.0f, 0.0f, 40.0f), 0.01f));
}

TEST(FrustumTest, APointFarToTheSideIsOutside)
{
    const Camera camera = DefaultCamera();
    const std::array<Plane, 6> frustum = camera.GetFrustumPlanes();

    EXPECT_FALSE(IsInside(frustum, Vec3(1000.0f, 0.0f, 0.0f), 0.01f));
}

TEST(FrustumTest, APointAboveAndBelowIsOutside)
{
    const Camera camera = DefaultCamera();
    const std::array<Plane, 6> frustum = camera.GetFrustumPlanes();

    EXPECT_FALSE(IsInside(frustum, Vec3(0.0f, 1000.0f, 0.0f), 0.01f));
    EXPECT_FALSE(IsInside(frustum, Vec3(0.0f, -1000.0f, 0.0f), 0.01f));
}

TEST(FrustumTest, APointBeyondTheFarPlaneIsOutside)
{
    const Camera camera = DefaultCamera();
    const std::array<Plane, 6> frustum = camera.GetFrustumPlanes();

    EXPECT_FALSE(IsInside(frustum, Vec3(0.0f, 0.0f, -camera.FarPlane - 10.0f), 0.01f));
}

TEST(FrustumTest, AStraddlingSphereIsStillVisible)
{
    const Camera camera = DefaultCamera();
    const std::array<Plane, 6> frustum = camera.GetFrustumPlanes();

    // The centre is outside, but the sphere reaches back inside the frustum, so
    // culling it would make it pop at the screen edge.
    const Vec3 justOutside(-camera.Target.x, 0.0f, -camera.FarPlane - 1.0f);
    EXPECT_FALSE(IsInside(frustum, justOutside, 0.01f));
    EXPECT_TRUE(IsInside(frustum, justOutside, 100.0f));
}

TEST(FrustumTest, PlaneNormalsAreUnitLength)
{
    const Camera camera = DefaultCamera();

    for (const Plane& plane : camera.GetFrustumPlanes())
    {
        // A plane's distance is only a distance when its normal is unit length.
        EXPECT_NEAR(glm::length(plane.Normal), 1.0f, 1e-4f);
    }
}

TEST(FrustumTest, BoxesAreTestedByTheirBoundingSphere)
{
    const Camera camera = DefaultCamera();
    const std::array<Plane, 6> frustum = camera.GetFrustumPlanes();

    EXPECT_TRUE(IsInside(frustum, Vec3(-0.4f), Vec3(0.4f)));
    EXPECT_FALSE(IsInside(frustum, Vec3(1000.0f), Vec3(1001.0f)));
}

// --------------------------------------------------------------------- shadows

TEST(ShadowTest, ProjectionCoversTheWholeScene)
{
    const ShadowProjection shadow =
        BuildDirectionalShadow(Vec3(-0.5f, -1.0f, 0.0f), Vec3(0.0f), 10.0f, 1024);

    // The shadow map covers the scene's bounding sphere, not its box: a sphere
    // projects to a disc under any view, which is what makes the orthographic
    // bounds sufficient for arbitrary geometry inside it.
    for (int corner = 0; corner < 8; ++corner)
    {
        const Vec3 direction((corner & 1) ? 1.0f : -1.0f,
                             (corner & 2) ? 1.0f : -1.0f,
                             (corner & 4) ? 1.0f : -1.0f);

        const Vec3 world = glm::normalize(direction) * 9.99f;
        const Vec3 projected = shadow.ProjectToLightSpace(world);

        EXPECT_GE(projected.x, 0.0f) << world.x << ", " << world.y << ", " << world.z;
        EXPECT_LE(projected.x, 1.0f);
        EXPECT_GE(projected.y, 0.0f);
        EXPECT_LE(projected.y, 1.0f);
    }
}

TEST(ShadowTest, TexelSizeFollowsTheMapResolution)
{
    const ShadowProjection coarse = BuildDirectionalShadow(Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f), 10.0f, 512);
    const ShadowProjection fine = BuildDirectionalShadow(Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f), 10.0f, 2048);

    EXPECT_NEAR(coarse.TexelWorldSize, 20.0f / 512.0f, 1e-5f);
    EXPECT_NEAR(fine.TexelWorldSize, 20.0f / 2048.0f, 1e-5f);
    EXPECT_LT(fine.TexelWorldSize, coarse.TexelWorldSize);
}

TEST(ShadowTest, CentreIsSnappedToWholeTexels)
{
    const ShadowProjection shadow =
        BuildDirectionalShadow(Vec3(0.0f, -1.0f, 0.0f), Vec3(0.013f, 0.0f, 0.0f), 10.0f, 1024);

    // Snapping is what stops shadow edges crawling as the camera moves. The
    // projected centre has to land on a texel boundary for that to hold.
    const Vec3 projected = shadow.ProjectToLightSpace(Vec3(0.0f));
    EXPECT_NEAR(projected.x, 0.5f, shadow.TexelWorldSize);
}

TEST(ShadowTest, LightSpaceIsInZeroOne)
{
    const ShadowProjection shadow =
        BuildDirectionalShadow(Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f), 10.0f, 1024);

    const Vec3 centre = shadow.ProjectToLightSpace(Vec3(0.0f));

    EXPECT_GE(centre.x, 0.0f);
    EXPECT_LE(centre.x, 1.0f);
    EXPECT_GE(centre.y, 0.0f);
    EXPECT_LE(centre.y, 1.0f);
    EXPECT_GE(centre.z, 0.0f);
    EXPECT_LE(centre.z, 1.0f);
}

TEST(ShadowTest, ADownwardLightMapsTheGroundToTheCentre)
{
    // Looking straight down, the ground is directly ahead of the light.
    const ShadowProjection shadow =
        BuildDirectionalShadow(Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f), 10.0f, 1024);

    const Vec3 ground = shadow.ProjectToLightSpace(Vec3(0.0f));

    EXPECT_NEAR(ground.x, 0.5f, 1e-3f);
    EXPECT_NEAR(ground.y, 0.5f, 1e-3f);
}

TEST(ShadowTest, AZeroRadiusStillProducesAUsableProjection)
{
    const ShadowProjection shadow =
        BuildDirectionalShadow(Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f), 0.0f, 1024);

    EXPECT_FLOAT_EQ(shadow.TexelWorldSize, 0.0f);
    EXPECT_NO_THROW((void)shadow.ProjectToLightSpace(Vec3(0.0f)));
}

TEST(ShadowTest, ADegenerateDirectionDoesNotProduceNaNs)
{
    const ShadowProjection shadow =
        BuildDirectionalShadow(Vec3(0.0f), Vec3(0.0f), 10.0f, 1024);

    const Vec3 projected = shadow.ProjectToLightSpace(Vec3(0.0f));

    EXPECT_TRUE(std::isfinite(projected.x));
    EXPECT_TRUE(std::isfinite(projected.y));
    EXPECT_TRUE(std::isfinite(projected.z));
}

// --------------------------------------------------------- screen-space maths

TEST(ScreenSpaceTest, FarPlaneReconstructsToTheFarPlane)
{
    const Camera camera = DefaultCamera();
    const Mat4 inverse = glm::inverse(camera.GetViewProjectionMatrix());

    const Vec2 screenSize(1920.0f, 1080.0f);
    const Vec3 reconstructed = ReconstructWorldPosition(inverse, Vec2(960.0f, 540.0f), 1.0f, screenSize);

    // The centre pixel at the far plane is straight ahead of the camera, at the
    // far plane's distance.
    const Vec3 expected = camera.Position + glm::normalize(camera.Target - camera.Position) * camera.FarPlane;

    EXPECT_TRUE(glm::all(glm::epsilonEqual(reconstructed, expected, 0.1f)));
}

TEST(ScreenSpaceTest, NearPlaneReconstructsToTheNearPlane)
{
    const Camera camera = DefaultCamera();
    const Mat4 inverse = glm::inverse(camera.GetViewProjectionMatrix());

    const Vec2 screenSize(1920.0f, 1080.0f);
    const Vec3 reconstructed = ReconstructWorldPosition(inverse, Vec2(960.0f, 540.0f), 0.0f, screenSize);

    const Vec3 expected = camera.Position + glm::normalize(camera.Target - camera.Position) * camera.NearPlane;

    EXPECT_TRUE(glm::all(glm::epsilonEqual(reconstructed, expected, 0.01f)));
}

TEST(ScreenSpaceTest, TheCentrePixelReconstructsAlongTheViewAxis)
{
    const Camera camera = DefaultCamera();
    const Mat4 inverse = glm::inverse(camera.GetViewProjectionMatrix());

    const Vec2 screenSize(1920.0f, 1080.0f);
    const Vec3 centre = ReconstructWorldPosition(inverse, Vec2(960.0f, 540.0f), 0.5f, screenSize);

    // Directly ahead of the camera, somewhere between its near and far planes.
    // Clip depth is not linear in distance, so half depth is well past the target
    // rather than halfway to it.
    EXPECT_NEAR(centre.x, camera.Target.x, 1e-2f);
    EXPECT_NEAR(centre.y, camera.Target.y, 1e-2f);

    const float distance = glm::length(centre - camera.Position);
    EXPECT_GT(distance, camera.NearPlane);
    EXPECT_LT(distance, camera.FarPlane);
}

TEST(ScreenSpaceTest, ScreenYIsFlippedBetweenSpaces)
{
    const Camera camera = DefaultCamera();
    const Mat4 inverse = glm::inverse(camera.GetViewProjectionMatrix());
    const Vec2 screenSize(1920.0f, 1080.0f);

    const Vec3 top = ReconstructWorldPosition(inverse, Vec2(960.0f, 0.0f), 1.0f, screenSize);
    const Vec3 bottom = ReconstructWorldPosition(inverse, Vec2(960.0f, 1080.0f), 1.0f, screenSize);

    // Screen space grows downwards and clip space grows upwards, so the top of the
    // screen is the top of the view.
    EXPECT_GT(top.y, bottom.y);
}

TEST(ScreenSpaceTest, ScreenXTracksTheViewAxis)
{
    const Camera camera = DefaultCamera();
    const Mat4 inverse = glm::inverse(camera.GetViewProjectionMatrix());
    const Vec2 screenSize(1920.0f, 1080.0f);

    const Vec3 left = ReconstructWorldPosition(inverse, Vec2(0.0f, 540.0f), 1.0f, screenSize);
    const Vec3 right = ReconstructWorldPosition(inverse, Vec2(1920.0f, 540.0f), 1.0f, screenSize);

    EXPECT_LT(left.x, right.x);
}

TEST(ScreenSpaceTest, AProjectedRadiusShrinksWithDistance)
{
    const float near = ProjectRadiusToScreen(1.0f, 5.0f, 1080.0f, 60.0f);
    const float far = ProjectRadiusToScreen(1.0f, 50.0f, 1080.0f, 60.0f);

    EXPECT_GT(near, 0.0f);
    EXPECT_NEAR(far, near / 10.0f, 1e-4f);
}

TEST(ScreenSpaceTest, AProjectedRadiusGrowsWithScreenHeight)
{
    const float small = ProjectRadiusToScreen(1.0f, 10.0f, 540.0f, 60.0f);
    const float large = ProjectRadiusToScreen(1.0f, 10.0f, 1080.0f, 60.0f);

    EXPECT_NEAR(large, small * 2.0f, 1e-4f);
}

TEST(ScreenSpaceTest, AProjectedRadiusIsZeroBehindTheCamera)
{
    EXPECT_FLOAT_EQ(ProjectRadiusToScreen(1.0f, 0.0f, 1080.0f, 60.0f), 0.0f);
    EXPECT_FLOAT_EQ(ProjectRadiusToScreen(1.0f, -5.0f, 1080.0f, 60.0f), 0.0f);
}

// -------------------------------------------------------------- render queue

TEST(RenderQueueTest, AnEmptyWorldProducesAnEmptyQueue)
{
    World world;
    RenderQueue queue;

    queue.Build(world, DefaultCamera());

    EXPECT_TRUE(queue.GetDrawItems().empty());
    EXPECT_TRUE(queue.GetShadowCasters().empty());
    EXPECT_EQ(queue.GetCulledCount(), 0u);
}

TEST(RenderQueueTest, VisibleMeshesAreQueued)
{
    World world;
    RenderQueue queue;

    AddMesh(world, queue, Vec3(0.0f, 0.0f, 0.0f));
    AddMesh(world, queue, Vec3(2.0f, 0.0f, 0.0f));

    queue.Build(world, DefaultCamera());

    EXPECT_EQ(queue.GetDrawItems().size(), 2u);
}

TEST(RenderQueueTest, InvisibleMeshesAreSkipped)
{
    World world;
    RenderQueue queue;

    AddMesh(world, queue, Vec3(0.0f), false);

    queue.Build(world, DefaultCamera());

    EXPECT_TRUE(queue.GetDrawItems().empty());
}

TEST(RenderQueueTest, DisabledEntitiesAreSkipped)
{
    World world;
    RenderQueue queue;

    const Entity mesh = AddMesh(world, queue, Vec3(0.0f));
    world.SetEnabled(mesh, false);

    queue.Build(world, DefaultCamera());

    EXPECT_TRUE(queue.GetDrawItems().empty());
}

TEST(RenderQueueTest, EntitiesUnderADisabledParentAreSkipped)
{
    World world;
    RenderQueue queue;

    const Entity parent = world.CreateEntity();
    const Entity child = AddMesh(world, queue, Vec3(0.0f));
    world.SetParent(child, parent);
    world.SetEnabled(parent, false);

    queue.Build(world, DefaultCamera());

    EXPECT_TRUE(queue.GetDrawItems().empty());
}

TEST(RenderQueueTest, MeshesBehindTheCameraAreCulled)
{
    World world;
    RenderQueue queue;

    AddMesh(world, queue, Vec3(0.0f, 0.0f, 500.0f));

    queue.Build(world, DefaultCamera());

    EXPECT_TRUE(queue.GetDrawItems().empty());
    EXPECT_EQ(queue.GetCulledCount(), 1u);
}

TEST(RenderQueueTest, MeshesWithoutKnownBoundsAreNotCulled)
{
    World world;
    RenderQueue queue;

    const Entity entity = world.CreateEntity();
    TransformComponent transform;
    transform.Position = Vec3(0.0f, 0.0f, 500.0f);
    world.AddComponent(entity, transform);
    world.AddComponent(entity, MeshComponent{});

    queue.Build(world, DefaultCamera());

    // Without bounds there is nothing to test against, so the mesh is drawn rather
    // than silently disappearing.
    EXPECT_EQ(queue.GetDrawItems().size(), 1u);
}

TEST(RenderQueueTest, TheQueueCarriesEachEntitysTransform)
{
    World world;
    RenderQueue queue;

    const Vec3 position(1.0f, 2.0f, 0.0f);
    AddMesh(world, queue, position);

    queue.Build(world, DefaultCamera());

    ASSERT_EQ(queue.GetDrawItems().size(), 1u);
    const Vec4 translation = queue.GetDrawItems()[0].Transform[3];
    EXPECT_NEAR(translation.x, position.x, 1e-5f);
    EXPECT_NEAR(translation.y, position.y, 1e-5f);
}

TEST(RenderQueueTest, DoubleSidedMeshesDisableBackFaceCulling)
{
    World world;
    RenderQueue queue;

    const Entity thin = AddMesh(world, queue, Vec3(0.0f), true, true);
    const Entity solid = AddMesh(world, queue, Vec3(2.0f), true, false);

    queue.Build(world, DefaultCamera());

    // The queue is sorted by depth, so the items are looked up by entity rather
    // than by position in the list.
    ASSERT_EQ(queue.GetDrawItems().size(), 2u);

    const auto findCull = [&queue](Entity entity)
    {
        for (const DrawItem& item : queue.GetDrawItems())
        {
            if (item.Entity == entity)
            {
                return item.Cull;
            }
        }

        return CullMode::Back;
    };

    EXPECT_EQ(findCull(thin), CullMode::None);
    EXPECT_EQ(findCull(solid), CullMode::Back);
}

TEST(RenderQueueTest, OpaqueGeometryIsSortedFrontToBack)
{
    World world;
    RenderQueue queue;

    AddMesh(world, queue, Vec3(0.0f, 0.0f, -8.0f));
    AddMesh(world, queue, Vec3(0.0f, 0.0f, 0.0f));
    AddMesh(world, queue, Vec3(0.0f, 0.0f, -4.0f));

    queue.Build(world, DefaultCamera());

    const std::vector<DrawItem>& items = queue.GetDrawItems();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_LE(items[0].Depth, items[1].Depth);
    EXPECT_LE(items[1].Depth, items[2].Depth);
}

TEST(RenderQueueTest, RebuildingReplacesThePreviousFrame)
{
    World world;
    RenderQueue queue;

    AddMesh(world, queue, Vec3(0.0f));
    queue.Build(world, DefaultCamera());
    EXPECT_EQ(queue.GetDrawItems().size(), 1u);

    queue.Build(world, DefaultCamera());
    EXPECT_EQ(queue.GetDrawItems().size(), 1u);

    queue.Clear();
    EXPECT_TRUE(queue.GetDrawItems().empty());
}

TEST(RenderQueueTest, SceneBoundsCoverTheQueuedGeometry)
{
    World world;
    RenderQueue queue;

    AddMesh(world, queue, Vec3(-5.0f, 0.0f, 0.0f));
    AddMesh(world, queue, Vec3(5.0f, 0.0f, 0.0f));

    queue.Build(world, DefaultCamera());

    EXPECT_LE(queue.GetSceneBoundsMin().x, -5.5f);
    EXPECT_GE(queue.GetSceneBoundsMax().x, 5.5f);
}

TEST(RenderQueueTest, MeshBoundsAreTransformedByTheEntity)
{
    World world;
    RenderQueue queue;

    // The same mesh, scaled up, must produce larger scene bounds.
    const Entity entity = world.CreateEntity();
    TransformComponent transform;
    transform.Position = Vec3(0.0f);
    transform.Scale = Vec3(4.0f);
    world.AddComponent(entity, transform);
    MeshComponent mesh;
    mesh.MeshId = 1;
    world.AddComponent(entity, mesh);
    queue.SetMeshBounds(1, Vec3(-0.5f), Vec3(0.5f));

    queue.Build(world, DefaultCamera());

    // A unit box scaled by four is two units either side of the origin.
    EXPECT_GE(queue.GetSceneBoundsMax().x, 2.0f);
}

// --------------------------------------------------------------------- lights

TEST(RenderQueueTest, DirectionalLightsAreCollectedSeparately)
{
    World world;
    RenderQueue queue;

    const Entity light = world.CreateEntity();
    world.AddComponent(light, LightComponent{});

    queue.Build(world, DefaultCamera());

    EXPECT_EQ(queue.GetDirectionalLights().size(), 1u);
    EXPECT_TRUE(queue.GetPointLights().empty());
}

TEST(RenderQueueTest, PointLightsAreCollectedSeparately)
{
    World world;
    RenderQueue queue;

    const Entity light = world.CreateEntity();
    LightComponent component;
    component.LightType = LightComponent::Type::Point;
    world.AddComponent(light, component);

    queue.Build(world, DefaultCamera());

    EXPECT_EQ(queue.GetPointLights().size(), 1u);
    EXPECT_TRUE(queue.GetDirectionalLights().empty());
}

TEST(RenderQueueTest, DisabledLightsAreSkipped)
{
    World world;
    RenderQueue queue;

    const Entity light = world.CreateEntity();
    world.AddComponent(light, LightComponent{});
    world.SetEnabled(light, false);

    queue.Build(world, DefaultCamera());

    EXPECT_TRUE(queue.GetDirectionalLights().empty());
}

TEST(RenderQueueTest, ASingleDirectionalLightGetsOneShadowProjection)
{
    World world;
    RenderQueue queue;

    const Entity light = world.CreateEntity();
    world.AddComponent(light, LightComponent{});

    queue.Build(world, DefaultCamera());

    ASSERT_EQ(queue.GetShadowProjections().size(), 1u);
    EXPECT_GT(queue.GetShadowProjections()[0].TexelWorldSize, 0.0f);
}

TEST(RenderQueueTest, ALightThatDoesNotCastShadowsHasNoProjection)
{
    World world;
    RenderQueue queue;

    const Entity light = world.CreateEntity();
    LightComponent component;
    component.CastsShadows = false;
    world.AddComponent(light, component);

    queue.Build(world, DefaultCamera());

    EXPECT_EQ(queue.GetDirectionalLights().size(), 1u);
    EXPECT_TRUE(queue.GetShadowProjections().empty());
}

TEST(RenderQueueTest, APointLightNeverGetsADirectionalShadowProjection)
{
    World world;
    RenderQueue queue;

    const Entity light = world.CreateEntity();
    LightComponent component;
    component.LightType = LightComponent::Type::Point;
    world.AddComponent(light, component);

    queue.Build(world, DefaultCamera());

    EXPECT_EQ(queue.GetPointLights().size(), 1u);
    EXPECT_TRUE(queue.GetShadowProjections().empty());
}