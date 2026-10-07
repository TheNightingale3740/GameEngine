// Engine/Application.h
//
// The frame loop.
//
// One `Application` owns a world's subsystems and runs them in a fixed order per
// frame. The order is the contract:
//
//   1. scripts start, for entities that just gained a ScriptComponent
//   2. scripts update
//   3. physics steps
//   4. physics results are written back into the world
//   5. audio mixes
//   6. the render queue is rebuilt
//
// Physics runs after scripts so that a script can set a velocity and have it
// simulated in the same frame. The render queue is rebuilt last so that it
// reflects everything the frame did, including a script that spawned something.
//
// Headless: the application runs the same order with no graphics device at all,
// which is what lets a test drive a whole game frame and check what it did.

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "Audio/AudioDevice.h"
#include "Core/Math/Math.h"
#include "Core/Result.h"
#include "Ecs/World.h"
#include "Physics/PhysicsWorld.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"
#include "Scripting/ScriptEngine.h"

namespace Ember
{
    /// What one frame cost and what it did, for tests and for a statistics panel.
    struct FrameStatistics
    {
        /// Seconds the frame was told to advance.
        float DeltaTime = 0.0f;

        /// Seconds since the application started.
        double ElapsedTime = 0.0;

        /// Frames run so far.
        std::uint64_t FrameCount = 0;

        /// Entities that exist, after the frame.
        std::size_t EntityCount = 0;

        /// Scripts that are running, after the frame.
        std::size_t ActiveScriptCount = 0;

        /// Draw items the render queue produced.
        std::size_t DrawItemCount = 0;

        /// Bodies the simulation has.
        std::size_t BodyCount = 0;
    };

    /// A running game or editor session.
    class Application
    {
    public:
        Application();
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        /// Brings every subsystem up.
        ///
        /// Audio and physics are best effort: a machine with no sound card, or a
        /// project with no physics, still runs. Everything else must succeed.
        Result<void> Initialise();

        /// Shuts every subsystem down. Safe to call more than once.
        void Shutdown();

        [[nodiscard]] bool IsInitialised() const noexcept { return m_Initialised; }

        [[nodiscard]] World& GetWorld() noexcept { return m_World; }
        [[nodiscard]] const World& GetWorld() const noexcept { return m_World; }

        [[nodiscard]] ScriptEngine& GetScriptEngine() noexcept { return m_Scripts; }
        [[nodiscard]] PhysicsWorld& GetPhysics() noexcept { return m_Physics; }
        [[nodiscard]] AudioDevice& GetAudio() noexcept { return m_Audio; }
        [[nodiscard]] RenderQueue& GetRenderQueue() noexcept { return m_RenderQueue; }

        /// The camera the frame renders from.
        [[nodiscard]] Camera& GetCamera() noexcept { return m_Camera; }

        /// Where scripts are loaded from.
        void SetScriptDirectory(std::string directory) { m_Scripts.SetScriptDirectory(std::move(directory)); }

        /// Caps how much time one frame may advance.
        ///
        /// A frame that takes longer than this advances the simulation by this
        /// much instead. Without it, a stall of a few seconds turns into a
        /// simulation that has to catch all of it up, and everything tunnels.
        void SetMaximumDeltaTime(float seconds) { m_MaximumDeltaTime = seconds; }

        /// Runs one frame.
        ///
        /// `deltaTime` is clamped to the maximum before anything is stepped, and
        /// the clamped value is what every subsystem is told, so a script's
        /// `time.delta()` agrees with the physics it is affecting.
        void RunFrame(float deltaTime);

        [[nodiscard]] const FrameStatistics& GetStatistics() const noexcept { return m_Statistics; }

        /// Loads a scene into the world and creates bodies for its colliders.
        ///
        /// Fails without changing the world if the scene cannot be loaded.
        Result<void> LoadScene(const Scene& scene);

        /// Calls `OnDestroy` on every script and clears the world.
        void UnloadScene();

        /// True when the simulation reports a problem, for a status bar.
        [[nodiscard]] bool HasSubsystemErrors() const noexcept
        {
            return !m_Scripts.GetErrors().empty();
        }

    private:
        World m_World;
        ScriptEngine m_Scripts;
        PhysicsWorld m_Physics;
        AudioDevice m_Audio;
        RenderQueue m_RenderQueue;
        Camera m_Camera;
        FrameStatistics m_Statistics;
        float m_MaximumDeltaTime = 0.25f;
        bool m_Initialised = false;
    };
}