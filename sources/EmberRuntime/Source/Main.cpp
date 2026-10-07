// Main.cpp - EmberRuntime entry point.
//
// The runtime plays an exported build. It reads the export's manifest rather
// than a project file, so what ships is exactly what was exported and a project
// edited after the export cannot change the game that is already built.
//
// It contains no editing code and links no editor module, so a shipped build
// cannot be talked into changing the game it is running.

#include <cstdlib>
#include <iostream>
#include <string>

#include "Core/Logging/Log.h"
#include "Engine/Application.h"
#include "Engine/Project.h"
#include "Scene/Scene.h"

namespace
{
    /// Reads the start scene from an exported build and plays it.
    int RunBuild(const Ember::FilePath& buildDirectory, int frames, float deltaTime)
    {
        Ember::Result<Ember::JsonValue> manifest = Ember::Project::ReadManifest(buildDirectory);
        if (manifest.IsFailure())
        {
            std::cerr << "error: " << manifest.GetError().Message << "\n";
            return EXIT_FAILURE;
        }

        const Ember::JsonValue& document = manifest.Value();

        const std::string projectName = document[Ember::ProjectFormat::ManifestProjectKey].AsString();
        const std::string startScene = document[Ember::ProjectFormat::ManifestStartSceneKey].AsString();

        if (startScene.empty())
        {
            std::cerr << "error: the export names no start scene\n";
            return EXIT_FAILURE;
        }

        Ember::Application application;
        if (Ember::Result<void> started = application.Initialise(); started.IsFailure())
        {
            std::cerr << "error: " << started.GetError().Message << "\n";
            return EXIT_FAILURE;
        }

        const Ember::FilePath scenePath = buildDirectory / "scenes" / startScene;

        Ember::Result<Ember::Scene> scene = Ember::Scene::LoadFromFile(scenePath);
        if (scene.IsFailure())
        {
            std::cerr << "error: " << scene.GetError().Message << "\n";
            return EXIT_FAILURE;
        }

        if (Ember::Result<void> loaded = application.LoadScene(scene.Value()); loaded.IsFailure())
        {
            std::cerr << "error: " << loaded.GetError().Message << "\n";
            return EXIT_FAILURE;
        }

        application.SetScriptDirectory(Ember::FileSystem::ToString(buildDirectory / "scripts"));

        std::cout << projectName << " running from " << Ember::FileSystem::ToString(buildDirectory) << "\n";

        // A frame count of zero runs until the process is stopped, which is what a
        // windowed run would do. A count is what a headless test asks for.
        const int totalFrames = frames > 0 ? frames : 0;

        for (int frame = 0; totalFrames == 0 || frame < totalFrames; ++frame)
        {
            application.RunFrame(deltaTime);

            if (application.HasSubsystemErrors())
            {
                std::cerr << "error: " << application.GetScriptEngine().GetErrors().back() << "\n";
                return EXIT_FAILURE;
            }
        }

        const Ember::FrameStatistics& statistics = application.GetStatistics();
        std::cout << "ran " << statistics.FrameCount << " frame(s), " << statistics.EntityCount << " entit(ies), "
                  << statistics.ActiveScriptCount << " script(s), " << statistics.DrawItemCount << " draw(s)\n";

        return EXIT_SUCCESS;
    }

    void PrintUsage()
    {
        std::cout << "EmberRuntime - plays an exported Ember build\n"
                     "\n"
                     "Usage:\n"
                     "  EmberRuntime <build-directory> [frames] [delta]\n"
                     "\n"
                     "  frames  how many frames to run; 0 runs until stopped (default 1)\n"
                     "  delta   seconds each frame advances (default 1/60)\n";
    }
}

int main(int argc, char** argv)
{
    Ember::Log::Get().SetLevel(Ember::LogLevel::Warn);

    if (argc < 2)
    {
        PrintUsage();
        return EXIT_FAILURE;
    }

    const std::string argument(argv[1]);

    if (argument == "--help" || argument == "-h")
    {
        PrintUsage();
        return EXIT_SUCCESS;
    }

    const int frames = argc > 2 ? std::atoi(argv[2]) : 1;
    const float deltaTime = argc > 3 ? static_cast<float>(std::atof(argv[3])) : 1.0f / 60.0f;

    return RunBuild(Ember::FilePath(argument), frames, deltaTime);
}