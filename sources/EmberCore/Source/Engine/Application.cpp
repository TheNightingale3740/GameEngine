// Engine/Application.cpp

#include "Engine/Application.h"

#include "Core/Logging/Log.h"
#include "Ecs/ComponentStorage.h"
#include "Ecs/Components.h"

namespace Ember
{
    namespace
    {
        /// Components the renderer needs registered before a world is built.
        ///
        /// Registration is explicit rather than a static initialiser because the
        /// component definitions live in a static library, where the linker would
        /// discard an object file that nothing references.
        bool EnsureComponentsRegistered()
        {
            Ecs::RegisterBuiltinComponents();
            return true;
        }
    }

    Application::Application() = default;

    Application::~Application()
    {
        Shutdown();
    }

    Result<void> Application::Initialise()
    {
        if (m_Initialised)
        {
            return {};
        }

        EnsureComponentsRegistered();

        if (Result<void> started = m_Scripts.Initialise(); started.IsFailure())
        {
            return started;
        }

        // Physics is required: a project with no colliders still wants the world
        // to exist, and a failure here means the engine cannot run at all.
        PhysicsSettings physicsSettings;
        physicsSettings.Gravity = Vec3(0.0f, -9.81f, 0.0f);

        if (Result<void> started = m_Physics.Initialise(physicsSettings); started.IsFailure())
        {
            m_Scripts.Shutdown();
            return started;
        }

        // Audio is best effort. A machine with no output device runs silently,
        // which is a supported configuration rather than a failure.
        if (Result<void> started = m_Audio.Initialise(); started.IsFailure())
        {
            EMBER_LOG_INFO("Continuing without audio: {}", started.GetError().Message);
        }

        m_Statistics = FrameStatistics{};
        m_Initialised = true;

        EMBER_LOG_INFO("Application initialised");
        return {};
    }

    void Application::Shutdown()
    {
        if (!m_Initialised)
        {
            return;
        }

        // Scripts stop first, so that a script can still read its own components
        // while shutting down and before the world they read from is gone.
        m_Scripts.Stop(m_World);
        m_Scripts.Shutdown();
        m_Physics.Shutdown();
        m_Audio.Shutdown();
        m_World.Clear();
        m_RenderQueue.Clear();

        m_Initialised = false;
    }

    void Application::RunFrame(float deltaTime)
    {
        if (!m_Initialised)
        {
            return;
        }

        const float clamped = Math::Clamp(deltaTime, 0.0f, m_MaximumDeltaTime);

        m_Statistics.DeltaTime = clamped;
        m_Statistics.ElapsedTime += static_cast<double>(clamped);
        ++m_Statistics.FrameCount;

        m_Scripts.SetElapsedTime(m_Statistics.ElapsedTime);

        // Scripts run before physics so that a velocity set this frame is
        // simulated in the same frame rather than the next one.
        m_Scripts.Start(m_World);
        m_Scripts.Update(m_World, clamped);

        m_Physics.Step(m_World, clamped);

        m_RenderQueue.Build(m_World, m_Camera);

        m_Statistics.EntityCount = m_World.GetEntityCount();
        m_Statistics.ActiveScriptCount = m_Scripts.GetActiveScriptCount();
        m_Statistics.DrawItemCount = m_RenderQueue.GetDrawItems().size();
        m_Statistics.BodyCount = m_Physics.GetBodyCount();
    }

    Result<void> Application::LoadScene(const Scene& scene)
    {
        if (Result<void> loaded = scene.LoadInto(m_World); loaded.IsFailure())
        {
            return loaded;
        }

        // Bodies are created after the world is populated, because a collider's
        // transform is read from the entity it belongs to.
        const std::size_t created = m_Physics.AddAllBodies(m_World);
        EMBER_LOG_INFO("Loaded a scene with {} entit(ies) and {} bod(ies)", m_World.GetEntityCount(), created);

        return {};
    }

    void Application::UnloadScene()
    {
        m_Scripts.Stop(m_World);
        m_World.Clear();
        m_RenderQueue.Clear();
    }
}