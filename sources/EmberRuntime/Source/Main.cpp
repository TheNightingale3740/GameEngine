// Main.cpp - EmberRuntime entry point.

#include "Core/Logging/Log.h"

#include <cstdlib>
#include <string_view>

int main(int argc, char** argv)
{
    Ember::Log::Get().SetLevel(Ember::LogLevel::Info);
    EMBER_LOG_INFO("EmberRuntime starting with {} argument(s)", argc - 1);

    for (int i = 1; i < argc; ++i)
    {
        EMBER_LOG_INFO("  argument: {}", std::string_view(argv[i]));
    }

    return EXIT_SUCCESS;
}
