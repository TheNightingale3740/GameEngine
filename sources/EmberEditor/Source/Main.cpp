// Main.cpp - EmberEditor entry point.

#include "Core/Logging/Log.h"

#include <cstdlib>

int main()
{
    Ember::Log::Get().SetLevel(Ember::LogLevel::Info);
    EMBER_LOG_INFO("EmberEditor starting");

    return EXIT_SUCCESS;
}
