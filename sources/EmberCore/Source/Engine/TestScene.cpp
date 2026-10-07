// Engine/TestScene.cpp

#include "Engine/TestScene.h"

#include "Core/FileSystem.h"
#include "Ecs/ComponentRegistry.h"
#include "Ecs/Components.h"

namespace Ember::TestScene
{
    namespace
    {
        /// Adds a transform at a position, and returns the entity.
        Entity AddEntity(World& world, std::string name, const Vec3& position)
        {
            const Entity entity = world.CreateEntity(std::move(name));

            TransformComponent transform;
            transform.Position = position;
            world.AddComponent(entity, transform);

            return entity;
        }
    }

    std::string GetScriptSource()
    {
        // The script does the three things a game script must: change its own
        // entity every frame, spawn another one, and destroy it again later.
        //
        // It reports through globals rather than returning values, because a
        // script's only output is what it does to the world.
        return R"LUA(
-- Ember test scene behaviour.
--
-- Spheres move in a circle and every twelfth frame one is spawned and destroyed,
-- so that a test running the scene sees movement, spawning and destruction
-- happen together.

updateCount = 0
spawnedCount = 0
destroyedCount = 0

spawnedEntity = nil
radius = 2.0
speed = 1.0

function OnStart(entity)
    spawnedEntity = Entity.create("ScriptSpawned")
    spawnedCount = spawnedCount + 1
end

function OnUpdate(entity, deltaTime)
    updateCount = updateCount + 1

    -- Lua's own `math` library provides the trigonometry; the engine's `Math`
    -- table is for vector operations, which are a different thing.
    local angle = updateCount * speed * 0.1
    local transform = Component.get(entity, "Transform")

    if transform ~= nil then
        transform.Position.x = math.cos(angle) * radius
        transform.Position.z = math.sin(angle) * radius
        Component.set(entity, "Transform", transform)
    end

    -- Every twelfth frame the spawned entity moves too, which exercises a script
    -- affecting an entity other than its own.
    if (updateCount % 12) == 0 and Entity.is_valid(spawnedEntity) then
        local spawnedTransform = Component.get(spawnedEntity, "Transform")
        if spawnedTransform ~= nil then
            spawnedTransform.Position.y = 1.0 + updateCount * 0.01
            Component.set(spawnedEntity, "Transform", spawnedTransform)
        end
    end
end

function OnDestroy(entity)
    if Entity.is_valid(spawnedEntity) then
        Entity.destroy(spawnedEntity)
        destroyedCount = destroyedCount + 1
    end
end
)LUA";
    }

    Scene Build()
    {
        World world;

        // The ground: a visible plane with a static box collider underneath it, so
        // that the renderer has something to draw and the physics has something to
        // land on.
        const Entity ground = AddEntity(world, "Ground", Vec3(0.0f, 0.0f, 0.0f));

        MeshComponent groundMesh;
        groundMesh.MeshId = 1;
        groundMesh.MaterialId = 1;
        world.AddComponent(ground, groundMesh);

        ColliderComponent groundCollider;
        groundCollider.BodyShape = ColliderComponent::Shape::Box;
        groundCollider.BodyMode = ColliderComponent::Mode::Static;
        groundCollider.Extents = Vec3(20.0f, 0.5f, 20.0f);
        world.AddComponent(ground, groundCollider);

        // A camera, looking at the ground from above and to one side.
        const Entity camera = AddEntity(world, "Camera", Vec3(0.0f, 6.0f, 12.0f));

        CameraComponent cameraComponent;
        cameraComponent.FieldOfViewDegrees = 60.0f;
        cameraComponent.NearPlane = 0.1f;
        cameraComponent.FarPlane = 200.0f;
        world.AddComponent(camera, cameraComponent);

        // A directional light, casting shadows.
        const Entity sun = AddEntity(world, "Sun", Vec3(0.0f, 10.0f, 0.0f));

        LightComponent sunLight;
        sunLight.LightType = LightComponent::Type::Directional;
        sunLight.Color = Vec3(1.0f, 0.95f, 0.9f);
        sunLight.Intensity = 5.0f;
        sunLight.CastsShadows = true;
        sunLight.ShadowResolution = 2048;
        world.AddComponent(sun, sunLight);

        // A point light, for the non-shadowing light path.
        const Entity lamp = AddEntity(world, "Lamp", Vec3(4.0f, 3.0f, 4.0f));

        LightComponent lampLight;
        lampLight.LightType = LightComponent::Type::Point;
        lampLight.Color = Vec3(1.0f, 0.4f, 0.2f);
        lampLight.Intensity = 10.0f;
        lampLight.Range = 15.0f;
        lampLight.CastsShadows = false;
        world.AddComponent(lamp, lampLight);

        // A dynamic box that falls onto the ground.
        const Entity falling = AddEntity(world, "FallingBox", Vec3(0.0f, 8.0f, 0.0f));

        MeshComponent fallingMesh;
        fallingMesh.MeshId = 2;
        fallingMesh.MaterialId = 2;
        world.AddComponent(falling, fallingMesh);

        ColliderComponent fallingCollider;
        fallingCollider.BodyShape = ColliderComponent::Shape::Box;
        fallingCollider.BodyMode = ColliderComponent::Mode::Dynamic;
        fallingCollider.Extents = Vec3(0.5f);
        fallingCollider.Mass = 2.0f;
        fallingCollider.Restitution = 0.2f;
        fallingCollider.Friction = 0.6f;
        world.AddComponent(falling, fallingCollider);

        // A kinematic sphere, which the script drives.
        const Entity sphere = AddEntity(world, "DrivenSphere", Vec3(3.0f, 2.0f, 0.0f));

        MeshComponent sphereMesh;
        sphereMesh.MeshId = 3;
        sphereMesh.MaterialId = 2;
        world.AddComponent(sphere, sphereMesh);

        ColliderComponent sphereCollider;
        sphereCollider.BodyShape = ColliderComponent::Shape::Sphere;
        sphereCollider.BodyMode = ColliderComponent::Mode::Kinematic;
        sphereCollider.Radius = 0.5f;
        world.AddComponent(sphere, sphereCollider);

        // The script that drives it. `WriteToProject` writes this file into the
        // project's scripts directory, so the path is the same one the scene uses.
        ScriptComponent sphereScript;
        sphereScript.ScriptPath = "driven_sphere.lua";
        world.AddComponent(sphere, sphereScript);

        // A speaker, playing a generated tone.
        const Entity speaker = AddEntity(world, "Speaker", Vec3(2.0f, 1.0f, 2.0f));

        AudioSourceComponent audio;
        audio.SoundId = 1;
        audio.Volume = 0.5f;
        audio.Looping = true;
        audio.Playing = true;
        world.AddComponent(speaker, audio);

        // A prefab instance, so that the scene file carries the component the
        // editor writes when something is instantiated from a prefab.
        const Entity instance = AddEntity(world, "PrefabInstance", Vec3(-3.0f, 0.0f, 0.0f));

        PrefabComponent prefab;
        prefab.SourcePrefab = "marker.prefab";
        prefab.InstanceId = "instance-0001";
        world.AddComponent(instance, prefab);

        // A hierarchy: a disabled parent with one child that is itself enabled.
        // Disabling a parent is what makes the child invisible to every system,
        // which the tests rely on.
        const Entity disabledParent = AddEntity(world, "DisabledParent", Vec3(-6.0f, 0.0f, 0.0f));
        world.SetEnabled(disabledParent, false);

        const Entity hiddenChild = AddEntity(world, "HiddenChild", Vec3(-6.0f, 1.0f, 0.0f));
        world.SetParent(hiddenChild, disabledParent);

        return Scene::Capture(world);
    }

    Result<std::string> WriteToProject(const Project& project, const std::string& sceneName)
    {
        if (!project.IsValid())
        {
            return {ErrorCode::InvalidArgument, "The project was not opened"};
        }

        if (Result<void> saved = Build().SaveToFile(project.GetScenesDirectory() / sceneName); saved.IsFailure())
        {
            return saved.GetError();
        }

        const std::string scriptName = "driven_sphere.lua";
        if (Result<void> written =
                FileSystem::WriteTextFile(project.GetScriptsDirectory() / scriptName, GetScriptSource());
            written.IsFailure())
        {
            return written.GetError();
        }

        return scriptName;
    }
}