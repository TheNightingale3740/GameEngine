#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "Renderer/Mesh.h"

using namespace Ember;

namespace
{
    /// True when every index is within the mesh's vertex count.
    [[nodiscard]] bool IndicesAreInBounds(const Mesh& mesh)
    {
        return std::all_of(mesh.GetIndices().begin(), mesh.GetIndices().end(),
                           [&mesh](std::uint32_t index) { return index < mesh.GetVertices().size(); });
    }

    /// Twice the area of a triangle's edges, a scalar a normal points along.
    [[nodiscard]] Vec3 FaceNormal(const std::array<Vec3, 3>& triangle)
    {
        return glm::cross(triangle[1] - triangle[0], triangle[2] - triangle[0]);
    }

    /// True when every vertex of a face agrees on which way it points.
    [[nodiscard]] bool FaceAgreesWithNormals(const Mesh& mesh)
    {
        for (std::size_t i = 0; i + 2 < mesh.GetIndices().size(); i += 3)
        {
            const std::array<Vec3, 3>& triangle = {mesh.GetVertices()[mesh.GetIndices()[i]].Position,
                                                    mesh.GetVertices()[mesh.GetIndices()[i + 1]].Position,
                                                    mesh.GetVertices()[mesh.GetIndices()[i + 2]].Position};

            const Vec3 face = FaceNormal(triangle);
            if (glm::length2(face) < 1e-12f)
            {
                continue;
            }

            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const Vec3& normal = mesh.GetVertices()[mesh.GetIndices()[i + corner]].Normal;
                if (glm::dot(normal, face) <= 0.0f)
                {
                    return false;
                }
            }
        }

        return true;
    }

    /// True when every vertex normal and tangent is a unit vector.
    [[nodiscard]] bool NormalsAndTangentsAreUnitLength(const Mesh& mesh)
    {
        return std::all_of(mesh.GetVertices().begin(), mesh.GetVertices().end(), [](const Vertex& vertex)
        {
            return std::fabs(glm::length(vertex.Normal) - 1.0f) < 1e-3f &&
                   std::fabs(glm::length(vertex.Tangent) - 1.0f) < 1e-3f;
        });
    }
}

// -------------------------------------------------------------------- creation

TEST(MeshTest, CreateStoresItsParts)
{
    std::vector<Vertex> vertices(3);
    const std::vector<std::uint32_t> indices{0, 1, 2};

    const Result<Mesh> result = Mesh::Create("triangle", vertices, indices, {});

    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.Value().GetName(), "triangle");
    EXPECT_EQ(result.Value().GetVertices().size(), 3u);
    EXPECT_EQ(result.Value().GetIndices().size(), 3u);
    EXPECT_TRUE(result.Value().IsValid());
}

TEST(MeshTest, CreateRejectsEmptyInput)
{
    EXPECT_TRUE(Mesh::Create("empty", {}, {}, {}).IsFailure());
    EXPECT_TRUE(Mesh::Create("empty", std::vector<Vertex>(3), {}, {}).IsFailure());
    EXPECT_TRUE(Mesh::Create("empty", {}, {0, 1, 2}, {}).IsFailure());
}

TEST(MeshTest, CreateRejectsAnIndexPastTheVertices)
{
    const Result<Mesh> result = Mesh::Create("bad", std::vector<Vertex>(3), {0, 1, 99}, {});

    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::InvalidArgument);
}

TEST(MeshTest, CreateSuppliesOnePrimitiveWhenNoneAreGiven)
{
    const Result<Mesh> result = Mesh::Create("triangle", std::vector<Vertex>(3), {0, 1, 2}, {});

    ASSERT_TRUE(result.IsSuccess());
    ASSERT_EQ(result.Value().GetPrimitives().size(), 1u);
    EXPECT_EQ(result.Value().GetPrimitives()[0].IndexCount, 3u);
}

TEST(MeshTest, CreateKeepsThePrimitivesItIsGiven)
{
    std::vector<Vertex> vertices(6);
    const std::vector<std::uint32_t> indices{0, 1, 2, 3, 4, 5};

    Primitive first;
    first.FirstIndex = 0;
    first.IndexCount = 3;
    first.MaterialSlot = 1;

    Primitive second;
    second.FirstIndex = 3;
    second.IndexCount = 3;
    second.MaterialSlot = 2;

    const Result<Mesh> result = Mesh::Create("two", vertices, indices, {first, second});

    ASSERT_TRUE(result.IsSuccess());
    ASSERT_EQ(result.Value().GetPrimitives().size(), 2u);
    EXPECT_EQ(result.Value().GetPrimitives()[1].MaterialSlot, 2u);
}

TEST(MeshTest, MaterialIdRoundTrips)
{
    Result<Mesh> mesh = Mesh::Create("m", std::vector<Vertex>(3), {0, 1, 2}, {});
    ASSERT_TRUE(mesh.IsSuccess());

    mesh.Value().SetMaterialId(42);
    EXPECT_EQ(mesh.Value().GetMaterialId(), 42u);
}

// ---------------------------------------------------------------------- bounds

TEST(MeshBoundsTest, BoundsCoverEveryVertex)
{
    std::vector<Vertex> vertices(3);
    vertices[0].Position = Vec3(-1.0f, -2.0f, -3.0f);
    vertices[1].Position = Vec3(4.0f, 0.0f, 0.0f);
    vertices[2].Position = Vec3(0.0f, 5.0f, 6.0f);

    const Result<Mesh> result = Mesh::Create("bounds", vertices, {0, 1, 2}, {});
    ASSERT_TRUE(result.IsSuccess());

    EXPECT_TRUE(glm::all(glm::epsilonEqual(result.Value().GetBoundsMin(), Vec3(-1.0f, -2.0f, -3.0f), 1e-5f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(result.Value().GetBoundsMax(), Vec3(4.0f, 5.0f, 6.0f), 1e-5f)));
}

TEST(MeshBoundsTest, BoundingRadiusContainsEveryVertex)
{
    std::vector<Vertex> vertices(3);
    vertices[0].Position = Vec3(-1.0f, 0.0f, 0.0f);
    vertices[1].Position = Vec3(1.0f, 0.0f, 0.0f);
    vertices[2].Position = Vec3(0.0f, 0.0f, 0.0f);

    const Result<Mesh> result = Mesh::Create("radius", vertices, {0, 1, 2}, {});
    ASSERT_TRUE(result.IsSuccess());

    EXPECT_NEAR(result.Value().GetBoundingRadius(), 1.0f, 1e-5f);
}

TEST(MeshBoundsTest, RecomputeFollowsMovedVertices)
{
    Result<Mesh> result = MeshFactory::CreateBox("box", Vec3(1.0f));
    ASSERT_TRUE(result.IsSuccess());

    EXPECT_NEAR(result.Value().GetBoundsMax().x, 1.0f, 1e-5f);

    result.Value().Scale(2.0f);

    EXPECT_NEAR(result.Value().GetBoundsMax().x, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Value().GetBoundingRadius(), 2.0f * std::sqrt(3.0f), 1e-4f);
}

TEST(MeshScaleTest, ScaleLeavesNormalsAlone)
{
    Result<Mesh> result = MeshFactory::CreateBox("box", Vec3(1.0f));
    ASSERT_TRUE(result.IsSuccess());

    const Vec3 normalBefore = result.Value().GetVertices().front().Normal;
    result.Value().Scale(3.0f);

    // Normals are directions: a uniform scale must not shorten them.
    EXPECT_NEAR(glm::length(result.Value().GetVertices().front().Normal), 1.0f, 1e-5f);
    EXPECT_TRUE(glm::all(glm::epsilonEqual(result.Value().GetVertices().front().Normal, normalBefore, 1e-6f)));
}

// -------------------------------------------------------------------- normals

TEST(MeshNormalTest, GeneratedNormalsAreUnitLength)
{
    Result<Mesh> mesh = MeshFactory::CreateSphere("sphere", 1.0f, 8, 6);
    ASSERT_TRUE(mesh.IsSuccess());

    mesh.Value().GenerateNormals();

    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        EXPECT_NEAR(glm::length(vertex.Normal), 1.0f, 1e-4f);
    }
}

TEST(MeshNormalTest, GeneratedNormalsAgreeWithTheirFaces)
{
    Result<Mesh> mesh = MeshFactory::CreateBox("box", Vec3(1.0f));
    ASSERT_TRUE(mesh.IsSuccess());

    mesh.Value().GenerateNormals();

    EXPECT_TRUE(FaceAgreesWithNormals(mesh.Value()));
}

TEST(MeshNormalTest, SphereNormalsPointAwayFromTheCentre)
{
    Result<Mesh> mesh = MeshFactory::CreateSphere("sphere", 2.0f, 12, 8);
    ASSERT_TRUE(mesh.IsSuccess());

    mesh.Value().GenerateNormals();

    // Averaging face normals tilts a vertex's normal away from the exact radial
    // direction, and the tilt grows as the mesh gets coarser, so the bound is on
    // agreement rather than on equality.
    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        const Vec3 direction = glm::normalize(vertex.Position);
        EXPECT_GT(glm::dot(vertex.Normal, direction), 0.95f);
    }
}

TEST(MeshNormalTest, PlaneNormalsAllPointUp)
{
    Result<Mesh> mesh = MeshFactory::CreatePlane("plane", Vec2(4.0f, 4.0f), 2);
    ASSERT_TRUE(mesh.IsSuccess());

    mesh.Value().GenerateNormals();

    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        EXPECT_NEAR(vertex.Normal.y, 1.0f, 1e-4f);
    }
}

TEST(MeshNormalTest, NormalsOfACubeStayFlat)
{
    Result<Mesh> mesh = MeshFactory::CreateBox("box", Vec3(1.0f));
    ASSERT_TRUE(mesh.IsSuccess());

    // A cube has 24 vertices rather than 8 precisely so that its corners do not
    // average six faces into one rounded normal.
    EXPECT_EQ(mesh.Value().GetVertices().size(), 24u);

    mesh.Value().GenerateNormals();

    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        // Each normal is exactly one of the six axis directions, not a blend.
        const int axes = static_cast<int>(std::fabs(vertex.Normal.x) > 0.5f) +
                         static_cast<int>(std::fabs(vertex.Normal.y) > 0.5f) +
                         static_cast<int>(std::fabs(vertex.Normal.z) > 0.5f);

        EXPECT_EQ(axes, 1);
    }
}

// -------------------------------------------------------------------- tangents

TEST(MeshTangentTest, GeneratedTangentsAreUnitLength)
{
    Result<Mesh> mesh = MeshFactory::CreatePlane("plane", Vec2(2.0f, 2.0f), 2);
    ASSERT_TRUE(mesh.IsSuccess());

    mesh.Value().GenerateNormals();
    mesh.Value().GenerateTangents();

    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        EXPECT_NEAR(glm::length(vertex.Tangent), 1.0f, 1e-4f);
    }
}

TEST(MeshTangentTest, TangentsArePerpendicularToNormals)
{
    Result<Mesh> mesh = MeshFactory::CreateSphere("sphere", 1.0f, 10, 6);
    ASSERT_TRUE(mesh.IsSuccess());

    mesh.Value().GenerateNormals();
    mesh.Value().GenerateTangents();

    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        // A tangent basis that is not orthonormal makes a normal map shade wrongly.
        EXPECT_NEAR(glm::dot(vertex.Tangent, vertex.Normal), 0.0f, 1e-4f);
    }
}

TEST(MeshTangentTest, TangentsFollowTheTextureCoordinates)
{
    Result<Mesh> mesh = MeshFactory::CreatePlane("plane", Vec2(2.0f, 2.0f), 1);
    ASSERT_TRUE(mesh.IsSuccess());

    mesh.Value().GenerateNormals();
    mesh.Value().GenerateTangents();

    // The plane's texture coordinates run along X, so its tangents must too.
    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        EXPECT_GT(std::fabs(vertex.Tangent.x), 0.9f);
    }
}

TEST(MeshTangentTest, DegenerateTextureCoordinatesDoNotBreakTangents)
{
    // Every triangle shares one texture coordinate, so no tangent can be recovered.
    std::vector<Vertex> vertices(4);
    vertices[0].Position = Vec3(0.0f, 0.0f, 0.0f);
    vertices[1].Position = Vec3(1.0f, 0.0f, 0.0f);
    vertices[2].Position = Vec3(1.0f, 1.0f, 0.0f);
    vertices[3].Position = Vec3(0.0f, 1.0f, 0.0f);

    Result<Mesh> result = Mesh::Create("degenerate", vertices, {0, 1, 2, 0, 2, 3}, {});
    ASSERT_TRUE(result.IsSuccess());

    result.Value().GenerateNormals();
    result.Value().GenerateTangents();

    EXPECT_TRUE(NormalsAndTangentsAreUnitLength(result.Value()));
}

TEST(MeshTangentTest, CreateWithGeneratedNormalsDoesBoth)
{
    const Result<Mesh> empty = Mesh::CreateWithGeneratedNormals("box", std::vector<Vertex>(), {}, {});
    EXPECT_TRUE(empty.IsFailure());

    Result<Mesh> sphere = MeshFactory::CreateSphere("sphere", 1.0f, 8, 6);
    ASSERT_TRUE(sphere.IsSuccess());

    Result<Mesh> generated = Mesh::CreateWithGeneratedNormals("sphere", sphere.Value().GetVertices(),
                                                              sphere.Value().GetIndices(),
                                                              sphere.Value().GetPrimitives());
    ASSERT_TRUE(generated.IsSuccess());

    EXPECT_TRUE(NormalsAndTangentsAreUnitLength(generated.Value()));
}

// ------------------------------------------------------------------ primitives

TEST(MeshPrimitiveTest, BoxHasTwentyFourVerticesAndThirtySixIndices)
{
    Result<Mesh> mesh = MeshFactory::CreateBox("box", Vec3(1.0f));
    ASSERT_TRUE(mesh.IsSuccess());

    EXPECT_EQ(mesh.Value().GetVertices().size(), 24u);
    EXPECT_EQ(mesh.Value().GetIndices().size(), 36u);
    EXPECT_TRUE(IndicesAreInBounds(mesh.Value()));
}

TEST(MeshPrimitiveTest, BoxIsCentredOnTheOrigin)
{
    Result<Mesh> mesh = MeshFactory::CreateBox("box", Vec3(2.0f, 1.0f, 3.0f));
    ASSERT_TRUE(mesh.IsSuccess());

    const Vec3 centre = (mesh.Value().GetBoundsMin() + mesh.Value().GetBoundsMax()) * 0.5f;
    EXPECT_TRUE(glm::all(glm::epsilonEqual(centre, Vec3(0.0f), 1e-5f)));
}

TEST(MeshPrimitiveTest, BoxHonoursItsHalfExtents)
{
    Result<Mesh> mesh = MeshFactory::CreateBox("box", Vec3(2.0f, 3.0f, 4.0f));
    ASSERT_TRUE(mesh.IsSuccess());

    EXPECT_TRUE(glm::all(glm::epsilonEqual(mesh.Value().GetBoundsMin(), Vec3(-2.0f, -3.0f, -4.0f), 1e-5f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(mesh.Value().GetBoundsMax(), Vec3(2.0f, 3.0f, 4.0f), 1e-5f)));
}

TEST(MeshPrimitiveTest, BoxRejectsNonPositiveExtents)
{
    EXPECT_TRUE(MeshFactory::CreateBox("box", Vec3(0.0f)).IsFailure());
    EXPECT_TRUE(MeshFactory::CreateBox("box", Vec3(1.0f, -1.0f, 1.0f)).IsFailure());
}

TEST(MeshPrimitiveTest, BoxFacesPointOutwards)
{
    Result<Mesh> mesh = MeshFactory::CreateBox("box", Vec3(1.0f));
    ASSERT_TRUE(mesh.IsSuccess());

    EXPECT_TRUE(FaceAgreesWithNormals(mesh.Value()));
}

TEST(MeshPrimitiveTest, SphereIsClosedAroundItsRadius)
{
    Result<Mesh> mesh = MeshFactory::CreateSphere("sphere", 3.0f, 12, 8);
    ASSERT_TRUE(mesh.IsSuccess());

    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        EXPECT_NEAR(glm::length(vertex.Position), 3.0f, 1e-3f);
    }

    EXPECT_TRUE(IndicesAreInBounds(mesh.Value()));
    EXPECT_EQ(mesh.Value().GetIndices().size() % 3, 0u);
}

TEST(MeshPrimitiveTest, SphereClampsTooFewSegments)
{
    // Below three divisions there are no faces at all, so the request is clamped
    // rather than producing an empty mesh.
    const Result<Mesh> tiny = MeshFactory::CreateSphere("tiny", 1.0f, 1, 1);
    ASSERT_TRUE(tiny.IsSuccess());

    EXPECT_GT(tiny.Value().GetIndices().size(), 0u);
    EXPECT_TRUE(IndicesAreInBounds(tiny.Value()));
}

TEST(MeshPrimitiveTest, SphereRejectsANonPositiveRadius)
{
    EXPECT_TRUE(MeshFactory::CreateSphere("sphere", 0.0f, 8, 6).IsFailure());
    EXPECT_TRUE(MeshFactory::CreateSphere("sphere", -1.0f, 8, 6).IsFailure());
}

TEST(MeshPrimitiveTest, PlaneLiesInTheXZPlane)
{
    Result<Mesh> mesh = MeshFactory::CreatePlane("plane", Vec2(4.0f, 2.0f), 1);
    ASSERT_TRUE(mesh.IsSuccess());

    for (const Vertex& vertex : mesh.Value().GetVertices())
    {
        EXPECT_FLOAT_EQ(vertex.Position.y, 0.0f);
    }

    EXPECT_TRUE(glm::all(glm::epsilonEqual(mesh.Value().GetBoundsMax(), Vec3(2.0f, 0.0f, 1.0f), 1e-5f)));
    EXPECT_EQ(mesh.Value().GetIndices().size(), 6u);
}

TEST(MeshPrimitiveTest, PlaneSubdivisionsAddVerticesAndIndices)
{
    const Result<Mesh> coarse = MeshFactory::CreatePlane("plane", Vec2(2.0f, 2.0f), 1);
    const Result<Mesh> fine = MeshFactory::CreatePlane("plane", Vec2(2.0f, 2.0f), 4);

    ASSERT_TRUE(coarse.IsSuccess());
    ASSERT_TRUE(fine.IsSuccess());

    EXPECT_EQ(coarse.Value().GetVertices().size(), 4u);
    EXPECT_EQ(fine.Value().GetVertices().size(), 25u);
    EXPECT_EQ(fine.Value().GetIndices().size(), 96u);
}

TEST(MeshPrimitiveTest, PlaneRejectsANonPositiveSize)
{
    EXPECT_TRUE(MeshFactory::CreatePlane("plane", Vec2(0.0f, 1.0f), 1).IsFailure());
    EXPECT_TRUE(MeshFactory::CreatePlane("plane", Vec2(1.0f, -1.0f), 1).IsFailure());
}

TEST(MeshPrimitiveTest, CylinderIsTheRequestedHeight)
{
    Result<Mesh> mesh = MeshFactory::CreateCylinder("cylinder", 1.0f, 4.0f, 12);
    ASSERT_TRUE(mesh.IsSuccess());

    EXPECT_NEAR(mesh.Value().GetBoundsMax().y, 2.0f, 1e-5f);
    EXPECT_NEAR(mesh.Value().GetBoundsMin().y, -2.0f, 1e-5f);
    EXPECT_NEAR(mesh.Value().GetBoundsMax().x, 1.0f, 1e-4f);
    EXPECT_TRUE(IndicesAreInBounds(mesh.Value()));
}

TEST(MeshPrimitiveTest, ConeSitsOnTheOrigin)
{
    Result<Mesh> mesh = MeshFactory::CreateCone("cone", 1.0f, 2.0f, 12);
    ASSERT_TRUE(mesh.IsSuccess());

    EXPECT_NEAR(mesh.Value().GetBoundsMin().y, -1.0f, 1e-5f);
    EXPECT_NEAR(mesh.Value().GetBoundsMax().y, 1.0f, 1e-5f);
    EXPECT_TRUE(IndicesAreInBounds(mesh.Value()));
}

TEST(MeshPrimitiveTest, CapsuleSpansItsCylinderAndCaps)
{
    Result<Mesh> mesh = MeshFactory::CreateCapsule("capsule", 1.0f, 2.0f, 12, 4);
    ASSERT_TRUE(mesh.IsSuccess());

    EXPECT_NEAR(mesh.Value().GetBoundsMax().y, 3.0f, 1e-4f);
    EXPECT_NEAR(mesh.Value().GetBoundsMin().y, -3.0f, 1e-4f);
    EXPECT_NEAR(mesh.Value().GetBoundsMax().x, 1.0f, 1e-3f);
    EXPECT_TRUE(IndicesAreInBounds(mesh.Value()));
}

TEST(MeshPrimitiveTest, PrimitivesRejectImpossibleParameters)
{
    EXPECT_TRUE(MeshFactory::CreateCylinder("cylinder", 0.0f, 1.0f, 8).IsFailure());
    EXPECT_TRUE(MeshFactory::CreateCone("cone", 1.0f, 0.0f, 8).IsFailure());
    EXPECT_TRUE(MeshFactory::CreateCapsule("capsule", -1.0f, 1.0f, 8, 4).IsFailure());
}

TEST(MeshPrimitiveTest, EveryPrimitiveHasWholeTriangles)
{
    // A partial triangle would read past the end of a vertex on the GPU.
    const Result<Mesh> meshes[]{
        MeshFactory::CreateBox("box", Vec3(1.0f)),
        MeshFactory::CreateSphere("sphere", 1.0f, 8, 6),
        MeshFactory::CreatePlane("plane", Vec2(1.0f, 1.0f), 3),
        MeshFactory::CreateCylinder("cylinder", 1.0f, 2.0f, 10),
        MeshFactory::CreateCone("cone", 1.0f, 2.0f, 10),
        MeshFactory::CreateCapsule("capsule", 1.0f, 1.0f, 10, 4),
    };

    for (const Result<Mesh>& mesh : meshes)
    {
        ASSERT_TRUE(mesh.IsSuccess());
        EXPECT_EQ(mesh.Value().GetIndices().size() % 3, 0u) << mesh.Value().GetName();
        EXPECT_TRUE(IndicesAreInBounds(mesh.Value())) << mesh.Value().GetName();
    }
}

TEST(MeshPrimitiveTest, EveryPrimitiveHasFacesFacingOutwards)
{
    const Result<Mesh> meshes[]{
        MeshFactory::CreateBox("box", Vec3(1.0f)),
        MeshFactory::CreateSphere("sphere", 1.0f, 10, 8),
        MeshFactory::CreateCylinder("cylinder", 1.0f, 2.0f, 10),
        MeshFactory::CreateCone("cone", 1.0f, 2.0f, 10),
        MeshFactory::CreateCapsule("capsule", 1.0f, 1.0f, 10, 4),
    };

    for (const Result<Mesh>& mesh : meshes)
    {
        ASSERT_TRUE(mesh.IsSuccess());
        EXPECT_TRUE(FaceAgreesWithNormals(mesh.Value())) << mesh.Value().GetName();
    }
}

// --------------------------------------------------------------- vertex layout

TEST(VertexLayoutTest, LayoutDescribesEveryVertexField)
{
    const std::vector<VertexLayout::Attribute>& layout = VertexLayout::Get();

    ASSERT_EQ(layout.size(), 4u);
    EXPECT_EQ(layout[0].Name, "Position");
    EXPECT_EQ(layout[1].Name, "Normal");
    EXPECT_EQ(layout[2].Name, "Tangent");
    EXPECT_EQ(layout[3].Name, "TexCoord");
}

TEST(VertexLayoutTest, LayoutOffsetsMatchTheVertexStruct)
{
    for (const VertexLayout::Attribute& attribute : VertexLayout::Get())
    {
        std::size_t expected = 0;

        if (attribute.Name == "Position")
        {
            expected = offsetof(Vertex, Position);
        }
        else if (attribute.Name == "Normal")
        {
            expected = offsetof(Vertex, Normal);
        }
        else if (attribute.Name == "Tangent")
        {
            expected = offsetof(Vertex, Tangent);
        }
        else
        {
            expected = offsetof(Vertex, TexCoord);
        }

        EXPECT_EQ(attribute.Offset, static_cast<std::uint32_t>(expected)) << attribute.Name;
    }
}

TEST(VertexLayoutTest, StrideMatchesTheVertexSize)
{
    EXPECT_EQ(VertexLayout::GetStride(), sizeof(Vertex));
}

TEST(VertexLayoutTest, ArraySizesMatchTheFieldTypes)
{
    const std::vector<VertexLayout::Attribute>& layout = VertexLayout::Get();

    EXPECT_EQ(layout[0].Type, PropertyType::Vec3);
    EXPECT_EQ(layout[3].Type, PropertyType::Vec2);
    EXPECT_EQ(layout[3].ArraySize, 2u);
}