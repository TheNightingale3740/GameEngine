// Core/FileSystem.cpp

#include "Core/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>

#include "Core/Logging/Log.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace Ember::FileSystem
{
    namespace
    {
        Result<void> OpenForRead(const FilePath& path, std::ifstream& stream)
        {
            if (!std::filesystem::exists(path))
            {
                return {ErrorCode::FileNotFound, "File not found: " + ToString(path)};
            }

            if (std::filesystem::is_directory(path))
            {
                return {ErrorCode::FileRead, "Path is a directory, not a file: " + ToString(path)};
            }

            stream.open(path, std::ios::binary);
            if (!stream.is_open())
            {
                return {ErrorCode::FileRead, "Could not open file for reading: " + ToString(path)};
            }

            return {};
        }
    }

    Result<std::string> ReadTextFile(const FilePath& path)
    {
        std::ifstream stream;
        if (Result<void> openResult = OpenForRead(path, stream); openResult.IsFailure())
        {
            return openResult.GetError();
        }

        std::string contents;
        try
        {
            stream.seekg(0, std::ios::end);
            const std::streampos size = stream.tellg();
            stream.seekg(0, std::ios::beg);

            if (size > 0)
            {
                contents.resize(static_cast<std::size_t>(size));
                stream.read(contents.data(), size);
                contents.resize(static_cast<std::size_t>(stream.gcount()));
            }
        }
        catch (const std::exception& exception)
        {
            return {ErrorCode::FileRead, std::string("Could not read file: ") + exception.what()};
        }

        return contents;
    }

    Result<std::vector<std::uint8_t>> ReadBinaryFile(const FilePath& path)
    {
        std::ifstream stream;
        if (Result<void> openResult = OpenForRead(path, stream); openResult.IsFailure())
        {
            return openResult.GetError();
        }

        std::vector<std::uint8_t> contents;
        try
        {
            stream.seekg(0, std::ios::end);
            const std::streampos size = stream.tellg();
            stream.seekg(0, std::ios::beg);

            if (size > 0)
            {
                contents.resize(static_cast<std::size_t>(size));
                stream.read(reinterpret_cast<char*>(contents.data()), size);
                contents.resize(static_cast<std::size_t>(stream.gcount()));
            }
        }
        catch (const std::exception& exception)
        {
            return {ErrorCode::FileRead, std::string("Could not read file: ") + exception.what()};
        }

        return contents;
    }

    Result<void> WriteTextFile(const FilePath& path, std::string_view contents)
    {
        return WriteBinaryFile(path, contents.data(), contents.size());
    }

    Result<void> WriteBinaryFile(const FilePath& path, const void* data, std::size_t size)
    {
        const FilePath parent = path.parent_path();
        if (!parent.empty())
        {
            if (Result<void> createResult = CreateDirectories(parent); createResult.IsFailure())
            {
                return createResult;
            }
        }

        std::ofstream stream;
        try
        {
            stream.open(path, std::ios::binary | std::ios::trunc);
        }
        catch (const std::exception& exception)
        {
            return {ErrorCode::FileWrite, std::string("Could not open file for writing: ") + exception.what()};
        }

        if (!stream.is_open())
        {
            return {ErrorCode::FileWrite, "Could not open file for writing: " + ToString(path)};
        }

        if (size > 0)
        {
            stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }

        stream.flush();
        if (!stream.good())
        {
            return {ErrorCode::FileWrite, "Could not write file: " + ToString(path)};
        }

        return {};
    }

    bool Exists(const FilePath& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error) && !error;
    }

    Result<void> CreateDirectories(const FilePath& path)
    {
        if (path.empty())
        {
            return {};
        }

        std::error_code error;
        std::filesystem::create_directories(path, error);

        if (error && !std::filesystem::is_directory(path))
        {
            return {ErrorCode::FileWrite, "Could not create directory '" + ToString(path) + "': " + error.message()};
        }

        return {};
    }

    Result<std::vector<FilePath>> ListFiles(const FilePath& directory)
    {
        std::vector<FilePath> files;

        std::error_code error;
        if (!std::filesystem::is_directory(directory, error))
        {
            return {ErrorCode::FileNotFound, "Not a directory: " + ToString(directory)};
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory, error))
        {
            if (entry.is_regular_file(error))
            {
                files.push_back(entry.path());
            }
        }

        if (error)
        {
            return {ErrorCode::FileRead, "Could not list directory '" + ToString(directory) + "': " + error.message()};
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    Result<std::vector<FilePath>> ListDirectories(const FilePath& directory)
    {
        std::vector<FilePath> directories;

        std::error_code error;
        if (!std::filesystem::is_directory(directory, error))
        {
            return {ErrorCode::FileNotFound, "Not a directory: " + ToString(directory)};
        }

        for (const auto& entry : std::filesystem::directory_iterator(directory, error))
        {
            if (entry.is_directory(error))
            {
                directories.push_back(entry.path());
            }
        }

        if (error)
        {
            return {ErrorCode::FileRead, "Could not list directory '" + ToString(directory) + "': " + error.message()};
        }

        std::sort(directories.begin(), directories.end());
        return directories;
    }

    std::string GetExtension(const FilePath& path)
    {
        std::string extension = path.extension().string();
        if (!extension.empty() && extension.front() == '.')
        {
            extension.erase(extension.begin());
        }

        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

        return extension;
    }

    FilePath GetExecutableDirectory()
    {
#if defined(_WIN32)
        std::wstring buffer(MAX_PATH, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length == buffer.size())
        {
            return FileSystem::GetCurrentWorkingDirectory();
        }

        buffer.resize(length);
        return FilePath(buffer).parent_path();

#elif defined(__APPLE__)
        std::string buffer(1024, '\0');
        uint32_t size = static_cast<uint32_t>(buffer.size());
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        {
            return FileSystem::GetCurrentWorkingDirectory();
        }

        buffer.resize(std::strlen(buffer.c_str()));
        std::error_code error;
        FilePath resolved = std::filesystem::weakly_canonical(FilePath(buffer), error);
        return error ? FilePath(buffer).parent_path() : resolved.parent_path();

#else
        const FilePath self("/proc/self/exe");
        std::error_code error;
        FilePath resolved = std::filesystem::read_symlink(self, error);
        return error ? FilePath(".") : resolved.parent_path();
#endif
    }

    FilePath GetCurrentWorkingDirectory()
    {
        std::error_code error;
        FilePath path = std::filesystem::current_path(error);
        return error ? FilePath(".") : path;
    }

    std::string ToString(const FilePath& path)
    {
        std::string text = path.generic_string();
        std::replace(text.begin(), text.end(), '\\', '/');
        return text;
    }
}
