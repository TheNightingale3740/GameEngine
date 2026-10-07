#include <gtest/gtest.h>

#include <cmath>

#include "Renderer/Material.h"

using namespace Ember;

namespace
{
    /// Sample points along a curve, used to check monotonicity without testing
    /// every value.
    [[nodiscard]] std::vector<float> Sample(float from, float to, int steps)
    {
        std::vector<float> values;

        for (int i = 0; i <= steps; ++i)
        {
            values.push_back(from + (to - from) * static_cast<float>(i) / static_cast<float>(steps));
        }

        return values;
    }

    /// True when `f` never decreases as `x` rises.
    [[nodiscard]] bool IsMonotonic(const std::vector<float>& samples, float (*f)(float) noexcept)
    {
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            if (f(samples[i]) < f(samples[i - 1]))
            {
                return false;
            }
        }

        return true;
    }
}

// ---------------------------------------------------------------------- BRDF

TEST(PbrTest, ReflectanceRoundTripsThroughTheIndexOfRefraction)
{
    const float reflectance = 0.04f;

    const float index = Pbr::ReflectanceToRefractiveIndex(reflectance);
    const float recovered = ((index - 1.0f) * (index - 1.0f)) / ((index + 1.0f) * (index + 1.0f));

    EXPECT_NEAR(recovered, reflectance, 1e-4f);
}

TEST(PbrTest, AStandardIndexOfRefractionGivesTheStandardReflectance)
{
    const float index = Pbr::ReflectanceToRefractiveIndex(0.04f);

    // 0.04 is what water and most solids reflect, and it comes from an index
    // around 1.5.
    EXPECT_NEAR(index, 1.5f, 0.01f);
}

TEST(PbrTest, FresnelRisesTowardsGrazingAngles)
{
    // Looking straight at a surface reflects only its base reflectance; looking
    // along it reflects everything. That is why a surface looks white at the
    // edges.
    EXPECT_NEAR(Pbr::FresnelSchlick(1.0f, 0.04f), 0.04f, 1e-4f);
    EXPECT_NEAR(Pbr::FresnelSchlick(0.0f, 0.04f), 1.0f, 1e-4f);
    EXPECT_GT(Pbr::FresnelSchlick(0.5f, 0.04f), Pbr::FresnelSchlick(0.9f, 0.04f));
}

TEST(PbrTest, FresnelNeverExceedsOne)
{
    for (const float cosTheta : Sample(0.0f, 1.0f, 16))
    {
        for (const float reflectance : Sample(0.0f, 1.0f, 4))
        {
            EXPECT_LE(Pbr::FresnelSchlick(cosTheta, reflectance), 1.0f);
            EXPECT_GE(Pbr::FresnelSchlick(cosTheta, reflectance), 0.0f);
        }
    }
}

TEST(PbrTest, DistributionPeaksWhereTheMicrofacetsLineUp)
{
    const float roughness = 0.4f;

    // Facing the light square on, every microfacet agrees, so the distribution is
    // at its peak and nowhere else.
    EXPECT_GT(Pbr::DistributionGGX(1.0f, roughness), Pbr::DistributionGGX(0.9f, roughness));
    EXPECT_GT(Pbr::DistributionGGX(0.9f, roughness), Pbr::DistributionGGX(0.5f, roughness));
    EXPECT_GT(Pbr::DistributionGGX(0.5f, roughness), Pbr::DistributionGGX(0.1f, roughness));
}

TEST(PbrTest, ASmootherSurfaceConcentratesItsHighlight)
{
    // A mirror has all its energy in one direction; a rough surface spreads it.
    EXPECT_GT(Pbr::DistributionGGX(1.0f, 0.1f), Pbr::DistributionGGX(1.0f, 0.5f));
    EXPECT_GT(Pbr::DistributionGGX(1.0f, 0.5f), Pbr::DistributionGGX(1.0f, 0.9f));
}

TEST(PbrTest, GeometryFallsOffAtGrazingAngles)
{
    const float roughness = 0.5f;

    // Looking along a surface, most of it is hidden behind its own roughness, so
    // less of its light reaches the viewer.
    EXPECT_GT(Pbr::GeometrySchlickGGX(0.9f, roughness), Pbr::GeometrySchlickGGX(0.2f, roughness));
}

TEST(PbrTest, VisibilityStaysBoundedAtGrazingAngles)
{
    // The correlated visibility term already divides out the cosine and the
    // light's own falloff, so it is a weighting rather than an occlusion and
    // does not collapse the way the raw geometry term does. What matters is that
    // it stays finite and positive, since the BRDF divides by it.
    for (const float view : Sample(0.0f, 1.0f, 16))
    {
        for (const float light : Sample(0.0f, 1.0f, 8))
        {
            const float visibility = Pbr::GeometrySmithGGXCorrelated(view, light, 0.3f);
            EXPECT_GT(visibility, 0.0f);
            EXPECT_LT(visibility, 100.0f);
        }
    }
}

TEST(PbrTest, GeometryStaysWithinReason)
{
    for (const float view : Sample(0.05f, 1.0f, 8))
    {
        for (const float light : Sample(0.05f, 1.0f, 4))
        {
            const float geometry = Pbr::GeometrySmithGGXCorrelated(view, light, 0.4f);
            EXPECT_GT(geometry, 0.0f);
            EXPECT_LT(geometry, 10.0f);
        }
    }
}

TEST(PbrTest, SchlickGeometryIsTheUncorrelatedApproximation)
{
    // The Schlick form of Smith's term, kept because it is the cheaper of the two
    // and the tests pin its behaviour.
    EXPECT_GT(Pbr::GeometrySchlickGGX(1.0f, 0.4f), Pbr::GeometrySchlickGGX(0.1f, 0.4f));
    EXPECT_NEAR(Pbr::GeometrySchlickGGX(1.0f, 1.0f), 1.0f, 1e-4f);
}

TEST(PbrTest, SpecularIsZeroBehindTheSurface)
{
    const Vec3 normal(0.0f, 1.0f, 0.0f);

    // A light below a surface contributes nothing to it.
    EXPECT_TRUE(glm::all(glm::epsilonEqual(Pbr::SpecularBRDF(normal, Vec3(0.0f, 1.0f, 1.0f),
                                                            Vec3(0.0f, -1.0f, 0.0f), 0.5f, 0.04f),
                                           Vec3(0.0f), 1e-6f)));
}

TEST(PbrTest, SpecularPeaksAtTheMirrorDirection)
{
    const Vec3 normal(0.0f, 1.0f, 0.0f);
    const Vec3 view = glm::normalize(Vec3(0.0f, 1.0f, 1.0f));

    // Straight down at the light is the mirror direction; well off to one side is
    // not, and reflects far less.
    const Vec3 aligned = Pbr::SpecularBRDF(normal, view, view, 0.3f, 0.04f);
    const Vec3 off = Pbr::SpecularBRDF(normal, view, glm::normalize(Vec3(1.0f, 0.2f, 0.1f)), 0.3f, 0.04f);

    EXPECT_GT(aligned.x, off.x);
}

TEST(PbrTest, DiffuseScalesWithTheBaseColour)
{
    const Vec3 dark = Pbr::DiffuseBRDF(Vec3(0.2f), 0.0f);
    const Vec3 light = Pbr::DiffuseBRDF(Vec3(0.8f), 0.0f);

    EXPECT_NEAR(light.x / dark.x, 4.0f, 1e-4f);
}

TEST(PbrTest, DiffuseIsDividedByPi)
{
    // A Lambert surface returns a twentieth of the light that hits it, whatever
    // its colour; the division by pi is what makes that true.
    EXPECT_NEAR(Pbr::DiffuseBRDF(Vec3(1.0f), 0.0f).x, 1.0f / Pi, 1e-5f);
}

TEST(PbrTest, AMetalHasNoDiffuseLobe)
{
    // Metal's reflection is tinted by its base colour rather than absorbed, so a
    // Lambert term on top would count the same light twice.
    EXPECT_TRUE(glm::all(glm::epsilonEqual(Pbr::DiffuseBRDF(Vec3(1.0f), 1.0f), Vec3(0.0f), 1e-6f)));
    EXPECT_GT(Pbr::DiffuseBRDF(Vec3(1.0f), 0.5f).x, 0.0f);
}

TEST(PbrTest, DiffuseNeverReturnsMoreThanItTook)
{
    for (const float amount : Sample(0.0f, 1.0f, 8))
    {
        EXPECT_LE(Pbr::DiffuseBRDF(Vec3(amount), 0.0f).x, amount + 1e-6f);
    }
}

TEST(PbrTest, DielectricFractionShrinksWithMetallicity)
{
    EXPECT_TRUE(glm::all(glm::epsilonEqual(Pbr::DielectricFraction(Vec3(1.0f), 1.0f), Vec3(0.0f), 1e-6f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(Pbr::DielectricFraction(Vec3(1.0f), 0.0f), Vec3(1.0f), 1e-6f)));
}

TEST(PbrTest, FresnelBlendRisesTowardsGrazingAngles)
{
    const Vec3 base(1.0f, 0.5f, 0.25f);
    const Vec3 reflectance(0.04f);

    const Vec3 headOn = Pbr::FresnelBlend(reflectance, base, 1.0f);
    const Vec3 grazing = Pbr::FresnelBlend(reflectance, base, 0.0f);

    EXPECT_GT(grazing.x, headOn.x);

    // Every channel rises by the same factor, so the tint is unchanged by angle.
    EXPECT_NEAR(grazing.y / headOn.y, grazing.x / headOn.x, 1e-4f);
}

TEST(PbrTest, FresnelBlendKeepsTheBaseColoursRatio)
{
    const Vec3 base(0.8f, 0.4f, 0.2f);
    const Vec3 blended = Pbr::FresnelBlend(Vec3(0.04f), base, 0.5f);

    // A metal's reflection is always tinted by its base colour, so the ratio
    // between the channels survives the Fresnel term.
    EXPECT_NEAR(blended.x / blended.z, base.x / base.z, 1e-4f);
}

TEST(PbrTest, FresnelBlendOfAGreyColourIsGrey)
{
    const Vec3 blended = Pbr::FresnelBlend(Vec3(0.04f), Vec3(0.8f), 0.2f);

    EXPECT_NEAR(blended.x, blended.y, 1e-5f);
    EXPECT_NEAR(blended.y, blended.z, 1e-5f);
}

// --------------------------------------------------------------- tone mapping

TEST(ToneMappingTest, ReinhardMapsZeroToZeroAndSaturatesNearOne)
{
    EXPECT_FLOAT_EQ(ToneMapping::Reinhard(0.0f), 0.0f);
    EXPECT_GT(ToneMapping::Reinhard(1.0f), 0.4f);
    EXPECT_LT(ToneMapping::Reinhard(1.0f), 0.6f);
    EXPECT_NEAR(ToneMapping::Reinhard(1000.0f), 1.0f, 1e-3f);
}

TEST(ToneMappingTest, ReinhardIsMonotonic)
{
    EXPECT_TRUE(IsMonotonic(Sample(0.0f, 100.0f, 64), ToneMapping::Reinhard));
}

TEST(ToneMappingTest, ExtendedReinhardReachesWhiteAtItsWhitePoint)
{
    EXPECT_NEAR(ToneMapping::ReinhardExtended(4.0f, 4.0f), 1.0f, 1e-4f);
    EXPECT_GT(ToneMapping::ReinhardExtended(4.0f, 4.0f), ToneMapping::Reinhard(4.0f));
}

TEST(ToneMappingTest, AcesFilmicKeepsMidTonesBrighterThanReinhard)
{
    // Reinhard washes out a mid-grey; the filmic curve is contrastier there, which
    // is why it is the default.
    EXPECT_GT(ToneMapping::AcesFilmic(0.18f), ToneMapping::Reinhard(0.18f));
}

TEST(ToneMappingTest, AcesFilmicIsMonotonicAndBounded)
{
    EXPECT_TRUE(IsMonotonic(Sample(0.0f, 50.0f, 64), ToneMapping::AcesFilmic));

    for (const float value : Sample(0.0f, 100.0f, 32))
    {
        EXPECT_GE(ToneMapping::AcesFilmic(value), 0.0f);
        EXPECT_LE(ToneMapping::AcesFilmic(value), 1.0f);
    }
}

TEST(ToneMappingTest, SrgbTransferFunctionHasItsEndpoints)
{
    EXPECT_FLOAT_EQ(ToneMapping::LinearToSrgb(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(ToneMapping::LinearToSrgb(1.0f), 1.0f);
    EXPECT_NEAR(ToneMapping::LinearToSrgb(0.5f), 0.735f, 1e-2f);
}

TEST(ToneMappingTest, SrgbTransferFunctionRoundTrips)
{
    for (const float value : Sample(0.0f, 1.0f, 32))
    {
        EXPECT_NEAR(ToneMapping::SrgbToLinear(ToneMapping::LinearToSrgb(value)), value, 1e-4f);
    }
}

TEST(ToneMappingTest, DisplayConversionAppliesExposure)
{
    const float dark = ToneMapping::ToDisplay(0.1f, 1.0f);
    const float bright = ToneMapping::ToDisplay(0.1f, 4.0f);

    EXPECT_GT(bright, dark);
    EXPECT_LE(bright, 1.0f);
    EXPECT_GE(dark, 0.0f);
}

TEST(ToneMappingTest, DisplayConversionNeverLeavesTheDisplayRange)
{
    for (const float value : Sample(0.0f, 1000.0f, 32))
    {
        for (const float exposure : Sample(0.1f, 8.0f, 4))
        {
            const float display = ToneMapping::ToDisplay(value, exposure);
            EXPECT_GE(display, 0.0f);
            EXPECT_LE(display, 1.0f);
        }
    }
}

// ------------------------------------------------------------------ materials

TEST(MaterialTest, DefaultMaterialIsADielectric)
{
    const Material material;

    EXPECT_NEAR(material.GetReflectance(), 0.04f, 1e-2f);
    EXPECT_NEAR(material.GetShadingReflectance().x, 0.04f, 1e-2f);
}

TEST(MaterialTest, AMetalReflectsWithItsBaseColour)
{
    Material material;
    material.BaseColor = Vec3(0.9f, 0.6f, 0.1f);
    material.Metallic = 1.0f;

    EXPECT_TRUE(glm::all(glm::epsilonEqual(material.GetShadingReflectance(), material.BaseColor, 1e-5f)));
}

TEST(MaterialTest, PartlyMetallicKeepsADielectricReflectance)
{
    Material material;
    material.Metallic = 0.5f;

    // Half-metal is still mostly dielectric, so its reflectance is not its colour.
    EXPECT_LT(material.GetShadingReflectance().x, material.BaseColor.x);
    EXPECT_GT(material.GetShadingReflectance().x, 0.0f);
}

TEST(MaterialTest, ReflectanceFollowsTheRefractiveIndex)
{
    Material material;

    material.RefractiveIndex = 1.0f;
    const float airLike = material.GetReflectance();

    material.RefractiveIndex = 2.4f;
    const float dense = material.GetReflectance();

    EXPECT_NEAR(airLike, 0.0f, 1e-5f);
    EXPECT_GT(dense, airLike);
}

TEST(MaterialLibraryTest, MaterialsResolveById)
{
    MaterialLibrary library;

    Material material;
    material.Name = "Brick";
    const std::uint32_t id = library.Add(material);

    ASSERT_NE(id, 0u);
    ASSERT_NE(library.Find(id), nullptr);
    EXPECT_EQ(library.Find(id)->Name, "Brick");
    EXPECT_TRUE(library.Find(id)->IsValid());
}

TEST(MaterialLibraryTest, UnknownIdsResolveToNothing)
{
    MaterialLibrary library;

    EXPECT_EQ(library.Find(0), nullptr);
    EXPECT_EQ(library.Find(1), nullptr);
    EXPECT_EQ(library.Find(9999), nullptr);
}

TEST(MaterialLibraryTest, RemovedIdsDoNotShiftTheOnesAfterThem)
{
    MaterialLibrary library;

    Material first;
    first.Name = "First";
    const std::uint32_t firstId = library.Add(first);

    Material second;
    second.Name = "Second";
    const std::uint32_t secondId = library.Add(second);

    ASSERT_TRUE(library.Remove(firstId));

    // A scene that referenced material 2 must keep meaning the same material.
    EXPECT_EQ(library.Find(firstId), nullptr);
    ASSERT_NE(library.Find(secondId), nullptr);
    EXPECT_EQ(library.Find(secondId)->Name, "Second");
}

TEST(MaterialLibraryTest, RemovingTwiceReportsFailure)
{
    MaterialLibrary library;

    const std::uint32_t id = library.Add(Material{});

    EXPECT_TRUE(library.Remove(id));
    EXPECT_FALSE(library.Remove(id));
}

TEST(MaterialLibraryTest, IdsAreNeverReused)
{
    MaterialLibrary library;

    const std::uint32_t first = library.Add(Material{});
    library.Remove(first);
    const std::uint32_t second = library.Add(Material{});

    EXPECT_NE(second, first);
    EXPECT_EQ(library.Count(), 2u);
}

TEST(MaterialLibraryTest, ClearEmptiesEverything)
{
    MaterialLibrary library;
    library.Add(Material{});
    library.Add(Material{});

    library.Clear();

    EXPECT_EQ(library.Count(), 0u);
    EXPECT_EQ(library.Find(1), nullptr);
}