#include <gtest/gtest.h>

#include "Core/Math/Math.h"

using namespace Ember;

TEST(MathTest, NearlyEqualDetectsCloseValues)
{
    EXPECT_TRUE(Math::NearlyEqual(1.0f, 1.0f));
    EXPECT_TRUE(Math::NearlyEqual(1.0f, 1.0f + 1e-6f));
    EXPECT_TRUE(Math::NearlyEqual(1.0f, 1.0f - 1e-6f));
    EXPECT_TRUE(Math::NearlyEqual(-2.5f, -2.5f));
    EXPECT_FALSE(Math::NearlyEqual(1.0f, 1.1f));

    // A symmetric difference just inside the tolerance must compare equal.
    EXPECT_TRUE(Math::NearlyEqual(0.0f, 0.9e-5f));
    EXPECT_FALSE(Math::NearlyEqual(0.0f, 1.1e-5f));
}

TEST(MathTest, NearlyEqualRespectsTolerance)
{
    EXPECT_FALSE(Math::NearlyEqual(0.0f, 0.5f, 0.1f));
    EXPECT_TRUE(Math::NearlyEqual(0.0f, 0.05f, 0.1f));
}

TEST(MathTest, LerpInterpolates)
{
    EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, 1.0f), 10.0f);
    EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, 0.25f), 2.5f);
}

TEST(MathTest, LerpExtrapolatesOutsideUnitRange)
{
    EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, 2.0f), 20.0f);
    EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, -1.0f), -10.0f);
}

TEST(MathTest, ClampBoundsValue)
{
    EXPECT_FLOAT_EQ(Math::Clamp(5.0f, 0.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(Math::Clamp(-5.0f, 0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(Math::Clamp(0.5f, 0.0f, 1.0f), 0.5f);
}

TEST(MathTest, DegreeRadianConversionRoundTrips)
{
    EXPECT_FLOAT_EQ(ToRadians(180.0f), Pi);
    EXPECT_FLOAT_EQ(ToDegrees(Pi), 180.0f);
    EXPECT_FLOAT_EQ(ToDegrees(ToRadians(37.0f)), 37.0f);
}

TEST(MathTest, ClampLengthLeavesShortVectorsAlone)
{
    const Vec3 vector(1.0f, 0.0f, 0.0f);
    const Vec3 result = Math::ClampLength(vector, 5.0f);

    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
}

TEST(MathTest, ClampLengthTruncatesLongVectors)
{
    const Vec3 vector(0.0f, 3.0f, 4.0f);
    const Vec3 result = Math::ClampLength(vector, 2.5f);

    EXPECT_FLOAT_EQ(glm::length(result), 2.5f);
}

TEST(MathTest, ClampLengthHandlesZeroVector)
{
    const Vec3 result = Math::ClampLength(Vec3(0.0f), 1.0f);

    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
}

TEST(MathTest, ComposeTransformRoundTripsThroughDecompose)
{
    const Vec3 translation(1.0f, -2.0f, 3.0f);
    const Quat rotation = glm::normalize(Quat(glm::angleAxis(ToRadians(35.0f), Vec3(0.0f, 1.0f, 0.0f))));
    const Vec3 scale(2.0f, 2.0f, 2.0f);

    const Mat4 transform = Math::ComposeTransform(translation, rotation, scale);

    Vec3 outTranslation;
    Quat outRotation;
    Vec3 outScale;
    Math::DecomposeTransform(transform, outTranslation, outRotation, outScale);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(outTranslation, translation, 1e-4f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(outScale, scale, 1e-4f)));

    // q and -q are the same rotation, so compare via their absolute dot product.
    EXPECT_NEAR(std::abs(glm::dot(outRotation, rotation)), 1.0f, 1e-4f);
}

TEST(MathTest, DecomposeTransformOfIdentityIsNeutral)
{
    Vec3 translation;
    Quat rotation;
    Vec3 scale;
    Math::DecomposeTransform(Mat4(1.0f), translation, rotation, scale);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(translation, Vec3(0.0f), 1e-6f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(scale, Vec3(1.0f), 1e-6f)));
    EXPECT_NEAR(std::abs(glm::dot(rotation, Quat(1.0f, 0.0f, 0.0f, 0.0f))), 1.0f, 1e-6f);
}

TEST(MathTest, DecomposeTransformFoldsNegativeScaleIntoX)
{
    // A mirror in X is stored as scale (-1, 1, 1) with the remaining basis
    // left-handed so that the quaternion conversion stays valid.
    const Mat4 transform = glm::scale(Mat4(1.0f), Vec3(-2.0f, 2.0f, 2.0f));

    Vec3 translation;
    Quat rotation;
    Vec3 scale;
    Math::DecomposeTransform(transform, translation, rotation, scale);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(scale, Vec3(-2.0f, 2.0f, 2.0f), 1e-4f)));

    // The recovered transform must reproduce the original matrix.
    const Mat4 rebuilt = Math::ComposeTransform(translation, rotation, scale);
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(rebuilt[column][row], transform[column][row], 1e-4f);
        }
    }
}

TEST(MathTest, WithTranslationPreservesBasis)
{
    const Mat4 transform = Math::ComposeTransform(Vec3(1.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(2.0f));
    const Mat4 moved = Math::WithTranslation(transform, Vec3(9.0f, 9.0f, 9.0f));

    EXPECT_FLOAT_EQ(moved[3][0], 9.0f);
    EXPECT_FLOAT_EQ(moved[3][1], 9.0f);
    EXPECT_FLOAT_EQ(moved[3][2], 9.0f);

    for (int column = 0; column < 3; ++column)
    {
        for (int row = 0; row < 3; ++row)
        {
            EXPECT_FLOAT_EQ(moved[column][row], transform[column][row]);
        }
    }
}

TEST(MathTest, WithRotationPreservesTranslationAndScale)
{
    const Vec3 translation(4.0f, 5.0f, 6.0f);
    const Quat original = glm::normalize(Quat(glm::angleAxis(ToRadians(20.0f), Vec3(0.0f, 0.0f, 1.0f))));
    const Quat replacement = glm::normalize(Quat(glm::angleAxis(ToRadians(70.0f), Vec3(1.0f, 0.0f, 0.0f))));

    const Mat4 rotated = Math::WithRotation(Math::ComposeTransform(translation, original, Vec3(3.0f)), replacement);

    Vec3 outTranslation;
    Quat outRotation;
    Vec3 outScale;
    Math::DecomposeTransform(rotated, outTranslation, outRotation, outScale);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(outTranslation, translation, 1e-4f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(outScale, Vec3(3.0f), 1e-4f)));
    EXPECT_NEAR(std::abs(glm::dot(outRotation, replacement)), 1.0f, 1e-4f);
}

TEST(MathTest, WithScalePreservesTranslationAndRotation)
{
    const Vec3 translation(-1.0f, 0.0f, 2.0f);
    const Quat rotation = glm::normalize(Quat(glm::angleAxis(ToRadians(45.0f), Vec3(0.0f, 1.0f, 0.0f))));

    const Mat4 scaled = Math::WithScale(Math::ComposeTransform(translation, rotation, Vec3(1.0f)), Vec3(5.0f, 6.0f, 7.0f));

    Vec3 outTranslation;
    Quat outRotation;
    Vec3 outScale;
    Math::DecomposeTransform(scaled, outTranslation, outRotation, outScale);

    EXPECT_TRUE(glm::all(glm::epsilonEqual(outTranslation, translation, 1e-4f)));
    EXPECT_TRUE(glm::all(glm::epsilonEqual(outScale, Vec3(5.0f, 6.0f, 7.0f), 1e-4f)));
    EXPECT_NEAR(std::abs(glm::dot(outRotation, rotation)), 1.0f, 1e-4f);
}

TEST(MathTest, LookAtPlacesTargetOnNegativeZ)
{
    const Vec3 eye(0.0f, 0.0f, 5.0f);
    const Vec3 target(0.0f, 0.0f, 0.0f);
    const Mat4 view = Math::LookAt(eye, target, Vec3(0.0f, 1.0f, 0.0f));

    // The eye sits at the origin of view space.
    const Vec4 eyeInView = view * Vec4(eye, 1.0f);
    EXPECT_NEAR(eyeInView.x, 0.0f, 1e-5f);
    EXPECT_NEAR(eyeInView.y, 0.0f, 1e-5f);
    EXPECT_NEAR(eyeInView.z, 0.0f, 1e-5f);

    // Looking down -Z means the target has negative view-space Z.
    const Vec4 targetInView = view * Vec4(target, 1.0f);
    EXPECT_NEAR(targetInView.z, -5.0f, 1e-5f);
}

TEST(MathTest, LookAtHandlesDegenerateDirection)
{
    // Looking parallel to the up vector has no valid right axis; the engine must
    // still return a finite matrix rather than NaNs.
    const Mat4 view = Math::LookAt(Vec3(0.0f), Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));

    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_TRUE(std::isfinite(view[column][row]));
        }
    }
}

TEST(MathTest, PerspectiveMapsClipRangeToZeroOne)
{
    const float nearZ = 0.1f;
    const float farZ = 100.0f;
    const Mat4 projection = Math::Perspective(90.0f, 16.0f / 9.0f, nearZ, farZ);

    const Vec4 atNear = projection * Vec4(0.0f, 0.0f, -nearZ, 1.0f);
    const Vec4 atFar = projection * Vec4(0.0f, 0.0f, -farZ, 1.0f);

    EXPECT_NEAR(atNear.z / atNear.w, 0.0f, 1e-4f);
    EXPECT_NEAR(atFar.z / atFar.w, 1.0f, 1e-4f);
}

TEST(MathTest, PerspectiveIsRightHanded)
{
    // A point in front of the camera must have negative eye-space Z, which the
    // Vulkan-style projection turns into positive clip-space W.
    const Mat4 projection = Math::Perspective(60.0f, 1.0f, 0.1f, 10.0f);
    const Vec4 clip = projection * Vec4(0.0f, 0.0f, -1.0f, 1.0f);

    EXPECT_GT(clip.w, 0.0f);
}

TEST(MathTest, OrthographicMapsClipRangeToZeroOne)
{
    const Mat4 projection = Math::Orthographic(-2.0f, 2.0f, -2.0f, 2.0f, 0.5f, 50.0f);

    const Vec4 atNear = projection * Vec4(0.0f, 0.0f, -0.5f, 1.0f);
    const Vec4 atFar = projection * Vec4(0.0f, 0.0f, -50.0f, 1.0f);

    EXPECT_NEAR(atNear.z, 0.0f, 1e-4f);
    EXPECT_NEAR(atFar.z, 1.0f, 1e-4f);
}

TEST(MathTest, OrthographicMapsCornersToClipSpace)
{
    const Mat4 projection = Math::Orthographic(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f);

    const Vec4 leftBottom = projection * Vec4(-1.0f, -1.0f, 0.0f, 1.0f);
    const Vec4 rightTop = projection * Vec4(1.0f, 1.0f, -1.0f, 1.0f);

    EXPECT_NEAR(leftBottom.x, -1.0f, 1e-5f);
    EXPECT_NEAR(leftBottom.y, -1.0f, 1e-5f);
    EXPECT_NEAR(rightTop.x, 1.0f, 1e-5f);
    EXPECT_NEAR(rightTop.y, 1.0f, 1e-5f);
}
