// Main.cpp - EmberEditor entry point.
//
// The editor drives the same command surface an AI agent uses, so that anything
// the editor can do is reachable from a script and testable without a window.
//
// With no arguments it prints its commands. With a script file it runs each line
// in order, stopping at the first failure. With `--interactive` it reads them
// from standard input.

#include <iostream>
#include <string>
#include <vector>

#include "Core/Logging/Log.h"
#include "Engine/Commands.h"

namespace
{
    /// The commands the editor understands, printed on request.
    void PrintUsage()
    {
        std::cout << "EmberEditor - the Ember game editor\n"
                     "\n"
                     "Usage:\n"
                     "  EmberEditor                        print this text\n"
                     "  EmberEditor <script.lua|cmd.txt>    run each line of a file in order\n"
                     "  EmberEditor --interactive          read commands from standard input\n"
                     "\n"
                     "Commands:\n"
                     "  create <directory> [name]          create a project\n"
                     "  open <directory>                   open a project\n"
                     "  save                               save the open project\n"
                     "  load-scene <file>                  load a scene from the project\n"
                     "  create-entity <name>               add an entity to the loaded scene\n"
                     "  destroy-entity <name>              remove an entity\n"
                     "  add-component <entity> <type> [k=v...]   add a component\n"
                     "  attach-script <entity> <path>      attach a Lua script\n"
                     "  write-test-scene [file]            write the scene that exercises every\n"
                     "                                     engine feature, and its script\n"
                     "  export <directory>                 export a shippable build\n"
                     "  run [frames] [delta]               run the application for a while\n"
                     "  statistics                         print the last frame's statistics\n"
                     "  list-components                    print every registered component\n"
                     "\n"
                     "Arguments containing spaces may be quoted.\n";
    }

    /// Runs one line and prints what it returned.
    void RunLine(Ember::CommandRunner& runner, const std::string& line)
    {
        Ember::Result<Ember::Command> parsed = Ember::ParseCommand(line);
        if (parsed.IsFailure())
        {
            std::cout << "error: " << parsed.GetError().Message << "\n";
            return;
        }

        const Ember::Command& command = parsed.Value();
        const std::vector<std::string>& arguments = command.Arguments;

        Ember::CommandResult result;

        if (command.Name == "create")
        {
            result = runner.Create(arguments.size() > 0 ? arguments[0] : "",
                                   arguments.size() > 1 ? arguments[1] : "");
        }
        else if (command.Name == "open")
        {
            result = runner.Open(arguments.size() > 0 ? arguments[0] : "");
        }
        else if (command.Name == "save")
        {
            result = runner.Save();
        }
        else if (command.Name == "load-scene")
        {
            result = runner.LoadScene(arguments.size() > 0 ? arguments[0] : "");
        }
        else if (command.Name == "create-entity")
        {
            result = runner.CreateEntity(arguments.size() > 0 ? arguments[0] : "");
        }
        else if (command.Name == "destroy-entity")
        {
            result = runner.DestroyEntity(arguments.size() > 0 ? arguments[0] : "");
        }
        else if (command.Name == "add-component")
        {
            const std::vector<std::string> fields(arguments.begin() + (arguments.size() > 2 ? 2 : arguments.size()),
                                                   arguments.end());
            result = runner.AddComponent(arguments.size() > 0 ? arguments[0] : "",
                                        arguments.size() > 1 ? arguments[1] : "",
                                        fields);
        }
        else if (command.Name == "attach-script")
        {
            result = runner.AttachScript(arguments.size() > 0 ? arguments[0] : "",
                                         arguments.size() > 1 ? arguments[1] : "");
        }
        else if (command.Name == "write-test-scene")
        {
            result = runner.WriteTestScene(arguments.size() > 0 ? arguments[0] : "test.ember");
        }
        else if (command.Name == "export")
        {
            result = runner.Export(arguments.size() > 0 ? arguments[0] : "");
        }
        else if (command.Name == "run")
        {
            const int frames = arguments.size() > 0 ? std::stoi(arguments[0]) : 1;
            const float delta = arguments.size() > 1 ? std::stof(arguments[1]) : 0.0f;
            result = runner.Run(frames, delta);
        }
        else if (command.Name == "statistics")
        {
            result = runner.Statistics();
        }
        else if (command.Name == "list-components")
        {
            result = Ember::CommandRunner::ListComponents();
        }
        else
        {
            result = Ember::CommandResult::Failure("No such command: " + command.Name);
        }

        if (result.Succeeded)
        {
            if (!result.Output.empty())
            {
                std::cout << result.Output << "\n";
            }
        }
        else
        {
            std::cerr << "error: " << result.Error << "\n";
        }
    }
}

int main(int argc, char** argv)
{
    Ember::Log::Get().SetLevel(Ember::LogLevel::Warn);

    if (argc < 2)
    {
        PrintUsage();
        return EXIT_SUCCESS;
    }

    const std::string argument(argv[1]);

    Ember::CommandRunner runner;

    if (argument == "--interactive")
    {
        std::string line;
        while (std::getline(std::cin, line))
        {
            RunLine(runner, line);
        }

        return EXIT_SUCCESS;
    }

    if (argument == "--help" || argument == "-h")
    {
        PrintUsage();
        return EXIT_SUCCESS;
    }

    // A script file is run line by line, so a project can record the sequence
    // that built it and replay it exactly.
    Ember::Result<std::string> script = Ember::FileSystem::ReadTextFile(argument);
    if (script.IsFailure())
    {
        std::cerr << "error: " << script.GetError().Message << "\n";
        return EXIT_FAILURE;
    }

    std::string line;
    for (std::size_t i = 0; i <= script.Value().size(); ++i)
    {
        const char character = i < script.Value().size() ? script.Value()[i] : '\n';

        if (character == '\n' || character == '\r')
        {
            if (!line.empty() && line.front() != '#')
            {
                RunLine(runner, line);
            }

            line.clear();
            continue;
        }

        line += character;
    }

    return EXIT_SUCCESS;
}