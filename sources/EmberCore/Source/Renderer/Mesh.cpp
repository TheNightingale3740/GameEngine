// Renderer/Mesh.cpp

#include "Renderer/Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "Core/Logging/Log.h"

namespace Ember
{
    Result<Mesh> Mesh::Create(std::string name,
                              std::vector<Vertex> vertices,
                              std::vector<std::uint32_t> indices,
                              std::vector<Primitive> primitives)
    {
        if (vertices.empty())
        {
            return Error(ErrorCode::InvalidArgument, "A mesh needs vertices");
        }

        if (indices.empty())
        {
            return Error(ErrorCode::InvalidArgument, "A mesh needs indices");
        }

        for (const std::uint32_t index : indices)
        {
            if (index >= vertices.size())
            {
                return Error(ErrorCode::InvalidArgument,
                             "Index " + std::to_string(index) + " refers to a vertex that does not exist");
            }
        }

        if (primitives.empty())
        {
            primitives.push_back(MakeSinglePrimitive(static_cast<std::uint32_t>(indices.size())));
        }

        Mesh mesh;
        mesh.m_Name = std::move(name);
        mesh.m_Vertices = std::move(vertices);
        mesh.m_Indices = std::move(indices);
        mesh.m_Primitives = std::move(primitives);
        mesh.m_Valid = true;
        mesh.RecomputeBounds();

        return mesh;
    }

    Result<Mesh> Mesh::CreateWithGeneratedNormals(std::string name,
                                                 std::vector<Vertex> vertices,
                                                 std::vector<std::uint32_t> indices,
                                                 std::vector<Primitive> primitives)
    {
        Result<Mesh> mesh = Create(std::move(name), std::move(vertices), std::move(indices), std::move(primitives));
        if (mesh.IsFailure())
        {
            return mesh;
        }

        mesh.Value().GenerateNormals();
        mesh.Value().GenerateTangents();
        mesh.Value().RecomputeBounds();

        return mesh;
    }

    Primitive Mesh::MakeSinglePrimitive(std::uint32_t indexCount)
    {
        Primitive primitive;
        primitive.FirstIndex = 0;
        primitive.IndexCount = indexCount;
        primitive.BaseVertex = 0;
        primitive.MaterialSlot = 0;
        return primitive;
    }

    void Mesh::RecomputeBounds()
    {
        if (m_Vertices.empty())
        {
            m_BoundsMin = Vec3(0.0f);
            m_BoundsMax = Vec3(0.0f);
            m_BoundingRadius = 0.0f;
            return;
        }

        constexpr float largest = std::numeric_limits<float>::max();

        m_BoundsMin = Vec3(largest, largest, largest);
        m_BoundsMax = Vec3(-largest, -largest, -largest);

        for (const Vertex& vertex : m_Vertices)
        {
            m_BoundsMin = glm::min(m_BoundsMin, vertex.Position);
            m_BoundsMax = glm::max(m_BoundsMax, vertex.Position);
        }

        const Vec3 centre = (m_BoundsMin + m_BoundsMax) * 0.5f;

        float radiusSquared = 0.0f;
        for (const Vertex& vertex : m_Vertices)
        {
            radiusSquared = std::max(radiusSquared, glm::length2(vertex.Position - centre));
        }

        m_BoundingRadius = std::sqrt(radiusSquared);
    }

    void Mesh::GenerateNormals()
    {
        for (Vertex& vertex : m_Vertices)
        {
            vertex.Normal = Vec3(0.0f);
        }

        // Each triangle's normal is added to all three of its vertices, so a vertex
        // shared between flat and smooth parts ends up smooth and a vertex used by
        // one triangle keeps that triangle's normal exactly.
        for (std::size_t i = 0; i + 2 < m_Indices.size(); i += 3)
        {
            const std::uint32_t a = m_Indices[i];
            const std::uint32_t b = m_Indices[i + 1];
            const std::uint32_t c = m_Indices[i + 2];

            const Vec3& pa = m_Vertices[a].Position;
            const Vec3& pb = m_Vertices[b].Position;
            const Vec3& pc = m_Vertices[c].Position;

            const Vec3 faceNormal = glm::cross(pb - pa, pc - pa);

            m_Vertices[a].Normal += faceNormal;
            m_Vertices[b].Normal += faceNormal;
            m_Vertices[c].Normal += faceNormal;
        }

        for (Vertex& vertex : m_Vertices)
        {
            // A vertex no triangle reaches has no direction; up is as good a guess
            // as any and keeps the shader from reading a zero vector.
            const float lengthSquared = glm::length2(vertex.Normal);
            vertex.Normal = lengthSquared > 0.0f ? vertex.Normal / std::sqrt(lengthSquared) : Vec3(0.0f, 1.0f, 0.0f);
        }
    }

    void Mesh::GenerateTangents()
    {
        for (Vertex& vertex : m_Vertices)
        {
            vertex.Tangent = Vec3(0.0f);
        }

        std::vector<Vec3> accumulated(m_Vertices.size(), Vec3(0.0f));

        for (std::size_t i = 0; i + 2 < m_Indices.size(); i += 3)
        {
            const std::uint32_t a = m_Indices[i];
            const std::uint32_t b = m_Indices[i + 1];
            const std::uint32_t c = m_Indices[i + 2];

            const Vec3& pa = m_Vertices[a].Position;
            const Vec3& pb = m_Vertices[b].Position;
            const Vec3& pc = m_Vertices[c].Position;

            const Vec2& ta = m_Vertices[a].TexCoord;
            const Vec2& tb = m_Vertices[b].TexCoord;
            const Vec2& tc = m_Vertices[c].TexCoord;

            const Vec3 edge1 = pb - pa;
            const Vec3 edge2 = pc - pa;

            const Vec2 deltaUv1 = tb - ta;
            const Vec2 deltaUv2 = tc - ta;

            const float determinant = deltaUv1.x * deltaUv2.y - deltaUv2.x * deltaUv1.y;

            // A triangle whose texture coordinates are collinear says nothing about
            // which way the tangent points, so it contributes nothing rather than
            // a tangent built from a divide by zero.
            if (std::fabs(determinant) < 1e-12f)
            {
                continue;
            }

            const float inverse = 1.0f / determinant;

            // The standard construction: solve the 2x2 system for the tangent, then
            // weight it by the triangle's texture area so that larger triangles
            // pull harder.
            const Vec3 tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * inverse;

            accumulated[a] += tangent;
            accumulated[b] += tangent;
            accumulated[c] += tangent;
        }

        for (std::size_t i = 0; i < m_Vertices.size(); ++i)
        {
            Vertex& vertex = m_Vertices[i];

            // Gram-Schmidt against the normal, so the tangent is perpendicular to
            // it and the basis a shader builds from the two is orthonormal.
            const Vec3 ortho = vertex.Normal * glm::dot(vertex.Normal, accumulated[i]);
            const Vec3 tangent = accumulated[i] - ortho;

            const float lengthSquared = glm::length2(tangent);
            vertex.Tangent = lengthSquared > 0.0f ? tangent / std::sqrt(lengthSquared) : Vec3(1.0f, 0.0f, 0.0f);
        }
    }

    void Mesh::Scale(float factor)
    {
        for (Vertex& vertex : m_Vertices)
        {
            vertex.Position *= factor;
        }

        // Normals are directions, not lengths: a uniform scale leaves them alone.
        RecomputeBounds();
    }

    namespace MeshFactory
    {
        namespace
        {
            /// A capsule's height at a given ring: two hemispherical caps around a
            /// cylinder of the requested half-height.
            [[nodiscard]] float m_CapsuleHeight(int ring, int capRingCount, int totalRings,
                                                float halfHeight, float radius) noexcept
            {
                if (ring <= capRingCount)
                {
                    const float t = static_cast<float>(ring) / static_cast<float>(capRingCount);
                    return halfHeight + std::cos(t * HalfPi) * radius;
                }

                if (ring >= totalRings - capRingCount)
                {
                    const float t = static_cast<float>(ring - (totalRings - capRingCount)) /
                                    static_cast<float>(capRingCount);
                    return -halfHeight - std::sin(t * HalfPi) * radius;
                }

                const float bandCount = static_cast<float>(totalRings - 2 * capRingCount - 1);
                const float t = static_cast<float>(ring - capRingCount - 1) / bandCount;
                return halfHeight - t * 2.0f * halfHeight;
            }

            /// The radial part of a capsule ring's normal: 1 on the cylinder and
            /// tapering towards 0 at the poles.
            [[nodiscard]] float m_CapsuleRadialScale(int ring, int capRingCount, int totalRings) noexcept
            {
                if (ring <= capRingCount)
                {
                    return std::sin(static_cast<float>(ring) / static_cast<float>(capRingCount) * HalfPi);
                }

                if (ring >= totalRings - capRingCount)
                {
                    const float t = static_cast<float>(ring - (totalRings - capRingCount)) /
                                    static_cast<float>(capRingCount);
                    return std::cos(t * HalfPi);
                }

                return 1.0f;
            }

            /// The axial part of a capsule ring's normal.
            [[nodiscard]] float m_CapsuleAxialScale(int ring, int capRingCount, int totalRings) noexcept
            {
                if (ring <= capRingCount)
                {
                    return std::cos(static_cast<float>(ring) / static_cast<float>(capRingCount) * HalfPi);
                }

                if (ring >= totalRings - capRingCount)
                {
                    const float t = static_cast<float>(ring - (totalRings - capRingCount)) /
                                    static_cast<float>(capRingCount);
                    return -std::sin(t * HalfPi);
                }

                return 0.0f;
            }
        }

        Result<Mesh> CreateBox(const std::string& name, const Vec3& halfExtents)
        {
            if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f)
            {
                return {ErrorCode::InvalidArgument, "A box needs positive half-extents"};
            }

            // One face per direction, each with its own four vertices so that the
            // normals stay flat instead of being averaged across the corners.
            const Vec3 normals[]{
                Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f),
                Vec3(1.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f),
                Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
            };

            // Tangent for each face, chosen perpendicular to its normal so that the
            // tangent basis stays orthonormal on every face.
            const Vec3 tangents[]{
                Vec3(1.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f),
                Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 1.0f),
                Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f),
            };

            const float extents[]{
                halfExtents.x, halfExtents.y, halfExtents.z,
                halfExtents.x, halfExtents.y, halfExtents.z,
                halfExtents.x, halfExtents.y, halfExtents.z,
            };

            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;
            vertices.reserve(24);

            for (int face = 0; face < 6; ++face)
            {
                const Vec3 normal = normals[face];
                const Vec3 tangent = tangents[face];
                const Vec3 bitangent = glm::cross(normal, tangent);

                // The face's extent along each of its two in-plane axes.
                const float u = std::fabs(tangent.x) * extents[0] + std::fabs(tangent.y) * extents[1] +
                                std::fabs(tangent.z) * extents[2];
                const float v = std::fabs(bitangent.x) * extents[0] + std::fabs(bitangent.y) * extents[1] +
                                std::fabs(bitangent.z) * extents[2];

                const float offsetU = normal.x * extents[0] + normal.y * extents[1] + normal.z * extents[2];
                const Vec3 centre = normal * offsetU;

                const auto base = static_cast<std::uint32_t>(vertices.size());

                const Vec2 uvs[]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
                const Vec2 signs[]{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

                for (int corner = 0; corner < 4; ++corner)
                {
                    Vertex vertex;
                    vertex.Position = centre + tangent * (signs[corner].x * u) + bitangent * (signs[corner].y * v);
                    vertex.Normal = normal;
                    vertex.Tangent = tangent;
                    vertex.TexCoord = uvs[corner];
                    vertices.push_back(vertex);
                }

                indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }

            return Mesh::Create(name, std::move(vertices), std::move(indices), {});
        }

        Result<Mesh> CreatePlane(const std::string& name, const Vec2& size, int subdivisions)
    {
            if (size.x <= 0.0f || size.y <= 0.0f)
            {
                return {ErrorCode::InvalidArgument, "A plane needs a positive size"};
            }

            const int cells = std::max(1, subdivisions);
            const std::uint32_t verticesPerSide = static_cast<std::uint32_t>(cells + 1);

            std::vector<Vertex> vertices;
            vertices.reserve(static_cast<std::size_t>(verticesPerSide) * verticesPerSide);

            for (std::uint32_t row = 0; row <= static_cast<std::uint32_t>(cells); ++row)
            {
                for (std::uint32_t column = 0; column <= static_cast<std::uint32_t>(cells); ++column)
                {
                    const float u = static_cast<float>(column) / static_cast<float>(cells);
                    const float v = static_cast<float>(row) / static_cast<float>(cells);

                    Vertex vertex;
                    vertex.Position = Vec3((u - 0.5f) * size.x, 0.0f, (v - 0.5f) * size.y);
                    vertex.Normal = Vec3(0.0f, 1.0f, 0.0f);
                    vertex.Tangent = Vec3(1.0f, 0.0f, 0.0f);
                    vertex.TexCoord = Vec2(u, v);
                    vertices.push_back(vertex);
                }
            }

            std::vector<std::uint32_t> indices;
            indices.reserve(static_cast<std::size_t>(cells) * cells * 6);

            for (std::uint32_t row = 0; row < static_cast<std::uint32_t>(cells); ++row)
            {
                for (std::uint32_t column = 0; column < static_cast<std::uint32_t>(cells); ++column)
                {
                    const std::uint32_t topLeft = row * verticesPerSide + column;
                    const std::uint32_t topRight = topLeft + 1;
                    const std::uint32_t bottomLeft = topLeft + verticesPerSide;
                    const std::uint32_t bottomRight = bottomLeft + 1;

                    indices.insert(indices.end(), {topLeft, bottomLeft, bottomRight, topLeft, bottomRight, topRight});
                }
            }

            return Mesh::Create(name, std::move(vertices), std::move(indices), {});
        }

        Result<Mesh> CreateSphere(const std::string& name, float radius, int segments, int rings)
        {
            if (radius <= 0.0f)
            {
                return {ErrorCode::InvalidArgument, "A sphere needs a positive radius"};
            }

            // Below three divisions in either direction there are no faces at all,
            // so the request is clamped rather than rejected.
            const int segmentCount = std::max(3, segments);
            const int ringCount = std::max(2, rings);

            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;

            vertices.push_back(Vertex{Vec3(0.0f, radius, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec2(0.5f, 1.0f)});

            for (int ring = 1; ring < ringCount; ++ring)
            {
                const float v = static_cast<float>(ring) / static_cast<float>(ringCount);
                const float phi = v * Pi;

                for (int segment = 0; segment <= segmentCount; ++segment)
                {
                    const float u = static_cast<float>(segment) / static_cast<float>(segmentCount);
                    const float theta = u * TwoPi;

                    const Vec3 normal(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));

                    Vertex vertex;
                    vertex.Position = normal * radius;
                    vertex.Normal = normal;

                    // The tangent runs along increasing longitude, which is the same
                    // direction the texture coordinates advance in.
                    vertex.Tangent = glm::normalize(Vec3(-std::sin(theta), 0.0f, std::cos(theta)));
                    vertex.TexCoord = Vec2(u, v);
                    vertices.push_back(vertex);
                }
            }

            const auto bottomIndex = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(Vertex{Vec3(0.0f, -radius, 0.0f), Vec3(0.0f, -1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec2(0.5f, 0.0f)});

            const auto ringStride = static_cast<std::uint32_t>(segmentCount + 1);

            for (int segment = 0; segment < segmentCount; ++segment)
            {
                indices.insert(indices.end(), {0, 1 + static_cast<std::uint32_t>(segment) + 1,
                                               1 + static_cast<std::uint32_t>(segment)});
            }

            for (int ring = 0; ring + 1 < ringCount - 1; ++ring)
            {
                for (int segment = 0; segment < segmentCount; ++segment)
                {
                    const std::uint32_t current = 1 + static_cast<std::uint32_t>(ring) * ringStride +
                                                  static_cast<std::uint32_t>(segment);
                    const std::uint32_t next = current + ringStride;

                    indices.insert(indices.end(), {current, next + 1, next, current, current + 1, next + 1});
                }
            }

            const std::uint32_t lastRingStart = 1 + static_cast<std::uint32_t>(ringCount - 2) * ringStride;
            for (int segment = 0; segment < segmentCount; ++segment)
            {
                indices.insert(indices.end(),
                               {bottomIndex, lastRingStart + static_cast<std::uint32_t>(segment),
                                lastRingStart + static_cast<std::uint32_t>(segment) + 1});
            }

            return Mesh::Create(name, std::move(vertices), std::move(indices), {});
        }

        Result<Mesh> CreateCapsule(const std::string& name, float radius, float halfHeight, int segments, int rings)
        {
            if (radius <= 0.0f || halfHeight <= 0.0f)
            {
                return {ErrorCode::InvalidArgument, "A capsule needs a positive radius and half-height"};
            }

            const int segmentCount = std::max(3, segments);
            const int capRings = std::max(1, rings);

            // The cylinder's height plus a hemisphere at each end is the whole
            // capsule, and each ring's height follows from its angle.
            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;

            const int capRingCount = capRings;
            const int totalRings = capRingCount * 2 + 1;

            for (int ring = 0; ring <= totalRings; ++ring)
            {
                // Each ring's normal and height follow from the same angle: the
                // caps' normals lean away from the axis while the cylinder's are
                // purely radial, and deriving both from one angle is what keeps the
                // shading continuous where they meet.
                const float y = m_CapsuleHeight(ring, capRingCount, totalRings, halfHeight, radius);
                const float radialScale = m_CapsuleRadialScale(ring, capRingCount, totalRings);

                for (int segment = 0; segment <= segmentCount; ++segment)
                {
                    const float u = static_cast<float>(segment) / static_cast<float>(segmentCount);
                    const float theta = u * TwoPi;

                    const Vec3 normal(glm::normalize(Vec3(std::cos(theta) * radialScale,
                                                          m_CapsuleAxialScale(ring, capRingCount, totalRings),
                                                          std::sin(theta) * radialScale)));

                    Vertex vertex;
                    vertex.Position = Vec3(normal.x * radius, y, normal.z * radius);
                    vertex.Normal = normal;
                    vertex.Tangent = glm::normalize(Vec3(-std::sin(theta), 0.0f, std::cos(theta)));
                    vertex.TexCoord = Vec2(u, static_cast<float>(ring) / static_cast<float>(totalRings));
                    vertices.push_back(vertex);
                }
            }

            const auto ringStride = static_cast<std::uint32_t>(segmentCount + 1);

            for (int ring = 0; ring < totalRings; ++ring)
            {
                for (int segment = 0; segment < segmentCount; ++segment)
                {
                    const std::uint32_t current = static_cast<std::uint32_t>(ring) * ringStride +
                                                  static_cast<std::uint32_t>(segment);
                    const std::uint32_t next = current + ringStride;

                    indices.insert(indices.end(), {current, next + 1, next, current, current + 1, next + 1});
                }
            }

            return Mesh::Create(name, std::move(vertices), std::move(indices), {});
        }

        Result<Mesh> CreateCone(const std::string& name, float radius, float height, int segments)
        {
            if (radius <= 0.0f || height <= 0.0f)
            {
                return {ErrorCode::InvalidArgument, "A cone needs a positive radius and height"};
            }

            const int segmentCount = std::max(3, segments);
            const float halfHeight = height * 0.5f;

            // The side's normal leans away from the cone's surface, which for a cone
            // of this shape is a constant angle from the horizontal.
            const float slant = std::sqrt(radius * radius + height * height);
            const Vec3 sideNormal = glm::normalize(Vec3(height / slant, radius / slant, 0.0f));

            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;

            const auto apex = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(Vertex{Vec3(0.0f, halfHeight, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec2(0.5f, 1.0f)});

            for (int segment = 0; segment <= segmentCount; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segmentCount);
                const float theta = u * TwoPi;
                const Vec3 outward(std::cos(theta), 0.0f, std::sin(theta));

                Vertex vertex;
                vertex.Position = outward * radius + Vec3(0.0f, -halfHeight, 0.0f);
                vertex.Normal = Vec3(sideNormal.x * outward.x, sideNormal.y, sideNormal.x * outward.z);
                vertex.Tangent = glm::normalize(Vec3(-std::sin(theta), 0.0f, std::cos(theta)));
                vertex.TexCoord = Vec2(u, 0.0f);
                vertices.push_back(vertex);
            }

            // The base gets its own vertices: sharing the side's ring would give a
            // base face normal of -Y meeting a vertex normal that leans outwards.
            const auto baseCentre = static_cast<std::uint32_t>(vertices.size());
            vertices.push_back(Vertex{Vec3(0.0f, -halfHeight, 0.0f), Vec3(0.0f, -1.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec2(0.5f, 0.5f)});

            const auto baseRing = static_cast<std::uint32_t>(vertices.size());
            for (int segment = 0; segment <= segmentCount; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segmentCount);
                const float theta = u * TwoPi;

                vertices.push_back(Vertex{Vec3(std::cos(theta) * radius, -halfHeight, std::sin(theta) * radius),
                                           Vec3(0.0f, -1.0f, 0.0f),
                                           Vec3(-std::sin(theta), 0.0f, std::cos(theta)),
                                           Vec2(u, 1.0f)});
            }

            for (int segment = 0; segment < segmentCount; ++segment)
            {
                const std::uint32_t side = 1 + static_cast<std::uint32_t>(segment);
                const std::uint32_t base = baseRing + static_cast<std::uint32_t>(segment);

                indices.insert(indices.end(), {apex, side + 1, side});
                indices.insert(indices.end(), {baseCentre, base, base + 1});
            }

            return Mesh::Create(name, std::move(vertices), std::move(indices), {});
        }

        Result<Mesh> CreateCylinder(const std::string& name, float radius, float height, int segments)
        {
            if (radius <= 0.0f || height <= 0.0f)
            {
                return {ErrorCode::InvalidArgument, "A cylinder needs a positive radius and height"};
            }

            const int segmentCount = std::max(3, segments);
            const float halfHeight = height * 0.5f;

            std::vector<Vertex> vertices;
            std::vector<std::uint32_t> indices;

            for (int end = 0; end < 2; ++end)
            {
                const float y = end == 0 ? halfHeight : -halfHeight;

                for (int segment = 0; segment <= segmentCount; ++segment)
                {
                    const float u = static_cast<float>(segment) / static_cast<float>(segmentCount);
                    const float theta = u * TwoPi;
                    const Vec3 outward(std::cos(theta), 0.0f, std::sin(theta));

                    Vertex vertex;
                    vertex.Position = outward * radius + Vec3(0.0f, y, 0.0f);
                    vertex.Normal = outward;
                    vertex.Tangent = glm::normalize(Vec3(-std::sin(theta), 0.0f, std::cos(theta)));
                    vertex.TexCoord = Vec2(u, end == 0 ? 0.0f : 1.0f);
                    vertices.push_back(vertex);
                }
            }

            const auto ringStride = static_cast<std::uint32_t>(segmentCount + 1);

            for (int segment = 0; segment < segmentCount; ++segment)
            {
                indices.insert(indices.end(), {static_cast<std::uint32_t>(segment),
                                               static_cast<std::uint32_t>(segment) + 1,
                                               static_cast<std::uint32_t>(segment) + 1 + ringStride,
                                               static_cast<std::uint32_t>(segment),
                                               static_cast<std::uint32_t>(segment) + 1 + ringStride,
                                               static_cast<std::uint32_t>(segment) + ringStride});
            }

            // The caps get their own vertices. Sharing the side's ring would mean a
            // cap face normal of +/-Y meeting a vertex normal that is radial, and
            // the shading across the rim would break.
            const auto addCap = [&vertices, radius, segmentCount](float y, float normalY)
            {
                const auto centre = static_cast<std::uint32_t>(vertices.size());
                vertices.push_back(Vertex{Vec3(0.0f, y, 0.0f), Vec3(0.0f, normalY, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec2(0.5f, 0.5f)});

                for (int segment = 0; segment <= segmentCount; ++segment)
                {
                    const float u = static_cast<float>(segment) / static_cast<float>(segmentCount);
                    const float theta = u * TwoPi;

                    vertices.push_back(Vertex{Vec3(std::cos(theta) * radius, y, std::sin(theta) * radius),
                                               Vec3(0.0f, normalY, 0.0f),
                                               Vec3(-std::sin(theta), 0.0f, std::cos(theta)),
                                               Vec2(u, 0.5f)});
                }

                return centre;
            };

            const std::uint32_t topCentre = addCap(halfHeight, 1.0f);
            const std::uint32_t bottomCentre = addCap(-halfHeight, -1.0f);

            for (int segment = 0; segment < segmentCount; ++segment)
            {
                const std::uint32_t top = topCentre + 1 + static_cast<std::uint32_t>(segment);
                const std::uint32_t bottom = bottomCentre + 1 + static_cast<std::uint32_t>(segment);

                indices.insert(indices.end(), {topCentre, top + 1, top});
                indices.insert(indices.end(), {bottomCentre, bottom, bottom + 1});
            }

            return Mesh::Create(name, std::move(vertices), std::move(indices), {});
        }
    }

    namespace VertexLayout
    {
        namespace
        {
            /// Built once from `Vertex`, so the buffer layout and the struct can
            /// never disagree about where a field lives.
            std::vector<Attribute> BuildLayout()
            {
                std::vector<Attribute> layout;

                const auto add = [&layout](std::string name, PropertyType type, std::uint32_t offset, std::uint32_t arraySize)
                {
                    layout.push_back(Attribute{std::move(name), type, offset, arraySize});
                };

                add("Position", PropertyType::Vec3,
                    static_cast<std::uint32_t>(offsetof(Vertex, Position)), 3);
                add("Normal", PropertyType::Vec3,
                    static_cast<std::uint32_t>(offsetof(Vertex, Normal)), 3);
                add("Tangent", PropertyType::Vec3,
                    static_cast<std::uint32_t>(offsetof(Vertex, Tangent)), 3);
                add("TexCoord", PropertyType::Vec2,
                    static_cast<std::uint32_t>(offsetof(Vertex, TexCoord)), 2);

                return layout;
            }
        }

        const std::vector<Attribute>& Get()
        {
            static const std::vector<Attribute> layout = BuildLayout();
            return layout;
        }

        std::size_t GetStride()
        {
            return sizeof(Vertex);
        }
    }
}