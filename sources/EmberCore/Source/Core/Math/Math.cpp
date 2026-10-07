// Core/Math/Math.cpp

#include "Core/Math/Math.h"

#include <cmath>

namespace Ember::Math
{
    Vec3 ClampLength(const Vec3& vector, float maxLength) noexcept
    {
        const float lengthSquared = glm::dot(vector, vector);
        if (lengthSquared <= maxLength * maxLength || lengthSquared <= 0.0f)
        {
            return vector;
        }

        return vector * (maxLength / std::sqrt(lengthSquared));
    }

    namespace
    {
        /// Divides an axis by its length, returning a fallback for a collapsed axis.
        Vec3 SafeNormalizeAxis(const Vec3& axis, float length, const Vec3& fallback) noexcept
        {
            return length > 0.0f ? axis / length : fallback;
        }
    }

    void DecomposeTransform(const Mat4& transform, Vec3& outTranslation, Quat& outRotation, Vec3& outScale) noexcept
    {
        // glm is column-major, so the basis vectors of the rotation are the
        // matrix columns, not its rows.
        const Vec3 axisX(transform[0]);
        const Vec3 axisY(transform[1]);
        const Vec3 axisZ(transform[2]);

        const float lengthX = glm::length(axisX);
        const float lengthY = glm::length(axisY);
        const float lengthZ = glm::length(axisZ);

        outTranslation = Vec3(transform[3]);
        outScale = Vec3(lengthX, lengthY, lengthZ);

        // glm's quaternion conversion assumes an unscaled basis, so the rotation
        // is recovered from the renormalised columns.
        Mat3 rotationBasis(
            SafeNormalizeAxis(axisX, lengthX, Vec3(1.0f, 0.0f, 0.0f)),
            SafeNormalizeAxis(axisY, lengthY, Vec3(0.0f, 1.0f, 0.0f)),
            SafeNormalizeAxis(axisZ, lengthZ, Vec3(0.0f, 0.0f, 1.0f)));

        // A mirrored transform shows up as a negative determinant. Push the flip
        // onto the X axis so that the remaining basis stays right-handed and the
        // quaternion conversion below produces a valid result.
        if (glm::determinant(rotationBasis) < 0.0f)
        {
            outScale.x = -outScale.x;
            rotationBasis[0] = -rotationBasis[0];
        }

        outRotation = glm::normalize(Quat(rotationBasis));
    }

    Mat4 ComposeTransform(const Vec3& translation, const Quat& rotation, const Vec3& scale) noexcept
    {
        return glm::translate(Mat4(1.0f), translation) * glm::mat4_cast(rotation) * glm::scale(Mat4(1.0f), scale);
    }

    Mat4 WithTranslation(const Mat4& transform, const Vec3& translation) noexcept
    {
        Mat4 result = transform;
        result[3] = Vec4(translation, 1.0f);
        return result;
    }

    Mat4 WithRotation(const Mat4& transform, const Quat& rotation) noexcept
    {
        Vec3 translation;
        Quat unusedRotation;
        Vec3 scale;
        DecomposeTransform(transform, translation, unusedRotation, scale);
        return ComposeTransform(translation, rotation, scale);
    }

    Mat4 WithScale(const Mat4& transform, const Vec3& scale) noexcept
    {
        Vec3 translation;
        Quat rotation;
        Vec3 unusedScale;
        DecomposeTransform(transform, translation, rotation, unusedScale);
        return ComposeTransform(translation, rotation, scale);
    }

    Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept
    {
        const Vec3 forward = glm::normalize(target - eye);

        // When the view direction is parallel to `up` the cross product vanishes
        // and normalising it would produce NaNs. Fall back to any vector that is
        // not parallel to the view direction so that the matrix stays usable.
        Vec3 right = glm::cross(forward, up);
        const float rightLength = glm::length(right);
        if (rightLength > 1e-6f)
        {
            right /= rightLength;
        }
        else
        {
            const Vec3 fallback = std::abs(forward.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f);
            right = glm::normalize(glm::cross(forward, fallback));
        }

        const Vec3 trueUp = glm::cross(right, forward);

        Mat4 result(1.0f);
        result[0] = Vec4(right.x, trueUp.x, -forward.x, 0.0f);
        result[1] = Vec4(right.y, trueUp.y, -forward.y, 0.0f);
        result[2] = Vec4(right.z, trueUp.z, -forward.z, 0.0f);
        result[3] = Vec4(-glm::dot(right, eye), -glm::dot(trueUp, eye), glm::dot(forward, eye), 1.0f);
        return result;
    }

    Mat4 Perspective(float fovYDegrees, float aspect, float nearZ, float farZ) noexcept
    {
        const float tanHalfFov = std::tan(fovYDegrees * DegreesToRadians * 0.5f);

        Mat4 result(0.0f);
        result[0][0] = 1.0f / (aspect * tanHalfFov);
        result[1][1] = 1.0f / tanHalfFov;
        result[2][2] = farZ / (nearZ - farZ);
        result[2][3] = -1.0f;
        result[3][2] = (nearZ * farZ) / (nearZ - farZ);
        return result;
    }

    Mat4 Orthographic(float left, float right, float bottom, float top, float nearZ, float farZ) noexcept
    {
        Mat4 result(1.0f);
        result[0][0] = 2.0f / (right - left);
        result[1][1] = 2.0f / (top - bottom);
        result[2][2] = -1.0f / (farZ - nearZ);
        result[3][0] = -(right + left) / (right - left);
        result[3][1] = -(top + bottom) / (top - bottom);
        result[3][2] = -nearZ / (farZ - nearZ);
        return result;
    }
}
