// Renderer/Material.h
//
// Physically based materials and the maths that shades them.
//
// The shading model is Cook-Torrance with a GGX distribution for the specular
// lobe and a Lambert term for the diffuse one. Image-based lighting is the split
// sum approximation: the environment's irradiance and prefiltered specular
// radiance are both precomputed offline, and a single real-time BRDF lookup
// combines them.
//
// All of it is plain arithmetic with no graphics API involved, which is what
// makes it testable: an energy-conservation bug in a shader is invisible until
// something looks slightly wrong, whereas `Diffuse()` returning more energy than
// went in is a number a test can catch.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Math/Math.h"
#include "Core/Result.h"

namespace Ember
{
    /// PBR constants, all normalised against a Cook-Torrance BRDF.
    namespace Pbr
    {
        /// Smallest roughness considered. A perfectly smooth surface makes the
        /// specular denominator collapse, and the light becomes a mirror.
        inline constexpr float MinimumRoughness = 0.045f;

        /// Largest roughness considered. A perfectly rough surface has no
        /// specular lobe left to integrate.
        inline constexpr float MaximumRoughness = 1.0f;

        /// Reflectance at a normal incidence, for a dielectric.
        ///
        /// 0.04 is the standard value for most solids. Metals use their own
        /// reflectance instead, from their base colour.
        inline constexpr float DielectricReflectance = 0.04f;

        /// Index of refraction derived from a reflectance.
        [[nodiscard]] float ReflectanceToRefractiveIndex(float reflectance) noexcept;

        /// Fresnel term for a dielectric, using Schlick's approximation.
        ///
        /// `cosTheta` is the angle between the view direction and the surface
        /// normal, in [0, 1].
        [[nodiscard]] float FresnelSchlick(float cosTheta, float reflectance) noexcept;

        /// GGX normal distribution term.
        ///
        /// Returns the distribution of microfacet normals around `normal`. A
        /// larger value means the surface is more mirror-like at this roughness.
        [[nodiscard]] float DistributionGGX(float normalDotHalf, float roughness) noexcept;

        /// Smith's geometry term with the Schlick-GGX approximation.
        [[nodiscard]] float GeometrySchlickGGX(float normalDotView, float roughness) noexcept;

        /// Height-correlated Smith geometry term.
        ///
        /// Preferred over the uncorrelated form because it does not
        /// over-brighten grazing angles, which is where most visible energy
        /// comes from.
        [[nodiscard]] float GeometrySmithGGXCorrelated(float normalDotView,
                                                      float normalDotLight,
                                                      float roughness) noexcept;

        /// Cook-Torrance specular term for one light.
        ///
        /// Returns the fraction of reflected light that reaches the viewer along
        /// the mirror direction.
        [[nodiscard]] Vec3 SpecularBRDF(const Vec3& normal,
                                        const Vec3& view,
                                        const Vec3& light,
                                        float roughness,
                                        float reflectance) noexcept;

        /// Lambert diffuse term for one light.
        ///
        /// `albedo` is the surface's base colour and `metallic` how much of it
        /// is metal: a metal has no diffuse lobe at all, because its reflection
        /// is tinted rather than absorbed.
        [[nodiscard]] Vec3 DiffuseBRDF(const Vec3& albedo, float metallic) noexcept;

        /// How much of the base colour is dielectric rather than metal.
        [[nodiscard]] Vec3 DielectricFraction(const Vec3& albedo, float metallic) noexcept;

        /// Fresnel-weighted reflectance of a surface.
        ///
        /// Blends towards the base colour for grazing angles, which is what makes
        /// a metal look coloured at the edges and a dielectric look white.
        [[nodiscard]] Vec3 FresnelBlend(const Vec3& reflectance,
                                        const Vec3& baseColour,
                                        float cosTheta) noexcept;
    }

    /// Tone mapping operators.
    ///
    /// The renderer's HDR pipeline ends in one of these. Each maps an unbounded
    /// linear value to a display value in [0, 1] while keeping mid-tones in a
    /// predictable place, which is what stops a bright scene from clipping to
    /// flat white.
    namespace ToneMapping
    {
        /// The Reinhard curve: `x / (1 + x)`.
        ///
        /// Rolls highlights off gradually but washes out contrast, so it is used
        /// only where a flat, forgiving look is wanted.
        [[nodiscard]] float Reinhard(float value) noexcept;

        /// Reinhard with a white point, so values at or above it map to white.
        [[nodiscard]] float ReinhardExtended(float value, float whitePoint) noexcept;

        /// ACES filmic curve, the Krzysztof Narkowicz fit.
        ///
        /// Applied after the sRGB transfer function rather than before, which is
        /// what the film industry does and what keeps highlights from going grey.
        [[nodiscard]] float AcesFilmic(float value) noexcept;

        /// The sRGB transfer function, from linear light to display values.
        [[nodiscard]] float LinearToSrgb(float value) noexcept;

        /// The inverse of `LinearToSrgb`.
        [[nodiscard]] float SrgbToLinear(float value) noexcept;

        /// Applies a tone map and the sRGB transfer function, in that order.
        [[nodiscard]] float ToDisplay(float value, float exposure = 1.0f) noexcept;
    }

    /// The engine's tone mapping operators.
    enum class ToneMapOperator : std::int32_t
    {
        None = 0,
        Reinhard = 1,
        ReinhardExtended = 2,
        AcesFilmic = 3
    };

    /// A material's shading parameters.
    ///
    /// Textures are referenced by asset id rather than held: the renderer resolves
    /// an id to a texture at bind time, which is what lets several materials share
    /// one texture and lets the editor show a material without loading its images.
    struct Material
    {
        std::string Name;

        /// Base colour, in linear space.
        Vec3 BaseColor = Vec3(0.8f);

        /// How metallic the surface is, in [0, 1].
        float Metallic = 0.0f;

        /// Surface roughness, in [0, 1].
        float Roughness = 0.5f;

        /// Refractive index, from which the reflectance is derived. A dielectric
        /// is around 1.5; a metal's reflectance comes from its base colour.
        float RefractiveIndex = 1.5f;

        /// Displacement along the normal, from a height map.
        float NormalScale = 1.0f;

        /// Strength of the parallax occlusion mapping effect.
        float DisplacementScale = 1.0f;

        /// Amount of light the surface lets through, in [0, 1].
        float Transparency = 0.0f;

        /// Whether the surface casts shadows.
        bool CastsShadow = true;

        /// Asset ids of the textures, or 0 for none.
        std::uint32_t BaseColorTextureId = 0;
        std::uint32_t NormalTextureId = 0;
        std::uint32_t RoughnessTextureId = 0;
        std::uint32_t MetallicTextureId = 0;
        std::uint32_t AmbientOcclusionTextureId = 0;
        std::uint32_t DisplacementTextureId = 0;
        std::uint32_t EmissiveTextureId = 0;

        /// Colour the surface emits on its own, in linear space.
        Vec3 EmissiveColor = Vec3(0.0f);

        [[nodiscard]] bool IsValid() const noexcept { return m_Valid; }

        /// Reflectance at normal incidence, for a dielectric surface.
        [[nodiscard]] float GetReflectance() const noexcept;

        /// Reflectance used for shading, folding in the base colour for a metal.
        [[nodiscard]] Vec3 GetShadingReflectance() const noexcept;

    private:
        friend class MaterialLibrary;

        bool m_Valid = false;
    };

    /// A named collection of materials, addressed by asset id.
    class MaterialLibrary
    {
    public:
        /// Registers a material and returns its asset id, or 0 on failure.
        ///
        /// An id is never reused: a scene that references material 3 must keep
        /// meaning material 3 even after another material is removed.
        std::uint32_t Add(Material material);

        /// Returns a material by id, or nullptr.
        [[nodiscard]] const Material* Find(std::uint32_t id) const noexcept;

        /// Removes a material. Its id stops resolving but is not reused.
        bool Remove(std::uint32_t id);

        [[nodiscard]] std::size_t Count() const noexcept { return m_Materials.size(); }
        [[nodiscard]] std::uint32_t GetCapacity() const noexcept
        {
            return static_cast<std::uint32_t>(m_Materials.size());
        }

        void Clear();

    private:
        /// Sparse by id: a removed id leaves a hole rather than shifting everything.
        std::vector<Material> m_Materials;
    };
}