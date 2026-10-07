// Core/Logging/Log.h
//
// Engine logging. Every subsystem logs through the single global `Ember::Log`
// instance. Messages are written to stderr and, once a log file is opened,
// mirrored into that file.
//
// Log levels are ordered so that setting a level enables it and everything more
// verbose than it.

#pragma once

#include <format>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Ember
{
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
        Off = 6
    };

    /// Parses a level name. Returns false if the name is not recognised.
    bool TryParseLogLevel(std::string_view name, LogLevel& outLevel) noexcept;

    /// Returns the canonical lowercase name of a level.
    std::string_view ToString(LogLevel level) noexcept;

    /// A sink receives already-formatted log lines. Sinks must be thread safe.
    class ILogSink
    {
    public:
        virtual ~ILogSink() = default;

        virtual void Write(LogLevel level, std::string_view message) = 0;
    };

    /// Thread safe logger with a minimum level and an ordered sink list.
    class Log
    {
    public:
        /// Returns the process-wide logger.
        static Log& Get() noexcept;

        /// Messages below this level are discarded before formatting.
        void SetLevel(LogLevel level) noexcept;
        [[nodiscard]] LogLevel GetLevel() const noexcept;

        /// Appends a sink. Ownership stays with the caller.
        void AddSink(ILogSink* sink);

        /// Removes a previously added sink. No-op if the sink is not present.
        void RemoveSink(ILogSink* sink);

        /// Writes a message. Prefer the EMBER_LOG_* macros.
        void Write(LogLevel level, std::string_view message);

        /// Formats and writes a message. Prefer the EMBER_LOG_* macros.
        template <typename... Args>
        void WriteFormatted(LogLevel level, std::format_string<Args...> format, Args&&... args)
        {
            if (!IsEnabled(level))
            {
                return;
            }

            Write(level, std::format(format, std::forward<Args>(args)...));
        }

        /// True when a message at this level would be emitted.
        [[nodiscard]] bool IsEnabled(LogLevel level) const noexcept;

    private:
        Log() = default;

        mutable std::mutex m_Mutex;
        LogLevel m_Level = LogLevel::Info;
        std::vector<ILogSink*> m_Sinks;
    };
}

#define EMBER_LOG_TRACE(...) ::Ember::Log::Get().WriteFormatted(::Ember::LogLevel::Trace, __VA_ARGS__)
#define EMBER_LOG_DEBUG(...) ::Ember::Log::Get().WriteFormatted(::Ember::LogLevel::Debug, __VA_ARGS__)
#define EMBER_LOG_INFO(...) ::Ember::Log::Get().WriteFormatted(::Ember::LogLevel::Info, __VA_ARGS__)
#define EMBER_LOG_WARN(...) ::Ember::Log::Get().WriteFormatted(::Ember::LogLevel::Warn, __VA_ARGS__)
#define EMBER_LOG_ERROR(...) ::Ember::Log::Get().WriteFormatted(::Ember::LogLevel::Error, __VA_ARGS__)
#define EMBER_LOG_FATAL(...) ::Ember::Log::Get().WriteFormatted(::Ember::LogLevel::Fatal, __VA_ARGS__)
