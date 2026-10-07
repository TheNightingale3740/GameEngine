// Core/Result.h
//
// A lightweight `Result<T>` used by fallible engine APIs.
//
// The engine never throws across a module boundary. Fallible operations either
// return a `Result`, take an out-parameter plus a bool, or log a hard failure
// and return a default-constructed value. This type covers the first case.

#pragma once

#include <cassert>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace Ember
{
    /// An error is a stable, machine-readable code plus a human-readable message.
    struct Error
    {
        std::string Code;
        std::string Message;

        Error() = default;
        Error(std::string code, std::string message)
            : Code(std::move(code))
            , Message(std::move(message))
        {
        }
    };

    /// The result of an operation that either succeeded with a `T` or failed.
    ///
    /// `T` may be `void`, in which case `Result<void>` represents bare success.
    template <typename T>
    class Result
    {
    public:
        using ValueType = T;

        /// Constructs a successful result.
        Result(T value)
            : m_Data(std::in_place_index<0>, std::move(value))
        {
        }

        /// Constructs a failed result.
        Result(Error error)
            : m_Data(std::in_place_index<1>, std::move(error))
        {
        }

        /// Constructs a failed result from an error code and message.
        Result(std::string code, std::string message)
            : Result(Error(std::move(code), std::move(message)))
        {
        }

        [[nodiscard]] bool IsSuccess() const noexcept { return m_Data.index() == 0; }
        [[nodiscard]] bool IsFailure() const noexcept { return m_Data.index() == 1; }

        explicit operator bool() const noexcept { return IsSuccess(); }

        /// Returns the contained value. Behaviour is undefined unless successful.
        [[nodiscard]] T& Value() & noexcept
        {
            assert(IsSuccess() && "Result::Value called on a failed result");
            return std::get<0>(m_Data);
        }

        [[nodiscard]] const T& Value() const& noexcept
        {
            assert(IsSuccess() && "Result::Value called on a failed result");
            return std::get<0>(m_Data);
        }

        [[nodiscard]] T&& Value() && noexcept
        {
            assert(IsSuccess() && "Result::Value called on a failed result");
            return std::get<0>(std::move(m_Data));
        }

        /// Returns the error. Behaviour is undefined unless failed.
        [[nodiscard]] const Error& GetError() const noexcept
        {
            assert(IsFailure() && "Result::GetError called on a successful result");
            return std::get<1>(m_Data);
        }

        /// Returns the value on success, or `fallback` on failure.
        [[nodiscard]] T ValueOr(T fallback) const
        {
            return IsSuccess() ? std::get<0>(m_Data) : std::move(fallback);
        }

    private:
        std::variant<T, Error> m_Data;
    };

    /// Specialisation for operations that only signal success or failure.
    template <>
    class Result<void>
    {
    public:
        using ValueType = void;

        Result() = default;
        Result(Error error)
            : m_Error(std::move(error))
        {
        }

        Result(std::string code, std::string message)
            : m_Error(Error(std::move(code), std::move(message)))
        {
        }

        [[nodiscard]] bool IsSuccess() const noexcept { return !m_Error.has_value(); }
        [[nodiscard]] bool IsFailure() const noexcept { return m_Error.has_value(); }

        explicit operator bool() const noexcept { return IsSuccess(); }

        void Value() const noexcept {}

        [[nodiscard]] const Error& GetError() const noexcept
        {
            assert(IsFailure() && "Result::GetError called on a successful result");
            return *m_Error;
        }

    private:
        std::optional<Error> m_Error;
    };

    /// Error codes returned across the engine. Codes are stable identifiers that
    /// scripts and tools may match on; messages are free-form and may change.
    namespace ErrorCode
    {
        inline constexpr const char* Unknown = "E_UNKNOWN";
        inline constexpr const char* InvalidArgument = "E_INVALID_ARGUMENT";
        inline constexpr const char* FileNotFound = "E_FILE_NOT_FOUND";
        inline constexpr const char* FileRead = "E_FILE_READ";
        inline constexpr const char* FileWrite = "E_FILE_WRITE";
        inline constexpr const char* ParseError = "E_PARSE";
        inline constexpr const char* Serialization = "E_SERIALIZATION";
        inline constexpr const char* NotFound = "E_NOT_FOUND";
        inline constexpr const char* AlreadyExists = "E_ALREADY_EXISTS";
        inline constexpr const char* NotSupported = "E_NOT_SUPPORTED";
        inline constexpr const char* ScriptError = "E_SCRIPT";
        inline constexpr const char* AssetError = "E_ASSET";
        inline constexpr const char* RenderError = "E_RENDER";
        inline constexpr const char* PhysicsError = "E_PHYSICS";
        inline constexpr const char* WindowError = "E_WINDOW";
        inline constexpr const char* DeviceError = "E_DEVICE";
    }
}
