// Engine/TestScene.h
//
// The scene that exercises every engine feature.
//
// The test suite needs a scene that touches every component, every subsystem and
// the whole export pipeline at once, so that a subsystem that stops working is
// caught by a test rather than by opening the editor. It is built in code rather
// than shipped as an asset file, so that a change to the engine that breaks it
// fails the build's tests instead of failing to load a file.

#pragma once

#include <string>

#include "Core/Result.h"
#include "Engine/Project.h"
#include "Scene/Scene.h"

namespace Ember
{
    namespace TestScene
    {
        /// Builds a scene using every built-in component and both hierarchy shapes.
        ///
        /// The scene contains:
        ///   - a ground plane with a static box collider
        ///   - a camera positioned to see the ground
        ///   - a directional light casting shadows
        ///   - a point light
        ///   - a falling dynamic box that lands on the ground
        ///   - a kinematic sphere driven by a script
        ///   - a speaker playing a generated tone
        ///   - an empty parent with two children, so that the hierarchy and
        ///     enabling behaviour are both covered
        [[nodiscard]] Scene Build();

        /// Writes the scene into a project, along with a script that drives one of
        /// its entities.
        ///
        /// Returns the script's path relative to the project, which a test
        /// attaches with `ScriptComponent::ScriptPath`.
        [[nodiscard]] Result<std::string> WriteToProject(const Project& project,
                                                        const std::string& sceneName = "test.ember");

        /// The script the test scene attaches, as source.
        ///
        /// It counts frames, moves its entity and spawns another one, so that
        /// spawning, destruction and per-frame updates are all covered.
        [[nodiscard]] std::string GetScriptSource();
    }
}