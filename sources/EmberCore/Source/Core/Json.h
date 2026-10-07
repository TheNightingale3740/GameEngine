// Core/Json.h
//
// A small, dependency-free JSON reader and writer.
//
// The engine's scene, prefab and project files are JSON. A dedicated
// implementation is used rather than a third-party library so that the exact
// number formatting, key ordering and error positions are under engine control:
// scenes must round-trip byte-stably enough that saving an unmodified scene is a
// no-op on disk.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Math/Math.h"
#include "Core/Result.h"

namespace Ember
{
    /// A JSON value. Object member order is preserved so that writing a parsed
    /// document reproduces the input's ordering.
    class JsonValue
    {
    public:
        /// The JSON type of a value.
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

        using Array = std::vector<JsonValue>;
        using Member = std::pair<std::string, JsonValue>;
        using Object = std::vector<Member>;

        JsonValue();
        explicit JsonValue(std::nullptr_t);
        explicit JsonValue(bool value);
        explicit JsonValue(double value);
        explicit JsonValue(std::string value);
        explicit JsonValue(const char* value);
        explicit JsonValue(Array value);
        explicit JsonValue(Object value);

        /// Returns the value's type.
        [[nodiscard]] Type GetType() const noexcept { return m_Type; }

        [[nodiscard]] bool IsNull() const noexcept { return m_Type == Type::Null; }
        [[nodiscard]] bool IsBool() const noexcept { return m_Type == Type::Bool; }
        [[nodiscard]] bool IsNumber() const noexcept { return m_Type == Type::Number; }
        [[nodiscard]] bool IsString() const noexcept { return m_Type == Type::String; }
        [[nodiscard]] bool IsArray() const noexcept { return m_Type == Type::Array; }
        [[nodiscard]] bool IsObject() const noexcept { return m_Type == Type::Object; }

        /// Reads the contained value with a fallback for a type mismatch.
        [[nodiscard]] bool AsBool(bool fallback = false) const noexcept;
        [[nodiscard]] double AsNumber(double fallback = 0.0) const noexcept;
        [[nodiscard]] float AsFloat(float fallback = 0.0f) const noexcept;
        [[nodiscard]] std::int64_t AsInt(std::int64_t fallback = 0) const noexcept;
        [[nodiscard]] const std::string& AsString() const noexcept;
        [[nodiscard]] const Array& AsArray() const noexcept;
        [[nodiscard]] const Object& AsObject() const noexcept;

        /// Reads a Vec2 from an object with `x`/`y` members.
        [[nodiscard]] Vec2 AsVec2(const Vec2& fallback = Vec2(0.0f)) const noexcept;

        /// Reads a Vec3 from an object with `x`/`y`/`z` members.
        [[nodiscard]] Vec3 AsVec3(const Vec3& fallback = Vec3(0.0f)) const noexcept;

        /// Reads a Vec4 from an object with `x`/`y`/`z`/`w` members.
        [[nodiscard]] Vec4 AsVec4(const Vec4& fallback = Vec4(0.0f)) const noexcept;

        /// Reads a Quat from an object with `x`/`y`/`z`/`w` members.
        [[nodiscard]] Quat AsQuat(const Quat& fallback = Quat(1.0f, 0.0f, 0.0f, 0.0f)) const noexcept;

        /// Array access. Returns a static null value when out of range.
        [[nodiscard]] const JsonValue& operator[](std::size_t index) const noexcept;
        [[nodiscard]] JsonValue& operator[](std::size_t index) noexcept;

        /// Member access. Returns a static null value when absent.
        [[nodiscard]] const JsonValue& operator[](std::string_view key) const noexcept;
        [[nodiscard]] JsonValue& operator[](std::string_view key) noexcept;

        /// Returns true if an object has the given member.
        [[nodiscard]] bool Contains(std::string_view key) const noexcept;

        /// Number of elements in an array or members in an object; 0 otherwise.
        [[nodiscard]] std::size_t Size() const noexcept;

        /// Object member lookup.
        [[nodiscard]] const JsonValue* Find(std::string_view key) const noexcept;

        /// Appends to an array. Converts a null value into an empty array first.
        void Push(JsonValue value);

        /// Sets an object member, replacing any existing member with the same key.
        /// Converts a null value into an empty object first.
        void Set(std::string key, JsonValue value);

        /// Serialises to JSON. `indent` of 0 produces a single compact line.
        [[nodiscard]] std::string ToString(int indent = 2) const;

    private:
        Type m_Type = Type::Null;
        bool m_Bool = false;
        double m_Number = 0.0;
        std::string m_String;
        Array m_Array;
        Object m_Object;
    };

    namespace Json
    {
        /// Parses a document. Fails with ParseError and a line/column on bad input.
        [[nodiscard]] Result<JsonValue> Parse(std::string_view text);

        /// Convenience builder for an object literal.
        [[nodiscard]] JsonValue Object(std::initializer_list<JsonValue::Member> members);

        /// Convenience builder for an array literal.
        [[nodiscard]] JsonValue Array(std::initializer_list<JsonValue> values);
    }
}
