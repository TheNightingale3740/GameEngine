// Ecs/Components.cpp

#include "Ecs/ComponentRegistry.h"
#include "Ecs/ComponentStorage.h"
#include "Ecs/Components.h"

#include "Core/Math/Math.h"

namespace Ember
{
    Mat4 TransformComponent::ToMatrix() const noexcept
    {
        return Math::ComposeTransform(Position, Rotation, Scale);
    }

    void TransformComponent::FromMatrix(const Mat4& matrix) noexcept
    {
        Math::DecomposeTransform(matrix, Position, Rotation, Scale);
    }
}

namespace Ember::Ecs
{
    void RegisterBuiltinComponents()
    {
        // Registering the same type twice returns the existing id, so this is
        // safe to call from every entry point that needs the built-in set.
        RegisterBuiltin<TransformComponent>("Transform",
            std::vector<Property>{
                EMBER_FIELD(TransformComponent, Vec3, Position),
                EMBER_FIELD(TransformComponent, Quat, Rotation),
                EMBER_FIELD(TransformComponent, Vec3, Scale)
            });

        RegisterBuiltin<MeshComponent>("Mesh",
            std::vector<Property>{
                EMBER_FIELD(MeshComponent, std::uint32_t, MeshId),
                EMBER_FIELD(MeshComponent, std::uint32_t, MaterialId),
                EMBER_FIELD(MeshComponent, std::uint32_t, FirstIndex),
                EMBER_FIELD(MeshComponent, std::uint32_t, IndexCount),
                EMBER_FIELD(MeshComponent, bool, Visible),
                EMBER_FIELD(MeshComponent, bool, DoubleSided)
            });

        RegisterBuiltin<LightComponent>("Light",
            std::vector<Property>{
EMBER_ENUM_FIELD(LightType, LightComponent, "Directional", "Point", "Spot"),
                EMBER_FIELD(LightComponent, Vec3, Color),
                EMBER_FIELD(LightComponent, float, Intensity),
                EMBER_FIELD(LightComponent, float, InnerAngleDegrees),
                EMBER_FIELD(LightComponent, float, Range),
                EMBER_FIELD(LightComponent, bool, CastsShadows),
                EMBER_FIELD(LightComponent, std::uint32_t, ShadowResolution)
            });

        RegisterBuiltin<CameraComponent>("Camera",
            std::vector<Property>{
                EMBER_FIELD(CameraComponent, float, FieldOfViewDegrees),
                EMBER_FIELD(CameraComponent, float, NearPlane),
                EMBER_FIELD(CameraComponent, float, FarPlane),
                EMBER_FIELD(CameraComponent, bool, Orthographic),
                EMBER_FIELD(CameraComponent, float, OrthographicSize)
            });

        RegisterBuiltin<ColliderComponent>("Collider",
            std::vector<Property>{
EMBER_ENUM_FIELD(BodyShape, ColliderComponent, "Box", "Sphere", "Capsule", "Plane"),
EMBER_ENUM_FIELD(BodyMode, ColliderComponent, "Static", "Dynamic", "Kinematic"),
                EMBER_FIELD(ColliderComponent, Vec3, Extents),
                EMBER_FIELD(ColliderComponent, float, Radius),
                EMBER_FIELD(ColliderComponent, float, HalfHeight),
                EMBER_FIELD(ColliderComponent, float, Restitution),
                EMBER_FIELD(ColliderComponent, float, Friction),
                EMBER_FIELD(ColliderComponent, float, GravityScale),
                EMBER_FIELD(ColliderComponent, float, Mass),
                EMBER_FIELD(ColliderComponent, bool, AllowSleeping)
            });

        RegisterBuiltin<AudioSourceComponent>("AudioSource",
            std::vector<Property>{
                EMBER_FIELD(AudioSourceComponent, std::uint32_t, SoundId),
                EMBER_FIELD(AudioSourceComponent, float, Volume),
                EMBER_FIELD(AudioSourceComponent, float, Pitch),
                EMBER_FIELD(AudioSourceComponent, bool, Looping),
                EMBER_FIELD(AudioSourceComponent, bool, Playing)
            });

        RegisterBuiltin<ScriptComponent>("Script",
            std::vector<Property>{
                EMBER_FIELD(ScriptComponent, std::string, ScriptPath),
                EMBER_FIELD(ScriptComponent, bool, Enabled)
            });

        RegisterBuiltin<PrefabComponent>("Prefab",
            std::vector<Property>{
                EMBER_FIELD(PrefabComponent, std::string, SourcePrefab),
                EMBER_FIELD(PrefabComponent, std::string, InstanceId)
            });
    }
}
