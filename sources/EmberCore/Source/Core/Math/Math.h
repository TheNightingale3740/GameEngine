// Core/Math/Math.h
//
// Engine-wide mathematics aliases. glm is used verbatim underneath; this header
// exists so that the engine never writes `glm::` directly in engine code and so
// that the math configuration is stated exactly once.
//
// Configuration is fixed engine-wide and MUST stay consistent between the host
// and shader stages, because uploaded vertex buffers are memcpy'd raw into GPU
// memory and must be interpreted identically by both sides.

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Ember
{
    using Vec2 = glm::vec2;
    using Vec3 = glm::vec3;
    using Vec4 = glm::vec4;

    using IVec2 = glm::ivec2;
    using IVec3 = glm::ivec3;
    using IVec4 = glm::ivec4;

    using UVec2 = glm::uvec2;
    using UVec3 = glm::uvec3;
    using UVec4 = glm::uvec4;

    using Mat3 = glm::mat3;
    using Mat4 = glm::mat4;

    using Quat = glm::quat;

    constexpr float Pi = 3.14159265358979323846f;
    constexpr float TwoPi = Pi * 2.0f;
    constexpr float HalfPi = Pi * 0.5f;
    constexpr float DegreesToRadians = Pi / 180.0f;
    constexpr float RadiansToDegrees = 180.0f / Pi;

    /// Converts degrees to radians.
    [[nodiscard]] constexpr float ToRadians(float degrees) noexcept { return degrees * DegreesToRadians; }

    /// Converts radians to degrees.
    [[nodiscard]] constexpr float ToDegrees(float radians) noexcept { return radians * RadiansToDegrees; }

    namespace Math
    {
        /// True when two floats are within `tolerance` of each other.
        [[nodiscard]] constexpr bool NearlyEqual(float a, float b, float tolerance = 1e-5f) noexcept
        {
            const float delta = a - b;
            return (delta < 0.0f ? -delta : delta) <= tolerance;
        }

        /// Linear interpolation. `t` is not clamped.
        [[nodiscard]] constexpr float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }

        /// Clamps `value` into `[min, max]`.
        [[nodiscard]] constexpr float Clamp(float value, float min, float max) noexcept
        {
            return value < min ? min : (value > max ? max : value);
        }

        /// Returns a vector with a magnitude of at most `maxLength`.
        [[nodiscard]] Vec3 ClampLength(const Vec3& vector, float maxLength) noexcept;

        /// Decomposes a transform matrix into translation, rotation and scale.
        ///
        /// Assumes the matrix is free of shear. Negative determinant is treated
        /// as mirrored scale and folded into the X axis.
        void DecomposeTransform(const Mat4& transform, Vec3& outTranslation, Quat& outRotation, Vec3& outScale) noexcept;

        /// Composes a transform matrix from translation, rotation and scale.
        [[nodiscard]] Mat4 ComposeTransform(const Vec3& translation, const Quat& rotation, const Vec3& scale) noexcept;

        /// Replaces the translation column of a transform.
        [[nodiscard]] Mat4 WithTranslation(const Mat4& transform, const Vec3& translation) noexcept;

        /// Replaces the rotation of a transform (preserving translation and scale).
        [[nodiscard]] Mat4 WithRotation(const Mat4& transform, const Quat& rotation) noexcept;

        /// Replaces the scale of a transform (preserving translation and rotation).
        [[nodiscard]] Mat4 WithScale(const Mat4& transform, const Vec3& scale) noexcept;

        /// Builds a right-handed look-at view matrix.
        [[nodiscard]] Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept;

        /// Builds a right-handed perspective projection with a [0, 1] depth range,
        /// which is what Vulkan clip space expects.
        [[nodiscard]] Mat4 Perspective(float fovYDegrees, float aspect, float nearZ, float farZ) noexcept;

        /// Builds a right-handed orthographic projection with a [0, 1] depth range.
        [[nodiscard]] Mat4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ) noexcept;
    }
}
