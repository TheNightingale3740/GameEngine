// Core/Logging/Log.cpp

#include "Core/Logging/Log.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace Ember
{
    namespace
    {
        constexpr std::array<std::string_view, 7> LevelNames{
            "trace", "debug", "info", "warn", "error", "fatal", "off"
        };

        /// Formats the local wall-clock time as HH:MM:SS.mmm.
        std::string FormatTimestamp()
        {
            const std::time_t now = std::time(nullptr);
            std::tm parts{};
#if defined(_WIN32)
            localtime_s(&parts, &now);
#else
            localtime_r(&now, &parts);
#endif

            std::array<char, 16> buffer{};
            const std::size_t written = std::strftime(buffer.data(), buffer.size(), "%H:%M:%S", &parts);

            std::string result(buffer.data(), written);
            result += '.';
            result += std::format("{:03}", static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count() % 1000));
            return result;
        }

        /// Default sink: writes to stderr with a coloured level tag.
        class ConsoleSink final : public ILogSink
        {
        public:
            void Write(LogLevel level, std::string_view message) override
            {
                std::fprintf(stderr, "[%s] [%-5s] %.*s\n",
                             FormatTimestamp().c_str(),
                             ToString(level).data(),
                             static_cast<int>(message.size()),
                             message.data());
            }
        };
    }

    bool TryParseLogLevel(std::string_view name, LogLevel& outLevel) noexcept
    {
        for (std::size_t i = 0; i < LevelNames.size(); ++i)
        {
            if (LevelNames[i] == name)
            {
                outLevel = static_cast<LogLevel>(i);
                return true;
            }
        }

        return false;
    }

    std::string_view ToString(LogLevel level) noexcept
    {
        const auto index = static_cast<std::size_t>(level);
        return index < LevelNames.size() ? LevelNames[index] : LevelNames[0];
    }

    Log& Log::Get() noexcept
    {
        static Log instance;
        static const bool consoleSinkInstalled = []
        {
            instance.AddSink(new ConsoleSink());
            return true;
        }();
        (void)consoleSinkInstalled;
        return instance;
    }

    void Log::SetLevel(LogLevel level) noexcept
    {
        std::lock_guard lock(m_Mutex);
        m_Level = level;
    }

    LogLevel Log::GetLevel() const noexcept
    {
        std::lock_guard lock(m_Mutex);
        return m_Level;
    }

    void Log::AddSink(ILogSink* sink)
    {
        if (sink == nullptr)
        {
            return;
        }

        std::lock_guard lock(m_Mutex);
        if (std::find(m_Sinks.begin(), m_Sinks.end(), sink) == m_Sinks.end())
        {
            m_Sinks.push_back(sink);
        }
    }

    void Log::RemoveSink(ILogSink* sink)
    {
        std::lock_guard lock(m_Mutex);
        std::erase(m_Sinks, sink);
    }

    bool Log::IsEnabled(LogLevel level) const noexcept
    {
        std::lock_guard lock(m_Mutex);
        return level >= m_Level && m_Level != LogLevel::Off;
    }

    void Log::Write(LogLevel level, std::string_view message)
    {
        std::vector<ILogSink*> sinks;
        {
            std::lock_guard lock(m_Mutex);
            if (m_Level == LogLevel::Off || level < m_Level)
            {
                return;
            }

            sinks = m_Sinks;
        }

        // Sinks are written to outside the lock: a sink is user code and may
        // itself log, which would otherwise deadlock on a non-recursive mutex.
        for (ILogSink* sink : sinks)
        {
            sink->Write(level, message);
        }
    }
}
