// Renderer/Renderer.cpp

#include "Renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Core/Logging/Log.h"

namespace Ember
{
    namespace
    {
        /// Normalises a plane in place, so that `Distance` really is a distance.
        ///
        /// A plane extracted from a matrix is a row of it, and that row is only
        /// unit length when the matrix is a projection. Without this the containment
        /// tests compare distances measured in different units.
        Plane NormalisedPlane(const Vec4& row) noexcept
        {
            Plane plane;
            plane.Normal = Vec3(row.x, row.y, row.z);

            const float length = glm::length(plane.Normal);
            if (length > 1e-6f)
            {
                plane.Normal /= length;
            }

            plane.Distance = row.w / length;
            return plane;
        }

    }

    Mat4 Camera::GetViewMatrix() const noexcept
    {
        return Math::LookAt(Position, Target, Up);
    }

    Mat4 Camera::GetProjectionMatrix() const noexcept
    {
        return Orthographic ? Math::Orthographic(-OrthographicSize * AspectRatio,
                                                 OrthographicSize * AspectRatio,
                                                 -OrthographicSize,
                                                 OrthographicSize,
                                                 NearPlane,
                                                 FarPlane)
                            : Math::Perspective(FieldOfViewDegrees, AspectRatio, NearPlane, FarPlane);
    }

    Mat4 Camera::GetViewProjectionMatrix() const noexcept
    {
        return GetProjectionMatrix() * GetViewMatrix();
    }

    std::array<Plane, 6> Camera::GetFrustumPlanes() const noexcept
    {
        // Gribb and Hartmann: each frustum plane is a sum or difference of rows of
        // the combined matrix. glm stores columns, so row j of the matrix is spread
        // across the j-th component of every column.
        const Mat4 m = GetViewProjectionMatrix();

        const auto row = [&m](int index) noexcept
        {
            return Vec4(m[0][index], m[1][index], m[2][index], m[3][index]);
        };

        const Vec4 row0 = row(0);
        const Vec4 row1 = row(1);
        const Vec4 row2 = row(2);
        const Vec4 row3 = row(3);

        // Depth is [0, 1] in Vulkan-style clip space, so the near plane is row 2 on
        // its own and the far plane is row 3 minus it. Assuming [-1, 1] depth here
        // would put the near plane halfway into the frustum.
        return {
            NormalisedPlane(row3 + row0),
            NormalisedPlane(row3 - row0),
            NormalisedPlane(row3 + row1),
            NormalisedPlane(row3 - row1),
            NormalisedPlane(row2),
            NormalisedPlane(row3 - row2),
        };
    }

    bool IsInside(const std::array<Plane, 6>& frustum, const Vec3& centre, float radius) noexcept
    {
        // A sphere is inside while its centre is inside every plane or close
        // enough to one that the sphere reaches across it.
        for (const Plane& plane : frustum)
        {
            if (plane.SignedDistance(centre) < -radius)
            {
                return false;
            }
        }

        return true;
    }

    bool IsInside(const std::array<Plane, 6>& frustum, const Vec3& minimum, const Vec3& maximum) noexcept
    {
        const Vec3 centre = (minimum + maximum) * 0.5f;
        return IsInside(frustum, centre, glm::length(maximum - minimum) * 0.5f);
    }

    Vec3 ShadowProjection::ProjectToLightSpace(const Vec3& worldPosition) const noexcept
    {
        const Vec4 clip = GetViewProjectionMatrix() * Vec4(worldPosition, 1.0f);

        if (std::fabs(clip.w) < 1e-6f)
        {
            return Vec3(0.0f);
        }

        const Vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);

        // Clip space is [-1, 1] on x and y and [0, 1] on z for a Vulkan-style
        // projection, so x and y are remapped here and z is already in range.
        return Vec3(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f, ndc.z);
    }

    ShadowProjection BuildDirectionalShadow(const Vec3& lightDirection,
                                           const Vec3& boundsCentre,
                                           float boundsRadius,
                                           std::uint32_t shadowMapSize,
                                           float depthRange) noexcept
    {
        ShadowProjection shadow;

        const std::uint32_t resolution = std::max<std::uint32_t>(shadowMapSize, 1);

        // The light looks along its direction, from far enough away that the scene
        // is in front of it.
        const Vec3 direction = glm::length(lightDirection) > 0.0f ? glm::normalize(lightDirection)
                                                                   : Vec3(0.0f, -1.0f, 0.0f);
        const Vec3 eye = boundsCentre - direction * (depthRange * 0.5f);

        // Snapping the centre to whole texels is what stops the shadow edges from
        // crawling as the camera moves: without it, a sub-texel shift in the
        // projection changes where every shadow lands.
        const float texelWorldSize = (boundsRadius * 2.0f) / static_cast<float>(resolution);
        const Vec3 snappedCentre = texelWorldSize > 0.0f
                                       ? Vec3(std::round(boundsCentre.x / texelWorldSize) * texelWorldSize,
                                              std::round(boundsCentre.y / texelWorldSize) * texelWorldSize,
                                              std::round(boundsCentre.z / texelWorldSize) * texelWorldSize)
                                       : boundsCentre;

        shadow.View = Math::LookAt(eye, snappedCentre, std::fabs(direction.y) > 0.99f ? Vec3(0.0f, 0.0f, 1.0f)
                                                                                      : Vec3(0.0f, 1.0f, 0.0f));
        shadow.Projection = Math::Orthographic(-boundsRadius, boundsRadius, -boundsRadius, boundsRadius,
                                               0.01f, depthRange);
        shadow.TexelWorldSize = texelWorldSize;
        shadow.Distance = depthRange;

        return shadow;
    }

    Vec3 ReconstructWorldPosition(const Mat4& inverseViewProjection,
                                  const Vec2& screenPosition,
                                  float depth,
                                  const Vec2& screenSize) noexcept
    {
        // Screen position to normalised device coordinates, with y flipped because
        // screen space grows downwards and clip space grows upwards.
        const Vec2 ndc = Vec2((screenPosition.x / screenSize.x) * 2.0f - 1.0f,
                              1.0f - (screenPosition.y / screenSize.y) * 2.0f);

        const float clamped = Math::Clamp(depth, 0.0f, 1.0f);

        // The two points are unprojected and interpolated in clip space, not in
        // world space. Clip depth is not linear in distance, so interpolating world
        // positions with the pixel's own depth puts it in the wrong place by a
        // margin that grows with distance.
        const Vec4 atDepth = inverseViewProjection * Vec4(ndc.x, ndc.y, clamped, 1.0f);
        const Vec4 atFar = inverseViewProjection * Vec4(ndc.x, ndc.y, 1.0f, 1.0f);

        if (std::fabs(atDepth.w) < 1e-6f || std::fabs(atFar.w) < 1e-6f)
        {
            return Vec3(0.0f);
        }

        const Vec3 nearPosition(atDepth.x / atDepth.w, atDepth.y / atDepth.w, atDepth.z / atDepth.w);
        const Vec3 farPosition(atFar.x / atFar.w, atFar.y / atFar.w, atFar.z / atFar.w);

        return nearPosition + (farPosition - nearPosition) * clamped;
    }

    float ProjectRadiusToScreen(float worldRadius, float distance, float screenHeight,
                                float fieldOfViewDegrees) noexcept
    {
        if (distance <= 0.0f || screenHeight <= 0.0f)
        {
            return 0.0f;
        }

        // The projection of a radius scales as its distance over the tangent of
        // the field of view, which is the same relation the projection matrix uses.
        const float tangent = std::tan(fieldOfViewDegrees * DegreesToRadians * 0.5f);
        return worldRadius * screenHeight / (2.0f * distance * tangent);
    }

    void RenderQueue::SetMeshBounds(std::uint32_t meshId, const Vec3& minimum, const Vec3& maximum)
    {
        for (auto& entry : m_MeshBounds)
        {
            if (entry.first == meshId)
            {
                entry.second = {minimum, maximum};
                return;
            }
        }

        m_MeshBounds.emplace_back(meshId, std::array<Vec3, 2>{minimum, maximum});
    }

    void RenderQueue::ClearMeshes()
    {
        m_MeshBounds.clear();
    }

    void RenderQueue::Clear()
    {
        m_DrawItems.clear();
        m_PointLights.clear();
        m_DirectionalLights.clear();
        m_ShadowProjections.clear();
        m_ShadowCasters.clear();
        m_BoundsMin = Vec3(0.0f);
        m_BoundsMax = Vec3(0.0f);
        m_CulledCount = 0;
    }

    void RenderQueue::Build(World& world, const Camera& camera)
    {
        Clear();

        m_Camera = camera;
        const std::array<Plane, 6> frustum = camera.GetFrustumPlanes();

        constexpr float largest = std::numeric_limits<float>::max();
        bool hasBounds = false;
        Vec3 boundsMin(largest, largest, largest);
        Vec3 boundsMax(-largest, -largest, -largest);

        // Candidates are collected before culling because culling needs the mesh's
        // bounds and looking those up while iterating would invalidate the
        // iteration.
        std::vector<DrawItem> candidates;

        world.Each<MeshComponent>([&](const MeshComponent& mesh, Entity entity)
        {
            if (!mesh.Visible || !world.IsActiveInHierarchy(entity))
            {
                return;
            }

            const TransformComponent* transform = world.TryGetComponent<TransformComponent>(entity);
            DrawItem item;
            item.Entity = entity;
            item.MeshId = mesh.MeshId;
            item.MaterialId = mesh.MaterialId;
            item.Transform = transform != nullptr ? transform->ToMatrix() : Mat4(1.0f);
            item.DoubleSided = mesh.DoubleSided;
            item.Cull = mesh.DoubleSided ? CullMode::None : CullMode::Back;
            item.FirstIndex = mesh.FirstIndex;

            // A count of zero means the whole mesh, which is what the component's
            // default says and what an authoring tool leaves behind.
            item.IndexCount = mesh.IndexCount;

            candidates.push_back(item);
        });

        for (DrawItem& item : candidates)
        {
            Vec3 centre(item.Transform[3][0], item.Transform[3][1], item.Transform[3][2]);
            float radius = 0.0f;
            bool knownBounds = false;

            for (const auto& [meshId, bounds] : m_MeshBounds)
            {
                if (meshId != item.MeshId)
                {
                    continue;
                }

                // The mesh's local bounds are transformed by the eight corners,
                // which is exact for a rotation and scale, and conservative for a
                // skewed matrix.
                Vec3 worldMin(largest, largest, largest);
                Vec3 worldMax(-largest, -largest, -largest);

                for (int corner = 0; corner < 8; ++corner)
                {
                    const Vec3 local((corner & 1) ? bounds[1].x : bounds[0].x,
                                     (corner & 2) ? bounds[1].y : bounds[0].y,
                                     (corner & 4) ? bounds[1].z : bounds[0].z);

                    const Vec4 transformed = item.Transform * Vec4(local, 1.0f);
                    const Vec3 worldCorner(transformed.x, transformed.y, transformed.z);
                    worldMin = glm::min(worldMin, worldCorner);
                    worldMax = glm::max(worldMax, worldCorner);
                }

                centre = (worldMin + worldMax) * 0.5f;
                radius = glm::length(worldMax - worldMin) * 0.5f;
                knownBounds = true;
                break;
            }

            if (knownBounds && !IsInside(frustum, centre, radius))
            {
                ++m_CulledCount;
                continue;
            }

            item.Depth = glm::distance(centre, camera.Position);
            m_DrawItems.push_back(item);

            // Everything the camera can see can also block light, so the shadow
            // pass draws the same set. A per-mesh opt-out would need a component
            // field for it, and a mesh that casts no shadow is rare enough that
            // the usual reason to want one is a transparent surface, which is not
            // in this list anyway.
            m_ShadowCasters.push_back(item);

            if (knownBounds)
            {
                boundsMin = glm::min(boundsMin, centre - Vec3(radius));
                boundsMax = glm::max(boundsMax, centre + Vec3(radius));
                hasBounds = true;
            }
        }

        m_BoundsMin = hasBounds ? boundsMin : Vec3(0.0f);
        m_BoundsMax = hasBounds ? boundsMax : Vec3(0.0f);

        // Opaque geometry is sorted front to back so that the depth test rejects
        // as much shading as possible before it happens.
        std::stable_sort(m_DrawItems.begin(), m_DrawItems.end(),
                         [](const DrawItem& a, const DrawItem& b) { return a.Depth < b.Depth; });

        world.Each<LightComponent>([&](const LightComponent& light, Entity entity)
        {
            if (!world.IsActiveInHierarchy(entity))
            {
                return;
            }

            const TransformComponent* transform = world.TryGetComponent<TransformComponent>(entity);

            LightDrawItem item;
            item.Entity = entity;
            item.Position = transform != nullptr ? transform->Position : Vec3(0.0f);
            item.Color = light.Color;
            item.Intensity = light.Intensity;
            item.Range = light.Range;
            item.InnerAngleDegrees = light.InnerAngleDegrees;
            item.ShadowResolution = light.ShadowResolution;
            item.CastsShadows = light.CastsShadows;

            switch (light.LightType)
            {
                case LightComponent::Type::Directional:
                    // A directional light has no position; only its direction
                    // matters, and it is the entity's forward axis.
                    item.Direction = transform != nullptr
                                         ? glm::vec3(transform->ToMatrix() * Vec4(0.0f, 0.0f, -1.0f, 0.0f))
                                         : Vec3(0.0f, -1.0f, 0.0f);
                    item.Direction = glm::length(item.Direction) > 0.0f ? glm::normalize(item.Direction)
                                                                       : Vec3(0.0f, -1.0f, 0.0f);
                    m_DirectionalLights.push_back(item);
                    break;

                case LightComponent::Type::Point:
                case LightComponent::Type::Spot:
                    m_PointLights.push_back(item);
                    break;
            }
        });

        for (const LightDrawItem& light : m_DirectionalLights)
        {
            if (!light.CastsShadows)
            {
                continue;
            }

            const Vec3 centre = (m_BoundsMin + m_BoundsMax) * 0.5f;
            const float radius = glm::length(m_BoundsMax - m_BoundsMin) * 0.5f;

            m_ShadowProjections.push_back(
                BuildDirectionalShadow(light.Direction, centre, std::max(radius, 0.1f), light.ShadowResolution));
        }
    }
}