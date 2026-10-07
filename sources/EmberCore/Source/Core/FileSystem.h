// Core/FileSystem.h
//
// Filesystem helpers. All engine paths are UTF-8 strings and all filesystem
// access funnels through this interface so that path handling can be swapped or
// mocked in tests.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Result.h"

namespace Ember
{
    using FilePath = std::filesystem::path;

    namespace FileSystem
    {
        /// Reads a whole file into memory. Fails with FileNotFound or FileRead.
        [[nodiscard]] Result<std::string> ReadTextFile(const FilePath& path);

        /// Reads a whole file as raw bytes. Fails with FileNotFound or FileRead.
        [[nodiscard]] Result<std::vector<std::uint8_t>> ReadBinaryFile(const FilePath& path);

        /// Writes text to a file, replacing any existing contents.
        [[nodiscard]] Result<void> WriteTextFile(const FilePath& path, std::string_view contents);

        /// Writes raw bytes to a file, replacing any existing contents.
        [[nodiscard]] Result<void> WriteBinaryFile(const FilePath& path, const void* data, std::size_t size);

        /// Returns true if the path exists and is a regular file.
        [[nodiscard]] bool Exists(const FilePath& path);

        /// Creates a directory and all missing parents. Succeeds if it already exists.
        [[nodiscard]] Result<void> CreateDirectories(const FilePath& path);

        /// Lists the files directly inside `directory`, sorted by name.
        [[nodiscard]] Result<std::vector<FilePath>> ListFiles(const FilePath& directory);

        /// Lists the subdirectories directly inside `directory`, sorted by name.
        [[nodiscard]] Result<std::vector<FilePath>> ListDirectories(const FilePath& directory);

        /// Returns the lowercase extension of a path without the dot, or "".
        [[nodiscard]] std::string GetExtension(const FilePath& path);

        /// Returns the executable's own directory.
        [[nodiscard]] FilePath GetExecutableDirectory();

        /// Returns the directory the process was launched from.
        [[nodiscard]] FilePath GetCurrentWorkingDirectory();

        /// Normalises a path lexically and converts it to a UTF-8 string with
        /// forward slashes, which is the engine's canonical path form.
        [[nodiscard]] std::string ToString(const FilePath& path);
    }
}
