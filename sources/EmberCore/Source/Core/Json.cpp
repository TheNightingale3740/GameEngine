// Core/Json.cpp

#include "Core/Json.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace Ember
{
    namespace
    {
        const JsonValue& NullValue() noexcept
        {
            static const JsonValue value;
            return value;
        }

        /// Escapes a string for embedding in a JSON document.
        void AppendEscaped(std::string& out, std::string_view text)
        {
            out += '"';

            for (const char character : text)
            {
                switch (character)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (static_cast<unsigned char>(character) < 0x20)
                        {
                            char buffer[7];
                            std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(character));
                            out += buffer;
                        }
                        else
                        {
                            out += character;
                        }
                        break;
                }
            }

            out += '"';
        }

        /// Formats a double so that a round trip is exact and integers stay integral.
        std::string FormatNumber(double value)
        {
            if (std::isfinite(value))
            {
                if (value == std::floor(value) && std::abs(value) < 1e15)
                {
                    return std::format("{}", static_cast<std::int64_t>(value));
                }

                return std::format("{}", value);
            }

            // JSON has no representation for NaN or infinity. Emitting `null` keeps
            // the document parseable instead of producing a file nothing can read.
            return "null";
        }

        /// A recursive-descent JSON parser. Tracks line and column for error messages.
        class Parser
        {
        public:
            explicit Parser(std::string_view text)
                : m_Text(text)
            {
            }

            Result<JsonValue> ParseDocument()
            {
                SkipWhitespace();

                Result<JsonValue> value = ParseValue();
                if (value.IsFailure())
                {
                    return value;
                }

                SkipWhitespace();
                if (m_Position != m_Text.size())
                {
                    return MakeError("Unexpected trailing content");
                }

                return std::move(value).Value();
            }

        private:
            Error MakeError(std::string_view message) const
            {
                return Error(ErrorCode::ParseError,
                             std::string(message) + " at line " + std::to_string(m_Line) +
                                 ", column " + std::to_string(m_Column));
            }

            void SkipWhitespace() noexcept
            {
                while (m_Position < m_Text.size())
                {
                    const char character = m_Text[m_Position];
                    if (character == '\n')
                    {
                        ++m_Line;
                        m_Column = 1;
                    }
                    else if (character == ' ' || character == '\t' || character == '\r')
                    {
                        ++m_Column;
                    }
                    else
                    {
                        break;
                    }

                    ++m_Position;
                }
            }

            [[nodiscard]] char Peek() const noexcept
            {
                return m_Position < m_Text.size() ? m_Text[m_Position] : '\0';
            }

            bool Consume(char expected) noexcept
            {
                if (Peek() != expected)
                {
                    return false;
                }

                ++m_Position;
                ++m_Column;
                return true;
            }

            bool ConsumeLiteral(std::string_view literal) noexcept
            {
                if (m_Text.compare(m_Position, literal.size(), literal) != 0)
                {
                    return false;
                }

                m_Position += literal.size();
                m_Column += literal.size();
                return true;
            }

            Result<JsonValue> ParseValue()
            {
                SkipWhitespace();

                switch (Peek())
                {
                    case '\0': return MakeError("Unexpected end of input");
                    case '{': return ParseObject();
                    case '[': return ParseArray();
                    case '"': return ParseString();
                    case 't':
                        if (ConsumeLiteral("true"))
                        {
                            return JsonValue(true);
                        }
                        return MakeError("Invalid literal");

                    case 'f':
                        if (ConsumeLiteral("false"))
                        {
                            return JsonValue(false);
                        }
                        return MakeError("Invalid literal");

                    case 'n':
                        if (ConsumeLiteral("null"))
                        {
                            return JsonValue(nullptr);
                        }
                        return MakeError("Invalid literal");

                    default: return ParseNumber();
                }
            }

            Result<JsonValue> ParseObject()
            {
                Consume('{');
                SkipWhitespace();

                JsonValue::Object members;

                if (Consume('}'))
                {
                    return JsonValue(std::move(members));
                }

                while (true)
                {
                    SkipWhitespace();

                    if (Peek() != '"')
                    {
                        return MakeError("Expected a member name");
                    }

                    Result<JsonValue> name = ParseString();
                    if (name.IsFailure())
                    {
                        return name;
                    }

                    SkipWhitespace();
                    if (!Consume(':'))
                    {
                        return MakeError("Expected ':' after a member name");
                    }

                    Result<JsonValue> value = ParseValue();
                    if (value.IsFailure())
                    {
                        return value;
                    }

                    members.emplace_back(std::move(name).Value().AsString(), std::move(value).Value());

                    SkipWhitespace();
                    if (Consume(','))
                    {
                        // A trailing comma before '}' is not permitted by the grammar.
                        SkipWhitespace();
                        if (Peek() == '}')
                        {
                            return MakeError("Trailing comma in object");
                        }

                        continue;
                    }

                    if (Consume('}'))
                    {
                        return JsonValue(std::move(members));
                    }

                    return MakeError("Expected ',' or '}' in object");
                }
            }

            Result<JsonValue> ParseArray()
            {
                Consume('[');
                SkipWhitespace();

                JsonValue::Array values;

                if (Consume(']'))
                {
                    return JsonValue(std::move(values));
                }

                while (true)
                {
                    Result<JsonValue> value = ParseValue();
                    if (value.IsFailure())
                    {
                        return value;
                    }

                    values.push_back(std::move(value).Value());

                    SkipWhitespace();
                    if (Consume(','))
                    {
                        SkipWhitespace();
                        if (Peek() == ']')
                        {
                            return MakeError("Trailing comma in array");
                        }

                        continue;
                    }

                    if (Consume(']'))
                    {
                        return JsonValue(std::move(values));
                    }

                    return MakeError("Expected ',' or ']' in array");
                }
            }

            Result<JsonValue> ParseString()
            {
                Consume('"');

                std::string text;
                while (m_Position < m_Text.size())
                {
                    const char character = m_Text[m_Position++];
                    ++m_Column;

                    if (character == '"')
                    {
                        return JsonValue(std::move(text));
                    }

                    if (character != '\\')
                    {
                        text += character;
                        continue;
                    }

                    if (m_Position >= m_Text.size())
                    {
                        return MakeError("Unterminated escape sequence");
                    }

                    const char escape = m_Text[m_Position++];
                    ++m_Column;

                    switch (escape)
                    {
                        case '"': text += '"'; break;
                        case '\\': text += '\\'; break;
                        case '/': text += '/'; break;
                        case 'b': text += '\b'; break;
                        case 'f': text += '\f'; break;
                        case 'n': text += '\n'; break;
                        case 'r': text += '\r'; break;
                        case 't': text += '\t'; break;
                        case 'u':
                        {
                            if (Result<void> result = ParseUnicodeEscape(text); result.IsFailure())
                            {
                                return result.GetError();
                            }
                            break;
                        }

                        default: return MakeError("Unknown escape sequence");
                    }
                }

                return MakeError("Unterminated string");
            }

            /// Reads a \uXXXX escape, including a surrogate pair when one is present.
            Result<void> ParseUnicodeEscape(std::string& text)
            {
                std::uint32_t codePoint = 0;
                if (Result<void> result = ReadHex4(codePoint); result.IsFailure())
                {
                    return result.GetError();
                }

                if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                {
                    // High surrogate: a low surrogate must follow to form the pair.
                    if (!ConsumeLiteral("\\u"))
                    {
                        return MakeError("Unpaired UTF-16 surrogate");
                    }

                    std::uint32_t low = 0;
                    if (Result<void> result = ReadHex4(low); result.IsFailure())
                    {
                        return result.GetError();
                    }

                    if (low < 0xDC00 || low > 0xDFFF)
                    {
                        return MakeError("Invalid UTF-16 low surrogate");
                    }

                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                }

                AppendUtf8(text, codePoint);
                return {};
            }

            Result<void> ReadHex4(std::uint32_t& outValue) noexcept
            {
                if (m_Position + 4 > m_Text.size())
                {
                    return MakeError("Truncated \\u escape");
                }

                std::uint32_t value = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char character = m_Text[m_Position + static_cast<std::size_t>(i)];
                    value <<= 4;

                    if (character >= '0' && character <= '9')
                    {
                        value |= static_cast<std::uint32_t>(character - '0');
                    }
                    else if (character >= 'a' && character <= 'f')
                    {
                        value |= static_cast<std::uint32_t>(character - 'a' + 10);
                    }
                    else if (character >= 'A' && character <= 'F')
                    {
                        value |= static_cast<std::uint32_t>(character - 'A' + 10);
                    }
                    else
                    {
                        return MakeError("Invalid hexadecimal digit in \\u escape");
                    }
                }

                m_Position += 4;
                m_Column += 4;
                outValue = value;
                return {};
            }

            static void AppendUtf8(std::string& text, std::uint32_t codePoint) noexcept
            {
                if (codePoint <= 0x7F)
                {
                    text += static_cast<char>(codePoint);
                }
                else if (codePoint <= 0x7FF)
                {
                    text += static_cast<char>(0xC0 | (codePoint >> 6));
                    text += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
                else if (codePoint <= 0xFFFF)
                {
                    text += static_cast<char>(0xE0 | (codePoint >> 12));
                    text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                    text += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
                else
                {
                    text += static_cast<char>(0xF0 | (codePoint >> 18));
                    text += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                    text += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                    text += static_cast<char>(0x80 | (codePoint & 0x3F));
                }
            }

            Result<JsonValue> ParseNumber()
            {
                const std::size_t start = m_Position;

                if (Peek() == '-' || Peek() == '+')
                {
                    ++m_Position;
                    ++m_Column;
                }

                while (std::isdigit(static_cast<unsigned char>(Peek())) != 0)
                {
                    ++m_Position;
                    ++m_Column;
                }

                if (Peek() == '.')
                {
                    ++m_Position;
                    ++m_Column;
                    while (std::isdigit(static_cast<unsigned char>(Peek())) != 0)
                    {
                        ++m_Position;
                        ++m_Column;
                    }
                }

                if (Peek() == 'e' || Peek() == 'E')
                {
                    ++m_Position;
                    ++m_Column;
                    if (Peek() == '-' || Peek() == '+')
                    {
                        ++m_Position;
                        ++m_Column;
                    }
                    while (std::isdigit(static_cast<unsigned char>(Peek())) != 0)
                    {
                        ++m_Position;
                        ++m_Column;
                    }
                }

                if (m_Position == start)
                {
                    return MakeError("Expected a value");
                }

                const std::string text(m_Text.substr(start, m_Position - start));

                char* end = nullptr;
                const double value = std::strtod(text.c_str(), &end);

                if (end == nullptr || *end != '\0')
                {
                    return MakeError("Invalid number '" + text + "'");
                }

                return JsonValue(value);
            }

            std::string_view m_Text;
            std::size_t m_Position = 0;
            std::size_t m_Line = 1;
            std::size_t m_Column = 1;
        };
    }

    JsonValue::JsonValue() = default;
    JsonValue::JsonValue(std::nullptr_t) : m_Type(Type::Null) {}
    JsonValue::JsonValue(bool value) : m_Type(Type::Bool), m_Bool(value) {}
    JsonValue::JsonValue(double value) : m_Type(Type::Number), m_Number(value) {}
    JsonValue::JsonValue(std::string value) : m_Type(Type::String), m_String(std::move(value)) {}
    JsonValue::JsonValue(const char* value) : m_Type(Type::String), m_String(value != nullptr ? value : "") {}
    JsonValue::JsonValue(Array value) : m_Type(Type::Array), m_Array(std::move(value)) {}
    JsonValue::JsonValue(Object value) : m_Type(Type::Object), m_Object(std::move(value)) {}

    bool JsonValue::AsBool(bool fallback) const noexcept
    {
        if (m_Type == Type::Bool)
        {
            return m_Bool;
        }

        if (m_Type == Type::Number)
        {
            return m_Number != 0.0;
        }

        return fallback;
    }

    double JsonValue::AsNumber(double fallback) const noexcept
    {
        if (m_Type == Type::Number)
        {
            return m_Number;
        }

        if (m_Type == Type::Bool)
        {
            return m_Bool ? 1.0 : 0.0;
        }

        return fallback;
    }

    float JsonValue::AsFloat(float fallback) const noexcept
    {
        return static_cast<float>(AsNumber(static_cast<double>(fallback)));
    }

    std::int64_t JsonValue::AsInt(std::int64_t fallback) const noexcept
    {
        if (m_Type != Type::Number)
        {
            return fallback;
        }

        return static_cast<std::int64_t>(m_Number);
    }

    const std::string& JsonValue::AsString() const noexcept
    {
        static const std::string empty;
        return m_Type == Type::String ? m_String : empty;
    }

    const JsonValue::Array& JsonValue::AsArray() const noexcept
    {
        static const Array empty;
        return m_Type == Type::Array ? m_Array : empty;
    }

    const JsonValue::Object& JsonValue::AsObject() const noexcept
    {
        static const Object empty;
        return m_Type == Type::Object ? m_Object : empty;
    }

    Vec2 JsonValue::AsVec2(const Vec2& fallback) const noexcept
    {
        if (IsArray() && m_Array.size() >= 2)
        {
            return Vec2(m_Array[0].AsFloat(), m_Array[1].AsFloat());
        }

        if (IsObject())
        {
            return Vec2((*this)["x"].AsFloat(fallback.x), (*this)["y"].AsFloat(fallback.y));
        }

        return fallback;
    }

    Vec3 JsonValue::AsVec3(const Vec3& fallback) const noexcept
    {
        if (IsArray() && m_Array.size() >= 3)
        {
            return Vec3(m_Array[0].AsFloat(), m_Array[1].AsFloat(), m_Array[2].AsFloat());
        }

        if (IsObject())
        {
            return Vec3((*this)["x"].AsFloat(fallback.x),
                        (*this)["y"].AsFloat(fallback.y),
                        (*this)["z"].AsFloat(fallback.z));
        }

        return fallback;
    }

    Vec4 JsonValue::AsVec4(const Vec4& fallback) const noexcept
    {
        if (IsArray() && m_Array.size() >= 4)
        {
            return Vec4(m_Array[0].AsFloat(), m_Array[1].AsFloat(), m_Array[2].AsFloat(), m_Array[3].AsFloat());
        }

        if (IsObject())
        {
            return Vec4((*this)["x"].AsFloat(fallback.x),
                        (*this)["y"].AsFloat(fallback.y),
                        (*this)["z"].AsFloat(fallback.z),
                        (*this)["w"].AsFloat(fallback.w));
        }

        return fallback;
    }

    Quat JsonValue::AsQuat(const Quat& fallback) const noexcept
    {
        const Vec4 value = AsVec4(Vec4(fallback.x, fallback.y, fallback.z, fallback.w));
        return Quat(value.w, value.x, value.y, value.z);
    }

    const JsonValue* JsonValue::Find(std::string_view key) const noexcept
    {
        for (const Member& member : m_Object)
        {
            if (member.first == key)
            {
                return &member.second;
            }
        }

        return nullptr;
    }

    const JsonValue& JsonValue::operator[](std::string_view key) const noexcept
    {
        if (const JsonValue* found = Find(key); found != nullptr)
        {
            return *found;
        }

        return NullValue();
    }

    JsonValue& JsonValue::operator[](std::string_view key) noexcept
    {
        if (m_Type != Type::Object)
        {
            m_Type = Type::Object;
            m_Object.clear();
        }

        for (Member& member : m_Object)
        {
            if (member.first == key)
            {
                return member.second;
            }
        }

        m_Object.emplace_back(std::string(key), JsonValue());
        return m_Object.back().second;
    }

    bool JsonValue::Contains(std::string_view key) const noexcept
    {
        return Find(key) != nullptr;
    }

    const JsonValue& JsonValue::operator[](std::size_t index) const noexcept
    {
        if (m_Type != Type::Array || index >= m_Array.size())
        {
            return NullValue();
        }

        return m_Array[index];
    }

    JsonValue& JsonValue::operator[](std::size_t index) noexcept
    {
        if (m_Type != Type::Array)
        {
            m_Type = Type::Array;
            m_Array.clear();
        }

        if (index >= m_Array.size())
        {
            m_Array.resize(index + 1);
        }

        return m_Array[index];
    }

    std::size_t JsonValue::Size() const noexcept
    {
        if (m_Type == Type::Array)
        {
            return m_Array.size();
        }

        if (m_Type == Type::Object)
        {
            return m_Object.size();
        }

        return 0;
    }

    void JsonValue::Push(JsonValue value)
    {
        if (m_Type != Type::Array)
        {
            m_Type = Type::Array;
            m_Array.clear();
        }

        m_Array.push_back(std::move(value));
    }

    void JsonValue::Set(std::string key, JsonValue value)
    {
        if (m_Type != Type::Object)
        {
            m_Type = Type::Object;
            m_Object.clear();
        }

        for (Member& member : m_Object)
        {
            if (member.first == key)
            {
                member.second = std::move(value);
                return;
            }
        }

        m_Object.emplace_back(std::move(key), std::move(value));
    }

    namespace
    {
        void WriteValue(std::string& out, const JsonValue& value, int indent, int depth);

        void WriteIndent(std::string& out, int indent, int depth)
        {
            if (indent <= 0)
            {
                return;
            }

            out += '\n';
            out.append(static_cast<std::size_t>(indent * depth), ' ');
        }

        void WriteValue(std::string& out, const JsonValue& value, int indent, int depth)
        {
            switch (value.GetType())
            {
                case JsonValue::Type::Null:
                    out += "null";
                    break;

                case JsonValue::Type::Bool:
                    out += value.AsBool() ? "true" : "false";
                    break;

                case JsonValue::Type::Number:
                    out += FormatNumber(value.AsNumber());
                    break;

                case JsonValue::Type::String:
                    AppendEscaped(out, value.AsString());
                    break;

                case JsonValue::Type::Array:
                {
                    const JsonValue::Array& items = value.AsArray();
                    if (items.empty())
                    {
                        out += "[]";
                        break;
                    }

                    out += '[';
                    for (std::size_t i = 0; i < items.size(); ++i)
                    {
                        if (i > 0)
                        {
                            out += ',';
                        }

                        WriteIndent(out, indent, depth + 1);
                        WriteValue(out, items[i], indent, depth + 1);
                    }

                    WriteIndent(out, indent, depth);
                    out += ']';
                    break;
                }

                case JsonValue::Type::Object:
                {
                    const JsonValue::Object& members = value.AsObject();
                    if (members.empty())
                    {
                        out += "{}";
                        break;
                    }

                    out += '{';
                    for (std::size_t i = 0; i < members.size(); ++i)
                    {
                        if (i > 0)
                        {
                            out += ',';
                        }

                        WriteIndent(out, indent, depth + 1);
                        AppendEscaped(out, members[i].first);
                        out += ':';
                        if (indent > 0)
                        {
                            out += ' ';
                        }

                        WriteValue(out, members[i].second, indent, depth + 1);
                    }

                    WriteIndent(out, indent, depth);
                    out += '}';
                    break;
                }
            }
        }
    }

    std::string JsonValue::ToString(int indent) const
    {
        std::string out;
        WriteValue(out, *this, indent, 0);
        return out;
    }

    namespace Json
    {
        Result<JsonValue> Parse(std::string_view text)
        {
            return Parser(text).ParseDocument();
        }

        JsonValue Object(std::initializer_list<JsonValue::Member> members)
        {
            return JsonValue(JsonValue::Object(members));
        }

        JsonValue Array(std::initializer_list<JsonValue> values)
        {
            return JsonValue(JsonValue::Array(values));
        }
    }
}
