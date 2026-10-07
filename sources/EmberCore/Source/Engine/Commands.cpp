// Engine/Commands.cpp

#include "Engine/Commands.h"
#include "Engine/TestScene.h"

#include <algorithm>
#include <cctype>
#include <format>

#include "Core/FileSystem.h"
#include "Core/Logging/Log.h"
#include "Ecs/ComponentRegistry.h"
#include "Ecs/Components.h"

namespace Ember
{
    namespace
    {
        /// Splits `key=value` into its two halves.
        [[nodiscard]] Result<std::pair<std::string, std::string>> SplitField(const std::string& text)
        {
            const std::size_t separator = text.find('=');

            if (separator == std::string::npos || separator == 0)
            {
                return Error(ErrorCode::InvalidArgument, "Expected 'key=value' but got '" + text + "'");
            }

            return std::pair(text.substr(0, separator), text.substr(separator + 1));
        }

        /// Builds a JSON object from `key=value` pairs.
        ///
        /// Values are read as whatever the component's field type says, so a
        /// numeric field given "3" and a string field given "3" both work.
        [[nodiscard]] Result<JsonValue> BuildFields(const std::vector<std::string>& fields)
        {
            JsonValue object(JsonValue::Object{});

            for (const std::string& field : fields)
            {
                Result<std::pair<std::string, std::string>> split = SplitField(field);
                if (split.IsFailure())
                {
                    return split.GetError();
                }

                object.Set(split.Value().first, JsonValue(split.Value().second));
            }

            return object;
        }

        /// Finds a live entity by name.
        [[nodiscard]] Result<Entity> FindEntityIn(const World& world, const std::string& name)
        {
            for (const Entity entity : world.GetEntities())
            {
                if (world.GetName(entity) == name)
                {
                    return entity;
                }
            }

            return Error(ErrorCode::NotFound, "No entity named '" + name + "'");
        }
    }

    CommandResult CommandResult::Success(std::string output)
    {
        CommandResult result;
        result.Succeeded = true;
        result.Output = std::move(output);
        return result;
    }

    CommandResult CommandResult::Failure(std::string error)
    {
        CommandResult result;
        result.Succeeded = false;
        result.Error = std::move(error);
        return result;
    }

    std::string CommandResult::GetText() const
    {
        return Succeeded ? Output : Error;
    }

    Result<Command> ParseCommand(std::string_view line)
    {
        Command command;

        std::size_t position = 0;
        while (position < line.size())
        {
            while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])) != 0)
            {
                ++position;
            }

            if (position >= line.size())
            {
                break;
            }

            std::string argument;
            bool quoted = false;

            while (position < line.size())
            {
                const char character = line[position];

                if (character == '"')
                {
                    quoted = !quoted;
                    ++position;
                    continue;
                }

                if (!quoted && std::isspace(static_cast<unsigned char>(character)) != 0)
                {
                    break;
                }

                argument += character;
                ++position;
            }

            if (command.Name.empty())
            {
                command.Name = std::move(argument);
            }
            else
            {
                command.Arguments.push_back(std::move(argument));
            }
        }

        if (command.Name.empty())
        {
            return Error(ErrorCode::InvalidArgument, "A command needs a name");
        }

        return command;
    }

    void CommandRunner::EnsureApplication()
    {
        if (m_ApplicationStarted)
        {
            return;
        }

        m_ApplicationResult = m_Application.Initialise();
        m_ApplicationStarted = true;

        if (m_Project.IsValid())
        {
            m_Application.SetScriptDirectory(FileSystem::ToString(m_Project.GetScriptsDirectory()));
        }
    }

    Result<Entity> CommandRunner::FindEntity(const std::string& name) const
    {
        return FindEntityIn(m_Application.GetWorld(), name);
    }

    CommandResult CommandRunner::Create(const std::string& directory, const std::string& name)
    {
        Result<Project> project = Project::Create(directory, name);
        if (project.IsFailure())
        {
            return CommandResult::Failure(project.GetError().Message);
        }

        m_Project = std::move(project).Value();
        return CommandResult::Success("Created project '" + m_Project.GetSettings().Name + "'");
    }

    CommandResult CommandRunner::Open(const std::string& directory)
    {
        Result<Project> project = Project::Open(directory);
        if (project.IsFailure())
        {
            return CommandResult::Failure(project.GetError().Message);
        }

        m_Project = std::move(project).Value();
        EnsureApplication();

        return CommandResult::Success("Opened project '" + m_Project.GetSettings().Name + "'");
    }

    CommandResult CommandRunner::Save()
    {
        if (!m_Project.IsValid())
        {
            return CommandResult::Failure("No project is open");
        }

        if (Result<void> saved = m_Project.SetSettings(m_Project.GetSettings()); saved.IsFailure())
        {
            return CommandResult::Failure(saved.GetError().Message);
        }

        return CommandResult::Success("Saved");
    }

    CommandResult CommandRunner::LoadScene(const std::string& relativePath)
    {
        if (!m_Project.IsValid())
        {
            return CommandResult::Failure("No project is open");
        }

        EnsureApplication();
        if (m_ApplicationResult.IsFailure())
        {
            return CommandResult::Failure("The application did not start: " + m_ApplicationResult.GetError().Message);
        }

        Result<Scene> scene = Scene::LoadFromFile(m_Project.GetScenesDirectory() / relativePath);
        if (scene.IsFailure())
        {
            return CommandResult::Failure(scene.GetError().Message);
        }

        if (Result<void> loaded = m_Application.LoadScene(scene.Value()); loaded.IsFailure())
        {
            return CommandResult::Failure(loaded.GetError().Message);
        }

        return CommandResult::Success(std::to_string(m_Application.GetWorld().GetEntityCount()) + " entities loaded");
    }

    CommandResult CommandRunner::CreateEntity(const std::string& name)
    {
        EnsureApplication();
        if (m_ApplicationResult.IsFailure())
        {
            return CommandResult::Failure("The application did not start: " + m_ApplicationResult.GetError().Message);
        }

        if (name.empty())
        {
            return CommandResult::Failure("An entity needs a name");
        }

        World& world = m_Application.GetWorld();
        const Entity entity = world.CreateEntity(name);

        return CommandResult::Success("Created " + ToString(entity));
    }

    CommandResult CommandRunner::DestroyEntity(const std::string& name)
    {
        if (!m_ApplicationStarted)
        {
            return CommandResult::Failure("Nothing is loaded");
        }

        Result<Entity> entity = FindEntity(name);
        if (entity.IsFailure())
        {
            return CommandResult::Failure(entity.GetError().Message);
        }

        m_Application.GetWorld().DestroyEntity(entity.Value());
        return CommandResult::Success("Destroyed " + ToString(entity.Value()));
    }

    CommandResult CommandRunner::AddComponent(const std::string& entityName,
                                               const std::string& componentName,
                                               const std::vector<std::string>& fields)
    {
        EnsureApplication();
        if (m_ApplicationResult.IsFailure())
        {
            return CommandResult::Failure("The application did not start: " + m_ApplicationResult.GetError().Message);
        }

        Result<Entity> entity = FindEntity(entityName);
        if (entity.IsFailure())
        {
            return CommandResult::Failure(entity.GetError().Message);
        }

        if (ComponentRegistry::Get().FindByName(componentName) == InvalidComponentType)
        {
            return CommandResult::Failure("No component type named '" + componentName + "'");
        }

        Result<JsonValue> parsed = BuildFields(fields);
        if (parsed.IsFailure())
        {
            return CommandResult::Failure(parsed.GetError().Message);
        }

        World& world = m_Application.GetWorld();
        if (Result<void> added = world.AddComponentFromJson(entity.Value(), componentName, parsed.Value());
            added.IsFailure())
        {
            return CommandResult::Failure(added.GetError().Message);
        }

        return CommandResult::Success("Added " + componentName + " to " + entityName);
    }

    CommandResult CommandRunner::AttachScript(const std::string& entityName, const std::string& scriptPath)
    {
        EnsureApplication();
        if (m_ApplicationResult.IsFailure())
        {
            return CommandResult::Failure("The application did not start: " + m_ApplicationResult.GetError().Message);
        }

        Result<Entity> entity = FindEntity(entityName);
        if (entity.IsFailure())
        {
            return CommandResult::Failure(entity.GetError().Message);
        }

        if (scriptPath.empty())
        {
            return CommandResult::Failure("A script needs a path");
        }

        ScriptComponent script;
        script.ScriptPath = scriptPath;
        m_Application.GetWorld().AddComponent(entity.Value(), script);

        return CommandResult::Success("Attached " + scriptPath + " to " + entityName);
    }

    CommandResult CommandRunner::WriteTestScene(const std::string& sceneName)
    {
        if (!m_Project.IsValid())
        {
            return CommandResult::Failure("No project is open");
        }

        // Building a scene needs the built-in components registered, which is one
        // of the things starting the application does.
        EnsureApplication();
        if (m_ApplicationResult.IsFailure())
        {
            return CommandResult::Failure("The application did not start: " +
                                          m_ApplicationResult.GetError().Message);
        }

        const Result<std::string> script = TestScene::WriteToProject(m_Project, sceneName);
        if (script.IsFailure())
        {
            return CommandResult::Failure(script.GetError().Message);
        }

        // The runtime starts whatever the start scene names, so a freshly written
        // scene becomes the project's start scene unless one was already set to
        // something that exists.
        const FilePath startScene = m_Project.GetScenesDirectory() / m_Project.GetSettings().StartScene;
        if (!FileSystem::Exists(startScene))
        {
            ProjectSettings settings = m_Project.GetSettings();
            settings.StartScene = sceneName;

            if (Result<void> saved = m_Project.SetSettings(settings); saved.IsFailure())
            {
                return CommandResult::Failure(saved.GetError().Message);
            }
        }

        return CommandResult::Success(std::format("Wrote {} and {}", sceneName, script.Value()));
    }

    CommandResult CommandRunner::Export(const std::string& buildDirectory)
    {
        if (!m_Project.IsValid())
        {
            return CommandResult::Failure("No project is open");
        }

        Result<void> exported = m_Project.Export(buildDirectory);
        if (exported.IsFailure())
        {
            return CommandResult::Failure(exported.GetError().Message);
        }

        return CommandResult::Success("Exported to " + buildDirectory);
    }

    CommandResult CommandRunner::Run(int frames, float deltaTime)
    {
        EnsureApplication();
        if (m_ApplicationResult.IsFailure())
        {
            return CommandResult::Failure("The application did not start: " + m_ApplicationResult.GetError().Message);
        }

        if (frames <= 0)
        {
            return CommandResult::Failure("A run needs at least one frame");
        }

        const float step = deltaTime > 0.0f ? deltaTime : 1.0f / 60.0f;

        for (int frame = 0; frame < frames; ++frame)
        {
            m_Application.RunFrame(step);
        }

        return Statistics();
    }

    CommandResult CommandRunner::Statistics() const
    {
        if (!m_ApplicationStarted)
        {
            return CommandResult::Failure("Nothing has been run yet");
        }

        const FrameStatistics& statistics = m_Application.GetStatistics();

        return CommandResult::Success(std::format(
            "frame {}  elapsed {:.2f}s  entities {}  scripts {}  draws {}  bodies {}",
            statistics.FrameCount, statistics.ElapsedTime, statistics.EntityCount, statistics.ActiveScriptCount,
            statistics.DrawItemCount, statistics.BodyCount));
    }

    CommandResult CommandRunner::ListComponents()
    {
        std::string output;

        for (const ComponentTypeInfo& info : ComponentRegistry::Get().Types())
        {
            output += info.Name;
            for (const Property& property : info.Properties)
            {
                output += " " + property.Name;
            }

            output += "\n";
        }

        return CommandResult::Success(output);
    }
}