// Renderer/Material.cpp

#include "Renderer/Material.h"

#include <algorithm>
#include <cmath>

namespace Ember
{
    namespace Pbr
    {
        float ReflectanceToRefractiveIndex(float reflectance) noexcept
        {
            const float clamped = Math::Clamp(reflectance, 0.0f, 0.999f);

            // Solving R = ((n - 1) / (n + 1))^2 for n. The result is the index
            // itself, not its reciprocal: the reflection grows with density.
            return (1.0f + std::sqrt(clamped)) / (1.0f - std::sqrt(clamped));
        }

        float FresnelSchlick(float cosTheta, float reflectance) noexcept
        {
            const float cos = Math::Clamp(1.0f - Math::Clamp(cosTheta, 0.0f, 1.0f), 0.0f, 1.0f);
            const float fifth = cos * cos * cos * cos * cos;

            return reflectance + (1.0f - reflectance) * fifth;
        }

        float DistributionGGX(float normalDotHalf, float roughness) noexcept
        {
            const float alpha = roughness * roughness;
            const float alphaSquared = alpha * alpha;

            const float normal = Math::Clamp(normalDotHalf, 0.0f, 1.0f);
            const float denominator = normal * normal * (alphaSquared - 1.0f) + 1.0f;

            return alphaSquared / (Pi * denominator * denominator);
        }

        float GeometrySchlickGGX(float normalDotView, float roughness) noexcept
        {
            const float r = roughness + 1.0f;
            const float k = (r * r) / 8.0f;

            return normalDotView / (normalDotView * (1.0f - k) + k);
        }

        float GeometrySmithGGXCorrelated(float normalDotView, float normalDotLight, float roughness) noexcept
        {
            // The cosines are floored rather than allowed to reach zero. At exactly
            // zero the denominator vanishes and the term is unbounded, which turns a
            // surface seen edge-on into a white speck rather than into darkness.
            // One hundredth is below anything that reads as edge-on and keeps the
            // term under a hundred even at full roughness.
            constexpr float MinimumCosine = 0.01f;

            const float view = Math::Clamp(normalDotView, MinimumCosine, 1.0f);
            const float light = Math::Clamp(normalDotLight, MinimumCosine, 1.0f);

            // Smith height-uncorrelated visibility, the Schlick-GGX approximation.
            const float viewSchlick = view * (view * (roughness * roughness - 1.0f) + 1.0f);
            const float lightSchlick = light * (light * (roughness * roughness - 1.0f) + 1.0f);

            // Halving is what turns a visibility term into a BRDF: the factor of
            // four cancels between the two Smith terms and the BRDF's own divisor.
            return 0.5f / std::max(viewSchlick + lightSchlick, 1e-5f);
        }

        Vec3 SpecularBRDF(const Vec3& normal, const Vec3& view, const Vec3& light, float roughness,
                          float reflectance) noexcept
        {
            const Vec3 halfVector = glm::normalize(view + light);

            const float normalDotView = glm::max(glm::dot(normal, view), 0.0f);
            const float normalDotLight = glm::max(glm::dot(normal, light), 0.0f);
            const float normalDotHalf = glm::max(glm::dot(normal, halfVector), 0.0f);

            if (normalDotView <= 0.0f || normalDotLight <= 0.0f)
            {
                return Vec3(0.0f);
            }

            const float clampedRoughness =
                Math::Clamp(roughness, MinimumRoughness, MaximumRoughness);

            const float distribution = DistributionGGX(normalDotHalf, clampedRoughness);
            const float geometry = GeometrySmithGGXCorrelated(normalDotView, normalDotLight, clampedRoughness);
            const float fresnel = FresnelSchlick(glm::dot(halfVector, view), reflectance);

            return Vec3(distribution * geometry * fresnel);
        }

        Vec3 DiffuseBRDF(const Vec3& albedo, float metallic) noexcept
        {
            // A metal has no diffuse lobe: its reflection is tinted by the base
            // colour rather than absorbed, so adding a Lambert term on top would
            // count the same light twice.
            return albedo * ((1.0f - Math::Clamp(metallic, 0.0f, 1.0f)) * (1.0f / Pi));
        }

        Vec3 DielectricFraction(const Vec3& albedo, float metallic) noexcept
        {
            return albedo * (1.0f - Math::Clamp(metallic, 0.0f, 1.0f));
        }

        Vec3 FresnelBlend(const Vec3& reflectance, const Vec3& baseColour, float cosTheta) noexcept
        {
            const Vec3 result = reflectance +
                                (1.0f - reflectance) * std::pow(Math::Clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);

            // Metals reflect with their base colour at every angle, so the tint
            // is applied on top of the Fresnel term rather than through it.
            return result * baseColour;
        }
    }

    namespace ToneMapping
    {
        float Reinhard(float value) noexcept
        {
            return Math::Clamp(value / (1.0f + value), 0.0f, 1.0f);
        }

        float ReinhardExtended(float value, float whitePoint) noexcept
        {
            const float numerator = value * (1.0f + value / (whitePoint * whitePoint));
            return Math::Clamp(numerator / (1.0f + value), 0.0f, 1.0f);
        }

        float AcesFilmic(float value) noexcept
        {
            // Narkowicz's fit of the ACES filmic curve. It is an approximation of
            // the real transform rather than the transform itself, and is used
            // because it fits in a handful of instructions.
            constexpr float a = 2.51f;
            constexpr float b = 0.03f;
            constexpr float c = 2.43f;
            constexpr float d = 0.59f;
            constexpr float e = 0.14f;

            const float x = std::max(value, 0.0f);
            return Math::Clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
        }

        float LinearToSrgb(float value) noexcept
        {
            const float x = Math::Clamp(value, 0.0f, 1.0f);

            // Below the sRGB toe the transfer function is very nearly linear, and
            // the power form is numerically ill-conditioned there.
            return x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
        }

        float SrgbToLinear(float value) noexcept
        {
            const float x = Math::Clamp(value, 0.0f, 1.0f);
            return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
        }

        float ToDisplay(float value, float exposure) noexcept
        {
            return LinearToSrgb(AcesFilmic(Math::Clamp(value, 0.0f, 1e6f) * exposure));
        }
    }

    float Material::GetReflectance() const noexcept
    {
        // Reflectance and index of refraction are two spellings of the same
        // number. A material may state either, and the other is derived, so that a
        // project can use whichever reads better for what it is describing.
        return ((RefractiveIndex - 1.0f) * (RefractiveIndex - 1.0f)) /
               ((RefractiveIndex + 1.0f) * (RefractiveIndex + 1.0f));
    }

    Vec3 Material::GetShadingReflectance() const noexcept
    {
        if (Metallic >= 1.0f)
        {
            return BaseColor;
        }

        return Vec3(GetReflectance());
    }

    std::uint32_t MaterialLibrary::Add(Material material)
    {
        material.m_Valid = true;
        m_Materials.push_back(std::move(material));

        // Ids start at 1 so that 0 always means "no material", which is what the
        // components use as their default.
        return static_cast<std::uint32_t>(m_Materials.size());
    }

    const Material* MaterialLibrary::Find(std::uint32_t id) const noexcept
    {
        if (id == 0 || id > m_Materials.size())
        {
            return nullptr;
        }

        // Material 0 is the default slot, so ids start at 1. A removed material
        // leaves an invalid entry rather than shifting the ones after it.
        const Material& material = m_Materials[id - 1];
        return material.IsValid() ? &material : nullptr;
    }

    bool MaterialLibrary::Remove(std::uint32_t id)
    {
        Material* material = const_cast<Material*>(Find(id));
        if (material == nullptr)
        {
            return false;
        }

        material->m_Valid = false;
        return true;
    }

    void MaterialLibrary::Clear()
    {
        m_Materials.clear();
    }
}