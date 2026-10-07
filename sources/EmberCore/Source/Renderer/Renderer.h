// Renderer/Renderer.h
//
// The renderer.
//
// The engine renders in a fixed order: shadow maps first, so that the main pass
// can sample them; then a depth-and-normal prepass, so that screen-space effects
// have something to work from; then the opaque pass, then transparents, then the
// post chain.
//
// The parts of a renderer that can be wrong without a GPU are the ones that
// decide *what* to draw and *how*: frustum culling, sorting, shadow bounds, the
// screen-space reconstruction of a pixel's world position. All of that lives here,
// separately from the parts that need a device, and is what the tests exercise.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Core/Math/Math.h"
#include "Core/Result.h"
#include "Ecs/Components.h"
#include "Ecs/World.h"
#include "Renderer/Material.h"
#include "Renderer/Mesh.h"

namespace Ember
{
    /// How a mesh's triangles are rasterised.
    enum class CullMode : std::int32_t
    {
        /// Cull nothing. Used for thin geometry such as foliage.
        None = 0,

        /// Cull back faces, the default for a closed solid.
        Back = 1
    };

    /// A plane as a unit normal and a signed distance along it.
    ///
    /// Positive is inside, which is what a frustum's planes use and what lets a
    /// containment test be a single dot product.
    struct Plane
    {
        Vec3 Normal = Vec3(0.0f, 1.0f, 0.0f);
        float Distance = 0.0f;

        /// Signed distance of a point, positive inside.
        [[nodiscard]] float SignedDistance(const Vec3& point) const noexcept
        {
            return glm::dot(Normal, point) + Distance;
        }
    };

    /// The camera the renderer draws from.
    struct Camera
    {
        Vec3 Position = Vec3(0.0f, 2.0f, 5.0f);
        Vec3 Target = Vec3(0.0f);
        Vec3 Up = Vec3(0.0f, 1.0f, 0.0f);

        float FieldOfViewDegrees = 60.0f;
        float NearPlane = 0.1f;
        float FarPlane = 1000.0f;
        float OrthographicSize = 10.0f;
        bool Orthographic = false;

        float AspectRatio = 16.0f / 9.0f;

        [[nodiscard]] Mat4 GetViewMatrix() const noexcept;
        [[nodiscard]] Mat4 GetProjectionMatrix() const noexcept;

        /// View and projection together, the matrix a world-space vertex needs.
        [[nodiscard]] Mat4 GetViewProjectionMatrix() const noexcept;

        /// The six frustum planes, each as `(normal, distance)`, with normals
        /// pointing inwards. Ready for `IsInside`.
        [[nodiscard]] std::array<Plane, 6> GetFrustumPlanes() const noexcept;
    };

    /// True when a sphere is at least partly inside a frustum.
    ///
    /// Conservative: a sphere that straddles a plane is treated as visible, so a
    /// mesh is never culled by a plane it does touch.
    [[nodiscard]] bool IsInside(const std::array<Plane, 6>& frustum, const Vec3& centre, float radius) noexcept;

    /// True when an axis-aligned box is at least partly inside a frustum.
    [[nodiscard]] bool IsInside(const std::array<Plane, 6>& frustum, const Vec3& minimum, const Vec3& maximum) noexcept;

    /// One mesh's place in the frame's draw list.
    struct DrawItem
    {
        Entity Entity = Entity::Null();
        std::uint32_t MeshId = 0;
        std::uint32_t MaterialId = 0;
        Mat4 Transform = Mat4(1.0f);

        /// Range of the mesh's index buffer to draw, for a mesh drawn in part.
        std::uint32_t FirstIndex = 0;
        std::uint32_t IndexCount = 0;

        /// Distance from the camera to the item's centre, used for sorting.
        float Depth = 0.0f;

        CullMode Cull = CullMode::Back;
        bool DoubleSided = false;
    };

    /// A light's contribution to the frame, as the renderer needs it.
    struct LightDrawItem
    {
        Entity Entity = Entity::Null();
        Vec3 Position = Vec3(0.0f);
        Vec3 Direction = Vec3(0.0f, -1.0f, 0.0f);
        Vec3 Color = Vec3(1.0f);
        float Intensity = 1.0f;
        float Range = 20.0f;
        float InnerAngleDegrees = 25.0f;
        std::uint32_t ShadowResolution = 2048;
        bool CastsShadows = true;

        [[nodiscard]] LightComponent::Type GetType() const noexcept
        {
            return LightComponent::Type::Directional;
        }
    };

    /// A directional light's shadow view and projection.
    ///
    /// A directional light's shadow frustum has to enclose everything that could
    /// cast into the view, or shadows appear and vanish as the camera moves.
    struct ShadowProjection
    {
        Mat4 View = Mat4(1.0f);
        Mat4 Projection = Mat4(1.0f);
        float TexelWorldSize = 0.0f;
        float Distance = 100.0f;

        [[nodiscard]] Mat4 GetViewProjectionMatrix() const noexcept { return Projection * View; }

        /// Projects a world position into the light's clip space, which is what a
        /// shadow lookup needs.
        [[nodiscard]] Vec3 ProjectToLightSpace(const Vec3& worldPosition) const noexcept;
    };

    /// Builds a directional light's shadow projection around a scene.
    ///
    /// `bounds` is the axis-aligned box the shadow map must cover. Snapping the
    /// centre to whole texels is what stops a shadow from crawling as the camera
    /// moves, which is otherwise the most visible artefact in a shadow map.
    [[nodiscard]] ShadowProjection BuildDirectionalShadow(const Vec3& lightDirection,
                                                         const Vec3& boundsCentre,
                                                         float boundsRadius,
                                                         std::uint32_t shadowMapSize,
                                                         float depthRange = 200.0f) noexcept;

    /// Reconstructs a world position from a screen-space position and depth.
    ///
    /// This is what screen-space effects need in order to know what a pixel is
    /// showing. A pixel at the far plane reconstructs to exactly that position,
    /// which is what makes the result checkable.
    [[nodiscard]] Vec3 ReconstructWorldPosition(const Mat4& inverseViewProjection,
                                                const Vec2& screenPosition,
                                                float depth,
                                                const Vec2& screenSize) noexcept;

    /// The screen-space radius a world-space sphere covers, in pixels.
    ///
    /// A shadow map's texel size projected to the screen is how a soft shadow's
    /// blur is scaled; it is also how a screen-space effect sizes its kernel.
    [[nodiscard]] float ProjectRadiusToScreen(float worldRadius, float distance, float screenHeight,
                                              float fieldOfViewDegrees) noexcept;

    /// The frame's camera, lights and draw list.
    ///
    /// Building the frame is separate from drawing it, so the whole selection and
    /// sorting stage can be exercised without a graphics device.
    class RenderQueue
    {
    public:
        RenderQueue() = default;

        /// Rebuilds the queue from a world.
        ///
        /// Culls everything outside the camera's frustum, skips disabled entities
        /// and entities with nothing to draw, and fills the light list from the
        /// world's light components.
        void Build(World& world, const Camera& camera);

        void Clear();

        [[nodiscard]] const Camera& GetCamera() const noexcept { return m_Camera; }
        [[nodiscard]] const std::vector<DrawItem>& GetDrawItems() const noexcept { return m_DrawItems; }
        [[nodiscard]] const std::vector<LightDrawItem>& GetPointLights() const noexcept { return m_PointLights; }
        [[nodiscard]] const std::vector<LightDrawItem>& GetDirectionalLights() const noexcept { return m_DirectionalLights; }

        /// Lights that cast shadows, with a shadow projection built for each.
        [[nodiscard]] const std::vector<ShadowProjection>& GetShadowProjections() const noexcept
        {
            return m_ShadowProjections;
        }

        /// Draw items that cast shadows.
        [[nodiscard]] const std::vector<DrawItem>& GetShadowCasters() const noexcept { return m_ShadowCasters; }

        /// How many draw items were culled, for the editor's statistics panel.
        [[nodiscard]] std::size_t GetCulledCount() const noexcept { return m_CulledCount; }

        /// The scene's bounds in world space, used to size a shadow map.
        [[nodiscard]] const Vec3& GetSceneBoundsMin() const noexcept { return m_BoundsMin; }
        [[nodiscard]] const Vec3& GetSceneBoundsMax() const noexcept { return m_BoundsMax; }

        /// Registers a mesh so the queue can look up its bounds.
        ///
        /// Without a mesh the queue can still draw the entity, but it cannot cull
        /// it or size a shadow map around it, so it is treated as un-cullable.
        void SetMeshBounds(std::uint32_t meshId, const Vec3& minimum, const Vec3& maximum);

        /// Forgets every registered mesh.
        void ClearMeshes();

    private:
        Camera m_Camera;
        std::vector<DrawItem> m_DrawItems;
        std::vector<LightDrawItem> m_PointLights;
        std::vector<LightDrawItem> m_DirectionalLights;
        std::vector<ShadowProjection> m_ShadowProjections;
        std::vector<DrawItem> m_ShadowCasters;
        std::vector<std::pair<std::uint32_t, std::array<Vec3, 2>>> m_MeshBounds;
        Vec3 m_BoundsMin = Vec3(0.0f);
        Vec3 m_BoundsMax = Vec3(0.0f);
        std::size_t m_CulledCount = 0;
    };
}