#include <gtest/gtest.h>

#include <limits>

#include "Core/Json.h"

using namespace Ember;

namespace
{
    JsonValue ParseOrDie(std::string_view text)
    {
        Result<JsonValue> result = Json::Parse(text);
        EXPECT_TRUE(result.IsSuccess()) << (result.IsFailure() ? result.GetError().Message : std::string());
        return result.ValueOr(JsonValue());
    }
}

TEST(JsonTest, ParsesNullAndBooleans)
{
    EXPECT_TRUE(ParseOrDie("null").IsNull());
    EXPECT_TRUE(ParseOrDie("true").AsBool());
    EXPECT_FALSE(ParseOrDie("false").AsBool());
}

TEST(JsonTest, ParsesIntegersAsNumbers)
{
    const JsonValue value = ParseOrDie("42");

    ASSERT_TRUE(value.IsNumber());
    EXPECT_DOUBLE_EQ(value.AsNumber(), 42.0);
    EXPECT_EQ(value.AsInt(), 42);
}

TEST(JsonTest, ParsesNegativeAndFractionalNumbers)
{
    EXPECT_DOUBLE_EQ(ParseOrDie("-17").AsNumber(), -17.0);
    EXPECT_DOUBLE_EQ(ParseOrDie("3.5").AsNumber(), 3.5);
    EXPECT_DOUBLE_EQ(ParseOrDie("-0.25").AsNumber(), -0.25);
}

TEST(JsonTest, ParsesExponentNotation)
{
    EXPECT_DOUBLE_EQ(ParseOrDie("1e3").AsNumber(), 1000.0);
    EXPECT_DOUBLE_EQ(ParseOrDie("1.5E-2").AsNumber(), 0.015);
    EXPECT_DOUBLE_EQ(ParseOrDie("-2e+2").AsNumber(), -200.0);
}

TEST(JsonTest, ParsesStrings)
{
    EXPECT_EQ(ParseOrDie("\"hello\"").AsString(), "hello");
    EXPECT_EQ(ParseOrDie("\"\"").AsString(), "");
}

TEST(JsonTest, ParsesEscapeSequences)
{
    EXPECT_EQ(ParseOrDie("\"a\\nb\"").AsString(), "a\nb");
    EXPECT_EQ(ParseOrDie("\"tab\\there\"").AsString(), "tab\there");
    EXPECT_EQ(ParseOrDie("\"quote\\\"inside\"").AsString(), "quote\"inside");
    EXPECT_EQ(ParseOrDie("\"back\\\\slash\"").AsString(), "back\\slash");
    EXPECT_EQ(ParseOrDie("\"\\u0041\"").AsString(), "A");
}

TEST(JsonTest, DecodesUtf8FromUnicodeEscapes)
{
    // U+00E9 is two UTF-8 bytes; U+1F600 is a surrogate pair encoded as four.
    const std::string twoByte = ParseOrDie("\"\\u00e9\"").AsString();
    ASSERT_EQ(twoByte.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(twoByte[0]), 0xC3u);
    EXPECT_EQ(static_cast<unsigned char>(twoByte[1]), 0xA9u);

    const std::string fourByte = ParseOrDie("\"\\ud83d\\ude00\"").AsString();
    ASSERT_EQ(fourByte.size(), 4u);
    EXPECT_EQ(static_cast<unsigned char>(fourByte[0]), 0xF0u);
}

TEST(JsonTest, RejectsUnpairedSurrogate)
{
    EXPECT_TRUE(Json::Parse("\"\\ud83d\"").IsFailure());
    EXPECT_TRUE(Json::Parse("\"\\ud83d\\u0041\"").IsFailure());
}

TEST(JsonTest, ParsesEmptyContainers)
{
    EXPECT_TRUE(ParseOrDie("[]").IsArray());
    EXPECT_EQ(ParseOrDie("[]").Size(), 0u);
    EXPECT_TRUE(ParseOrDie("{}").IsObject());
    EXPECT_EQ(ParseOrDie("{}").Size(), 0u);
}

TEST(JsonTest, ParsesArrays)
{
    const JsonValue value = ParseOrDie("[1, 2.5, \"three\", true, null]");

    ASSERT_TRUE(value.IsArray());
    ASSERT_EQ(value.Size(), 5u);
    EXPECT_DOUBLE_EQ(value[0].AsNumber(), 1.0);
    EXPECT_DOUBLE_EQ(value[1].AsNumber(), 2.5);
    EXPECT_EQ(value[2].AsString(), "three");
    EXPECT_TRUE(value[3].AsBool());
    EXPECT_TRUE(value[4].IsNull());
}

TEST(JsonTest, ParsesNestedObjects)
{
    const JsonValue value = ParseOrDie(R"({"a": {"b": {"c": 7}}})");

    ASSERT_TRUE(value["a"]["b"]["c"].IsNumber());
    EXPECT_EQ(value["a"]["b"]["c"].AsInt(), 7);
}

TEST(JsonTest, IgnoresInsignificantWhitespace)
{
    const JsonValue value = ParseOrDie("  {\n\t\"a\" :\r\n [ 1 , 2 ]  }  ");

    ASSERT_TRUE(value.IsObject());
    ASSERT_EQ(value["a"].Size(), 2u);
    EXPECT_DOUBLE_EQ(value["a"][1].AsNumber(), 2.0);
}

TEST(JsonTest, ReportsErrorPosition)
{
    const Result<JsonValue> result = Json::Parse("{\n  \"a\": 1,\n  \"b\": }\n}");

    ASSERT_TRUE(result.IsFailure());
    EXPECT_EQ(result.GetError().Code, ErrorCode::ParseError);
    EXPECT_NE(result.GetError().Message.find("line 3"), std::string::npos);
}

TEST(JsonTest, RejectsMalformedDocuments)
{
    EXPECT_TRUE(Json::Parse("").IsFailure());
    EXPECT_TRUE(Json::Parse("{").IsFailure());
    EXPECT_TRUE(Json::Parse("{\"a\" 1}").IsFailure());
    EXPECT_TRUE(Json::Parse("{a: 1}").IsFailure());
    EXPECT_TRUE(Json::Parse("[1, 2").IsFailure());
    EXPECT_TRUE(Json::Parse("[1,]").IsFailure());
    EXPECT_TRUE(Json::Parse("{\"a\": 1,}").IsFailure());
    EXPECT_TRUE(Json::Parse("\"unterminated").IsFailure());
    EXPECT_TRUE(Json::Parse("tru").IsFailure());
    EXPECT_TRUE(Json::Parse("{} extra").IsFailure());
    EXPECT_TRUE(Json::Parse("\"\\q\"").IsFailure());
}

TEST(JsonTest, TypeMismatchReturnsFallback)
{
    const JsonValue string = ParseOrDie("\"not a number\"");
    EXPECT_DOUBLE_EQ(string.AsNumber(-1.0), -1.0);
    EXPECT_EQ(string.AsInt(-7), -7);
    EXPECT_EQ(string.Size(), 0u);
    EXPECT_FALSE(string.IsArray());
    EXPECT_TRUE(string.AsArray().empty());

    const JsonValue number = ParseOrDie("42");
    EXPECT_TRUE(number.AsString().empty());
    EXPECT_DOUBLE_EQ(number.AsNumber(9.0), 42.0);
}

TEST(JsonTest, MissingMemberReturnsNull)
{
    const JsonValue value = ParseOrDie(R"({"a": 1})");

    EXPECT_TRUE(value["missing"].IsNull());
    EXPECT_FALSE(value.Contains("missing"));
    EXPECT_TRUE(value.Contains("a"));
    EXPECT_EQ(value["a"].AsInt(), 1);
}

TEST(JsonTest, ArrayAccessOutOfRangeReturnsNull)
{
    const JsonValue value = ParseOrDie("[1, 2]");

    EXPECT_TRUE(value[5].IsNull());
    EXPECT_DOUBLE_EQ(value[1].AsNumber(), 2.0);
}

TEST(JsonTest, MutableAccessCreatesMembers)
{
    JsonValue value;

    value["name"] = JsonValue("cube");
    value["count"] = JsonValue(3.0);

    EXPECT_EQ(value["name"].AsString(), "cube");
    EXPECT_DOUBLE_EQ(value["count"].AsNumber(), 3.0);
    EXPECT_EQ(value.Size(), 2u);
}

TEST(JsonTest, SetReplacesExistingMemberInPlace)
{
    JsonValue value = Json::Object({{"a", JsonValue(1.0)}});

    value.Set("a", JsonValue(2.0));

    EXPECT_EQ(value.Size(), 1u);
    EXPECT_DOUBLE_EQ(value["a"].AsNumber(), 2.0);
}

TEST(JsonTest, PushAppendsToArray)
{
    JsonValue value;

    value.Push(JsonValue(1.0));
    value.Push(JsonValue(2.0));

    ASSERT_TRUE(value.IsArray());
    EXPECT_EQ(value.Size(), 2u);
}

TEST(JsonTest, PreservesObjectMemberOrder)
{
    const JsonValue value = ParseOrDie(R"({"zebra": 1, "apple": 2, "mango": 3})");

    const JsonValue::Object& members = value.AsObject();
    ASSERT_EQ(members.size(), 3u);
    EXPECT_EQ(members[0].first, "zebra");
    EXPECT_EQ(members[1].first, "apple");
    EXPECT_EQ(members[2].first, "mango");
}

TEST(JsonTest, ReadsVectorsFromObjects)
{
    const JsonValue value = ParseOrDie(R"({"v": {"x": 1, "y": 2, "z": 3}})");

    EXPECT_TRUE(glm::all(glm::epsilonEqual(value["v"].AsVec3(), Vec3(1.0f, 2.0f, 3.0f), 1e-6f)));
}

TEST(JsonTest, ReadsVectorsFromArrays)
{
    const JsonValue value = ParseOrDie(R"({"v": [4, 5, 6]})");

    EXPECT_TRUE(glm::all(glm::epsilonEqual(value["v"].AsVec3(), Vec3(4.0f, 5.0f, 6.0f), 1e-6f)));
}

TEST(JsonTest, ReadsQuaternionInXyzwOrder)
{
    const JsonValue value = ParseOrDie(R"({"q": {"x": 1, "y": 2, "z": 3, "w": 4}})");
    const Quat quaternion = value["q"].AsQuat();

    EXPECT_FLOAT_EQ(quaternion.x, 1.0f);
    EXPECT_FLOAT_EQ(quaternion.y, 2.0f);
    EXPECT_FLOAT_EQ(quaternion.z, 3.0f);
    EXPECT_FLOAT_EQ(quaternion.w, 4.0f);
}

TEST(JsonTest, SerialisesValues)
{
    EXPECT_EQ(JsonValue(nullptr).ToString(), "null");
    EXPECT_EQ(JsonValue(true).ToString(), "true");
    EXPECT_EQ(JsonValue(false).ToString(), "false");
    EXPECT_EQ(JsonValue("hi").ToString(), "\"hi\"");
    EXPECT_EQ(JsonValue(42.0).ToString(), "42");
}

TEST(JsonTest, SerialisesEmptyContainersCompactly)
{
    EXPECT_EQ(JsonValue(JsonValue::Array{}).ToString(), "[]");
    EXPECT_EQ(JsonValue(JsonValue::Object{}).ToString(), "{}");
}

TEST(JsonTest, SerialisesNonFiniteNumbersAsNull)
{
    // JSON cannot represent NaN or infinity, so the document must stay parseable.
    const double infinity = std::numeric_limits<double>::infinity();

    EXPECT_EQ(JsonValue(infinity).ToString(), "null");
    EXPECT_EQ(JsonValue(std::nan("")).ToString(), "null");
    EXPECT_TRUE(Json::Parse(JsonValue(infinity).ToString()).IsSuccess());
}

TEST(JsonTest, EscapesControlCharactersOnWrite)
{
    const std::string text = JsonValue(std::string("a\nb\tc\x01")).ToString();

    EXPECT_NE(text.find("\\n"), std::string::npos);
    EXPECT_NE(text.find("\\t"), std::string::npos);
    EXPECT_NE(text.find("\\u0001"), std::string::npos);
}

TEST(JsonTest, RoundTripsThroughText)
{
    const std::string source = R"({"name":"test","value":3.5,"list":[1,2,3],"nested":{"flag":true}})";

    const JsonValue parsed = ParseOrDie(source);
    const JsonValue reparsed = ParseOrDie(parsed.ToString());

    EXPECT_EQ(reparsed["name"].AsString(), "test");
    EXPECT_DOUBLE_EQ(reparsed["value"].AsNumber(), 3.5);
    EXPECT_EQ(reparsed["list"].Size(), 3u);
    EXPECT_TRUE(reparsed["nested"]["flag"].AsBool());
}

TEST(JsonTest, PreservesStringEscapesThroughRoundTrip)
{
    const JsonValue original = JsonValue(std::string("quote\" backslash\\ newline\n tab\t"));
    const JsonValue reparsed = ParseOrDie(original.ToString());

    EXPECT_EQ(reparsed.AsString(), original.AsString());
}

TEST(JsonTest, CompactAndIndentedFormsParseIdentically)
{
    const JsonValue value = ParseOrDie(R"({"a":[1,{"b":2}],"c":"d"})");

    const JsonValue fromCompact = ParseOrDie(value.ToString(0));
    const JsonValue fromIndented = ParseOrDie(value.ToString(4));

    EXPECT_EQ(fromCompact.ToString(0), fromIndented.ToString(0));
    EXPECT_EQ(value.ToString(0), fromCompact.ToString(0));
}

TEST(JsonTest, RejectsNonStandardNonFiniteLiterals)
{
    // Standard JSON has no NaN or Infinity literals; the writer emits `null` for
    // non-finite numbers, so the reader must reject the non-standard spellings
    // rather than inventing values.
    EXPECT_TRUE(Json::Parse("NaN").IsFailure());
    EXPECT_TRUE(Json::Parse("Infinity").IsFailure());
    EXPECT_TRUE(Json::Parse("-Infinity").IsFailure());
    EXPECT_TRUE(Json::Parse("[NaN]").IsFailure());
}
