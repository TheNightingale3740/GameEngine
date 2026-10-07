// Renderer/Mesh.h
//
// CPU-side geometry.
//
// A mesh is a set of positions, normals, tangents, texture coordinates and
// indices, plus a material reference. It exists independently of any graphics
// device so that geometry can be built, imported and tested without a GPU, and
// uploaded to the device separately when something is rendered with it.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Math/Math.h"
#include "Core/Result.h"
#include "Ecs/Reflection.h"

namespace Ember
{
    /// One vertex of a mesh.
    ///
    /// The layout is fixed and shared with the shader that consumes it, so the
    /// two must agree: `VertexPosition` and friends describe it once, and both
    /// the vertex buffer layout and the reflection below are derived from it.
    struct Vertex
    {
        Vec3 Position = Vec3(0.0f);
        Vec3 Normal = Vec3(0.0f, 1.0f, 0.0f);
        Vec3 Tangent = Vec3(1.0f, 0.0f, 0.0f);
        Vec2 TexCoord = Vec2(0.0f);
    };

    /// A contiguous range of one mesh's index buffer, drawn as one draw call.
    struct Primitive
    {
        /// First index to draw.
        std::uint32_t FirstIndex = 0;

        /// Indices to draw.
        std::uint32_t IndexCount = 0;

        /// Base vertex, so primitives can share one vertex buffer.
        std::int32_t BaseVertex = 0;

        /// Which of the mesh's materials this primitive uses.
        std::uint32_t MaterialSlot = 0;
    };

    /// Geometry ready to be uploaded or drawn.
    class Mesh
    {
    public:
        Mesh() = default;

        /// Builds a mesh from its parts.
        ///
        /// Fails with InvalidArgument for an empty mesh, or for an index that
        /// refers to a vertex that does not exist: a mesh that draws out of bounds
        /// is a crash on the GPU, and far harder to trace than a load failure.
        static Result<Mesh> Create(std::string name,
                                  std::vector<Vertex> vertices,
                                  std::vector<std::uint32_t> indices,
                                  std::vector<Primitive> primitives);

        /// Returns the mesh with normals and tangents computed from its faces.
        ///
        /// Tangents are needed by any material that uses a normal map, and are
        /// otherwise meaningless, so they are computed on request rather than at
        /// import: a mesh with no normal map does not pay for them.
        static Result<Mesh> CreateWithGeneratedNormals(std::string name,
                                                       std::vector<Vertex> vertices,
                                                       std::vector<std::uint32_t> indices,
                                                       std::vector<Primitive> primitives);

        [[nodiscard]] const std::string& GetName() const noexcept { return m_Name; }
        void SetName(std::string name) { m_Name = std::move(name); }

        [[nodiscard]] const std::vector<Vertex>& GetVertices() const noexcept { return m_Vertices; }
        [[nodiscard]] const std::vector<std::uint32_t>& GetIndices() const noexcept { return m_Indices; }
        [[nodiscard]] const std::vector<Primitive>& GetPrimitives() const noexcept { return m_Primitives; }
        void SetPrimitives(std::vector<Primitive> primitives) { m_Primitives = std::move(primitives); }

        /// Asset id of the material, or 0 when the mesh has none.
        [[nodiscard]] std::uint32_t GetMaterialId() const noexcept { return m_MaterialId; }
        void SetMaterialId(std::uint32_t materialId) noexcept { m_MaterialId = materialId; }

        [[nodiscard]] bool IsValid() const noexcept { return m_Valid; }

        /// Bounding box in local space, computed from the vertices.
        [[nodiscard]] const Vec3& GetBoundsMin() const noexcept { return m_BoundsMin; }

        /// Opposite corner of the bounding box in local space.
        [[nodiscard]] const Vec3& GetBoundsMax() const noexcept { return m_BoundsMax; }

        /// Radius of a sphere containing the mesh, from its centre.
        [[nodiscard]] float GetBoundingRadius() const noexcept { return m_BoundingRadius; }

        /// Recomputes the bounds from the current vertices.
        void RecomputeBounds();

        /// Builds one primitive covering the whole index buffer.
        [[nodiscard]] static Primitive MakeSinglePrimitive(std::uint32_t indexCount);

        // ------------------------------------------------------------------ tangents

        /// Recomputes vertex normals from the mesh's faces.
        ///
        /// A vertex's normal is the normalised sum of the face normals around it,
        /// which is what makes a sphere smooth and a cube flat.
        void GenerateNormals();

        /// Recomputes tangents from the mesh's faces and texture coordinates.
        ///
        /// Uses the standard accumulated tangent construction, orthonormalised
        /// against the normal so that the basis a shader builds from them is
        /// actually orthogonal.
        void GenerateTangents();

        /// Scales every position and the normals with it, for a uniform size change.
        void Scale(float factor);

    private:
        std::string m_Name;
        std::vector<Vertex> m_Vertices;
        std::vector<std::uint32_t> m_Indices;
        std::vector<Primitive> m_Primitives;
        Vec3 m_BoundsMin = Vec3(0.0f);
        Vec3 m_BoundsMax = Vec3(0.0f);
        float m_BoundingRadius = 0.0f;
        std::uint32_t m_MaterialId = 0;
        bool m_Valid = false;
    };

    namespace MeshFactory
    {
        /// Builds a box centred on the origin with the given half-extents.
        [[nodiscard]] Result<Mesh> CreateBox(const std::string& name, const Vec3& halfExtents);

        /// Builds a UV sphere centred on the origin.
        ///
        /// `segments` is the number of horizontal divisions and `rings` the number
        /// of vertical ones. Both are clamped to at least three, below which a
        /// sphere has no faces to draw.
        [[nodiscard]] Result<Mesh> CreateSphere(const std::string& name, float radius, int segments, int rings);

        /// Builds a capsule along the Y axis: a cylinder capped with hemispheres.
        [[nodiscard]] Result<Mesh> CreateCapsule(const std::string& name, float radius, float halfHeight, int segments, int rings);

        /// Builds a plane on the XZ plane centred on the origin.
        [[nodiscard]] Result<Mesh> CreatePlane(const std::string& name, const Vec2& size, int subdivisions);

        /// Builds a cone along the Y axis, standing on the XZ plane.
        [[nodiscard]] Result<Mesh> CreateCone(const std::string& name, float radius, float height, int segments);

        /// Builds a cylinder along the Y axis, centred on the origin.
        [[nodiscard]] Result<Mesh> CreateCylinder(const std::string& name, float radius, float height, int segments);
    }

    /// The vertex layout both the vertex buffer and the shaders agree on.
    namespace VertexLayout
    {
        /// One attribute of the layout.
        struct Attribute
        {
            std::string Name;
            PropertyType Type = PropertyType::Float;
            std::uint32_t Offset = 0;
            std::uint32_t ArraySize = 1;
        };

        /// The layout as reflection, so the serialiser and editor can read it.
        [[nodiscard]] const std::vector<Attribute>& Get();

        /// Bytes one vertex occupies.
        [[nodiscard]] std::size_t GetStride();
    }
}