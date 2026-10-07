// Engine/Commands.h
//
// The command line both executables understand.
//
// The editor has to be drivable by an AI agent as well as by a person, so its
// commands are the same either way: a command name, parsed arguments, and text
// out. There is no mode in which the editor can do something a person could not,
// which is what makes it scriptable rather than merely automatable.
//
// Every command returns whether it succeeded and what it printed. Nothing here
// touches a window, so the whole surface is testable headlessly.

#pragma once

#include <string>
#include <vector>

#include "Core/Result.h"
#include "Engine/Application.h"
#include "Engine/Project.h"

namespace Ember
{
    /// A command's outcome: whether it worked, and what to show the caller.
    struct CommandResult
    {
        bool Succeeded = true;
        std::string Output;
        std::string Error;

        static CommandResult Success(std::string output = {});
        static CommandResult Failure(std::string error);

        /// The text to show, which is the error when the command failed.
        [[nodiscard]] std::string GetText() const;
    };

    /// One parsed command line.
    struct Command
    {
        std::string Name;
        std::vector<std::string> Arguments;
    };

    /// Splits a command line into a name and arguments.
    ///
    /// Double quotes group an argument containing spaces, which is what a path
    /// with a space in it needs.
    [[nodiscard]] Result<Command> ParseCommand(std::string_view line);

    /// Runs commands against a project and an application.
    class CommandRunner
    {
    public:
        CommandRunner() = default;

        /// Opens a project, creating it if the directory has none.
        [[nodiscard]] CommandResult Create(const std::string& directory, const std::string& name);
        [[nodiscard]] CommandResult Open(const std::string& directory);

        /// Saves the open project.
        [[nodiscard]] CommandResult Save();

        /// Loads a scene from the project into the application.
        [[nodiscard]] CommandResult LoadScene(const std::string& relativePath);

        /// Creates an entity in the loaded scene with the given name.
        [[nodiscard]] CommandResult CreateEntity(const std::string& name);

        /// Destroys an entity by name.
        [[nodiscard]] CommandResult DestroyEntity(const std::string& name);

        /// Adds a component to an entity by the component's registered name, with
        /// its fields given as `key=value` pairs.
        [[nodiscard]] CommandResult AddComponent(const std::string& entityName,
                                                 const std::string& componentName,
                                                 const std::vector<std::string>& fields);

        /// Attaches a script to an entity.
        [[nodiscard]] CommandResult AttachScript(const std::string& entityName, const std::string& scriptPath);

        /// Writes the scene that exercises every engine feature into the project,
        /// along with the script it drives.
        ///
        /// This is how a new project gets something to look at, and how an agent
        /// checks that every subsystem is wired up: the scene touches every
        /// component, the script drives one entity, and the physics settles.
        [[nodiscard]] CommandResult WriteTestScene(const std::string& sceneName = "test.ember");

        /// Exports the project into a build directory.
        [[nodiscard]] CommandResult Export(const std::string& buildDirectory);

        /// Runs the application for `frames` frames at a fixed step.
        [[nodiscard]] CommandResult Run(int frames, float deltaTime);

        /// Prints the frame statistics as one line of text.
        [[nodiscard]] CommandResult Statistics() const;

        /// Prints the names of the components registered in the engine.
        [[nodiscard]] static CommandResult ListComponents();

        [[nodiscard]] bool HasProject() const noexcept { return m_Project.IsValid(); }
        [[nodiscard]] const Project& GetProject() const noexcept { return m_Project; }

        /// Brings the application up, or the last failure if it could not start.
        [[nodiscard]] const Result<void>& GetApplicationResult() const noexcept { return m_ApplicationResult; }

    private:
        /// Brings the application up if it is not already.
        void EnsureApplication();

        /// Finds a live entity by name, or reports that there is none.
        [[nodiscard]] Result<Entity> FindEntity(const std::string& name) const;

        Project m_Project;
        Application m_Application;
        Result<void> m_ApplicationResult;
        bool m_ApplicationStarted = false;
    };
}