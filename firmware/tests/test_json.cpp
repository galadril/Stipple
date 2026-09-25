// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/json/Json.h"

#include <string>

#include "support/TestFramework.h"

using stipple::json::Document;
using stipple::json::Error;
using stipple::json::Limits;
using stipple::json::Token;
using stipple::json::Value;

namespace {

constexpr int kTokens = 256;

int code(Error error) {
    return static_cast<int>(error);
}

/// Owns both the token storage and the input text, so Values stay valid for the
/// lifetime of the fixture.
struct Parsed {
    Token storage[kTokens];
    std::string text;
    Document document{storage, kTokens};
    Error error = Error::None;

    explicit Parsed(std::string input, const Limits& limits = Limits{}) : text(std::move(input)) {
        error = document.parse(text, limits);
    }

    Value root() const { return document.root(); }
    bool ok() const { return error == Error::None; }
};

/// Assert that a document is rejected, without caring which error.
bool rejects(const std::string& input) {
    Token storage[kTokens];
    Document document(storage, kTokens);
    return document.parse(input) != Error::None;
}

bool accepts(const std::string& input) {
    Token storage[kTokens];
    Document document(storage, kTokens);
    return document.parse(input) == Error::None;
}

}  // namespace

// --- well-formed input -------------------------------------------------------

STIPPLE_TEST(Json, ParsesScalars) {
    STIPPLE_CHECK(accepts("true"));
    STIPPLE_CHECK(accepts("false"));
    STIPPLE_CHECK(accepts("null"));
    STIPPLE_CHECK(accepts("0"));
    STIPPLE_CHECK(accepts("-1.5e10"));
    STIPPLE_CHECK(accepts("\"text\""));
}

STIPPLE_TEST(Json, ParsesEmptyContainers) {
    Parsed object("{}");
    STIPPLE_CHECK(object.ok());
    STIPPLE_CHECK(object.root().isObject());
    STIPPLE_CHECK_EQ(object.root().size(), 0);

    Parsed array("[]");
    STIPPLE_CHECK(array.ok());
    STIPPLE_CHECK(array.root().isArray());
    STIPPLE_CHECK_EQ(array.root().size(), 0);
}

STIPPLE_TEST(Json, ReadsObjectMembers) {
    Parsed doc(R"({"name":"living-room","duration":10,"enabled":true})");
    STIPPLE_CHECK(doc.ok());

    const Value root = doc.root();
    STIPPLE_CHECK_EQ(root.size(), 3);
    STIPPLE_CHECK_EQ(root["name"].toString(), std::string("living-room"));
    STIPPLE_CHECK_EQ(root["duration"].toInt(), std::int64_t(10));
    STIPPLE_CHECK(root["enabled"].toBool());
}

STIPPLE_TEST(Json, ReadsArrayElements) {
    Parsed doc("[10,20,30]");
    STIPPLE_CHECK(doc.ok());

    const Value root = doc.root();
    STIPPLE_CHECK_EQ(root.size(), 3);
    STIPPLE_CHECK_EQ(root[0].toInt(), std::int64_t(10));
    STIPPLE_CHECK_EQ(root[2].toInt(), std::int64_t(30));
}

STIPPLE_TEST(Json, NavigatesNestedStructures) {
    // Element access has to skip whole subtrees correctly, so nest both kinds.
    Parsed doc(R"({"a":{"b":[1,{"c":[2,3]},4]},"d":5})");
    STIPPLE_CHECK(doc.ok());

    const Value root = doc.root();
    STIPPLE_CHECK_EQ(root["d"].toInt(), std::int64_t(5));

    const Value b = root["a"]["b"];
    STIPPLE_CHECK_EQ(b.size(), 3);
    STIPPLE_CHECK_EQ(b[0].toInt(), std::int64_t(1));
    STIPPLE_CHECK_EQ(b[2].toInt(), std::int64_t(4));
    STIPPLE_CHECK_EQ(b[1]["c"][1].toInt(), std::int64_t(3));
}

STIPPLE_TEST(Json, ParsesARealisticScene) {
    // The blueprint §11 example, which is what this parser actually exists for.
    Parsed doc(R"({
      "name": "living-room",
      "duration": 10,
      "elements": [
        { "type": "icon", "x": 1, "y": 3, "icon": "thermometer" },
        { "type": "text", "rect": [11, 0, 40, 8], "text": "21.4\u00b0C", "align": "left" },
        { "type": "text", "rect": [11, 8, 40, 8], "text": "Living room", "scroll": "auto" }
      ]
    })");
    STIPPLE_CHECK(doc.ok());

    const Value elements = doc.root()["elements"];
    STIPPLE_CHECK_EQ(elements.size(), 3);
    STIPPLE_CHECK(elements[0]["type"].stringEquals("icon"));
    STIPPLE_CHECK_EQ(elements[1]["rect"][2].toInt(), std::int64_t(40));
    STIPPLE_CHECK_EQ(elements[1]["text"].toString(), std::string("21.4\xC2\xB0" "C"));
    STIPPLE_CHECK(elements[2]["scroll"].stringEquals("auto"));
}

STIPPLE_TEST(Json, IteratesObjectMembersByPosition) {
    Parsed doc(R"({"x":1,"y":2,"z":3})");
    STIPPLE_CHECK(doc.ok());

    const Value root = doc.root();
    STIPPLE_CHECK(root.keyAt(0).stringEquals("x"));
    STIPPLE_CHECK(root.keyAt(2).stringEquals("z"));
    STIPPLE_CHECK_EQ(root.valueAt(1).toInt(), std::int64_t(2));
    STIPPLE_CHECK_FALSE(root.keyAt(3).valid());
}

STIPPLE_TEST(Json, ToleratesWhitespaceEverywhere) {
    STIPPLE_CHECK(accepts("  {\n\t\"a\" : [ 1 , 2 ]\r\n}  "));
}

// --- missing and mistyped access ---------------------------------------------

STIPPLE_TEST(Json, MissingFieldsYieldFallbacks) {
    // A malformed or partial scene should degrade, not crash.
    Parsed doc(R"({"a":1})");
    const Value root = doc.root();

    STIPPLE_CHECK_FALSE(root["missing"].valid());
    STIPPLE_CHECK_EQ(root["missing"].toInt(42), std::int64_t(42));
    STIPPLE_CHECK_EQ(root["missing"].toDouble(1.5), 1.5);
    STIPPLE_CHECK(root["missing"].toBool(true));
    STIPPLE_CHECK_EQ(root["missing"].toString("fallback"), std::string("fallback"));
    STIPPLE_CHECK_EQ(root["missing"].size(), 0);
}

STIPPLE_TEST(Json, WrongTypeYieldsFallback) {
    Parsed doc(R"({"text":"hello","number":7})");
    const Value root = doc.root();

    STIPPLE_CHECK_EQ(root["text"].toInt(-1), std::int64_t(-1));
    STIPPLE_CHECK_EQ(root["number"].toString("none"), std::string("none"));
    STIPPLE_CHECK_FALSE(root["number"].toBool(false));
}

STIPPLE_TEST(Json, OutOfRangeIndexIsSafe) {
    Parsed doc("[1,2]");
    const Value root = doc.root();

    STIPPLE_CHECK_FALSE(root[-1].valid());
    STIPPLE_CHECK_FALSE(root[2].valid());
    STIPPLE_CHECK_FALSE(root[9999].valid());
}

STIPPLE_TEST(Json, DefaultConstructedValueIsInert) {
    Value nothing;
    STIPPLE_CHECK_FALSE(nothing.valid());
    STIPPLE_CHECK_EQ(nothing.size(), 0);
    STIPPLE_CHECK_FALSE(nothing["any"].valid());
    STIPPLE_CHECK_FALSE(nothing[0].valid());
    STIPPLE_CHECK_EQ(nothing.toInt(5), std::int64_t(5));
}

// --- strictness --------------------------------------------------------------

STIPPLE_TEST(Json, RejectsTrailingCommas) {
    STIPPLE_CHECK(rejects("[1,2,]"));
    STIPPLE_CHECK(rejects(R"({"a":1,})"));
}

STIPPLE_TEST(Json, RejectsCommentsAndSingleQuotes) {
    STIPPLE_CHECK(rejects("{} // comment"));
    STIPPLE_CHECK(rejects("/* c */ {}"));
    STIPPLE_CHECK(rejects("{'a':1}"));
}

STIPPLE_TEST(Json, RejectsUnquotedKeys) {
    STIPPLE_CHECK(rejects("{a:1}"));
}

STIPPLE_TEST(Json, RejectsNonFiniteLiterals) {
    // Accepting these would mean the device and every client disagree about
    // what a scene means.
    STIPPLE_CHECK(rejects("NaN"));
    STIPPLE_CHECK(rejects("Infinity"));
    STIPPLE_CHECK(rejects("-Infinity"));
}

STIPPLE_TEST(Json, RejectsMalformedNumbers) {
    STIPPLE_CHECK(rejects("01"));       // leading zero
    STIPPLE_CHECK(rejects("+1"));       // leading plus
    STIPPLE_CHECK(rejects(".5"));       // no integer part
    STIPPLE_CHECK(rejects("5."));       // no fraction digits
    STIPPLE_CHECK(rejects("1e"));       // no exponent digits
    STIPPLE_CHECK(rejects("1e+"));
    STIPPLE_CHECK(rejects("-"));
    STIPPLE_CHECK(accepts("0"));
    STIPPLE_CHECK(accepts("-0"));
    STIPPLE_CHECK(accepts("1e-3"));
}

STIPPLE_TEST(Json, RejectsTrailingContent) {
    Parsed doc("{} {}");
    STIPPLE_CHECK_EQ(code(doc.error), code(Error::TrailingContent));
}

STIPPLE_TEST(Json, RejectsEmptyInput) {
    STIPPLE_CHECK(rejects(""));
    STIPPLE_CHECK(rejects("   "));
}

STIPPLE_TEST(Json, RejectsUnterminatedContainers) {
    STIPPLE_CHECK(rejects("["));
    STIPPLE_CHECK(rejects("[1"));
    STIPPLE_CHECK(rejects("{"));
    STIPPLE_CHECK(rejects(R"({"a")"));
    STIPPLE_CHECK(rejects(R"({"a":)"));
    STIPPLE_CHECK(rejects(R"({"a":1)"));
}

STIPPLE_TEST(Json, RejectsBadStrings) {
    STIPPLE_CHECK(rejects("\"unterminated"));
    STIPPLE_CHECK(rejects("\"raw\nnewline\""));   // control character
    STIPPLE_CHECK(rejects("\"bad\\xescape\""));
    STIPPLE_CHECK(rejects("\"\\u12\""));          // short hex
    STIPPLE_CHECK(rejects("\"\\uZZZZ\""));
}

STIPPLE_TEST(Json, RejectsBadLiterals) {
    STIPPLE_CHECK(rejects("tru"));
    STIPPLE_CHECK(rejects("TRUE"));
    STIPPLE_CHECK(rejects("nul"));
}

// --- bounds ------------------------------------------------------------------

STIPPLE_TEST(Json, EnforcesDepthLimit) {
    // The classic JSON denial of service: nesting deep enough to exhaust the
    // stack. It must be refused, not survived by luck.
    std::string deep;
    for (int i = 0; i < 200; ++i) {
        deep += '[';
    }
    for (int i = 0; i < 200; ++i) {
        deep += ']';
    }

    Parsed doc(deep);
    STIPPLE_CHECK_EQ(code(doc.error), code(Error::DepthExceeded));
}

STIPPLE_TEST(Json, DepthLimitIsConfigurable) {
    Limits shallow;
    shallow.maxDepth = 2;

    // maxDepth counts nested containers: two are allowed, the third is not.
    STIPPLE_CHECK_EQ(code(Parsed("[1]", shallow).error), code(Error::None));
    STIPPLE_CHECK_EQ(code(Parsed("[[1]]", shallow).error), code(Error::None));
    STIPPLE_CHECK_EQ(code(Parsed("[[[1]]]", shallow).error), code(Error::DepthExceeded));
    STIPPLE_CHECK_EQ(code(Parsed(R"({"a":{"b":{"c":1}}})", shallow).error),
                    code(Error::DepthExceeded));
}

STIPPLE_TEST(Json, EnforcesTokenLimit) {
    Token storage[4];
    Document document(storage, 4);
    STIPPLE_CHECK_EQ(code(document.parse("[1,2,3,4,5,6,7,8]")), code(Error::TokenLimit));
}

STIPPLE_TEST(Json, EnforcesInputSizeLimit) {
    Limits tiny;
    tiny.maxInputBytes = 8;
    STIPPLE_CHECK_EQ(code(Parsed(R"({"aaaaaaaaaa":1})", tiny).error), code(Error::InputTooLarge));
}

STIPPLE_TEST(Json, ReportsWhereParsingFailed) {
    Parsed doc("[1, 2, x]");
    STIPPLE_CHECK_FALSE(doc.ok());
    STIPPLE_CHECK_EQ(doc.document.errorOffset(), std::size_t(7));
}

STIPPLE_TEST(Json, EveryErrorHasADescription) {
    for (int i = 0; i <= static_cast<int>(Error::TrailingContent); ++i) {
        const char* text = stipple::json::describe(static_cast<Error>(i));
        STIPPLE_CHECK(text != nullptr && text[0] != '\0');
    }
}

// --- robustness --------------------------------------------------------------

STIPPLE_TEST(Json, EveryTruncationOfAValidDocumentTerminates) {
    // Truncation is what a dropped connection looks like. No prefix may hang,
    // read out of bounds, or report success.
    const std::string full =
        R"({"name":"a","elements":[{"type":"text","rect":[1,2,3,4],"text":"hi\u00b0"}],"n":-1.5e3})";

    for (std::size_t length = 0; length < full.size(); ++length) {
        Token storage[kTokens];
        Document document(storage, kTokens);
        const Error error = document.parse(full.substr(0, length));
        STIPPLE_CHECK(error != Error::None);
    }

    STIPPLE_CHECK(accepts(full));
}

STIPPLE_TEST(Json, HandlesEveryByteValueInsideAString) {
    // Only the printable range is legal raw; the rest must be rejected rather
    // than smuggled through.
    for (int byte = 1; byte < 256; ++byte) {
        std::string input = "\"";
        input += static_cast<char>(byte);
        input += "\"";

        Token storage[kTokens];
        Document document(storage, kTokens);
        const Error error = document.parse(input);

        if (byte < 0x20) {
            STIPPLE_CHECK(error != Error::None);
        } else if (byte != '"' && byte != '\\') {
            STIPPLE_CHECK_EQ(code(error), code(Error::None));
        }
    }
}

// --- escapes -----------------------------------------------------------------

STIPPLE_TEST(Json, DecodesSimpleEscapes) {
    Parsed doc(R"(["a\"b","c\\d","e\/f","g\nh","i\tj"])");
    STIPPLE_CHECK(doc.ok());

    const Value root = doc.root();
    STIPPLE_CHECK_EQ(root[0].toString(), std::string("a\"b"));
    STIPPLE_CHECK_EQ(root[1].toString(), std::string("c\\d"));
    STIPPLE_CHECK_EQ(root[2].toString(), std::string("e/f"));
    STIPPLE_CHECK_EQ(root[3].toString(), std::string("g\nh"));
    STIPPLE_CHECK_EQ(root[4].toString(), std::string("i\tj"));
}

STIPPLE_TEST(Json, DecodesUnicodeEscapes) {
    Parsed doc(R"(["\u0041","\u00b0","\u20ac"])");
    STIPPLE_CHECK(doc.ok());

    const Value root = doc.root();
    STIPPLE_CHECK_EQ(root[0].toString(), std::string("A"));
    STIPPLE_CHECK_EQ(root[1].toString(), std::string("\xC2\xB0"));       // degree
    STIPPLE_CHECK_EQ(root[2].toString(), std::string("\xE2\x82\xAC"));   // euro
}

STIPPLE_TEST(Json, CombinesSurrogatePairs) {
    Parsed doc(R"("\ud83d\ude00")");  // U+1F600
    STIPPLE_CHECK(doc.ok());
    STIPPLE_CHECK_EQ(doc.root().toString(), std::string("\xF0\x9F\x98\x80"));
}

STIPPLE_TEST(Json, LoneSurrogateBecomesReplacementCharacter) {
    // Passing it through would produce invalid UTF-8 downstream; dropping it
    // would silently shorten the text.
    Parsed high(R"("\ud83d")");
    STIPPLE_CHECK(high.ok());
    STIPPLE_CHECK_EQ(high.root().toString(), std::string("\xEF\xBF\xBD"));

    Parsed low(R"("\ude00")");
    STIPPLE_CHECK(low.ok());
    STIPPLE_CHECK_EQ(low.root().toString(), std::string("\xEF\xBF\xBD"));
}

STIPPLE_TEST(Json, StringEqualsHandlesEscapes) {
    Parsed doc(R"({"a\u0062c":1})");
    STIPPLE_CHECK(doc.ok());
    STIPPLE_CHECK(doc.root().keyAt(0).stringEquals("abc"));
    STIPPLE_CHECK_FALSE(doc.root().keyAt(0).stringEquals("ab"));
    STIPPLE_CHECK_FALSE(doc.root().keyAt(0).stringEquals("abcd"));
    STIPPLE_CHECK_EQ(doc.root()["abc"].toInt(), std::int64_t(1));
}

STIPPLE_TEST(Json, RawKeepsEscapesIntact) {
    Parsed doc(R"("a\nb")");
    STIPPLE_CHECK_EQ(std::string(doc.root().raw()), std::string("a\\nb"));
}

// --- numbers -----------------------------------------------------------------

STIPPLE_TEST(Json, ParsesIntegerExtremes) {
    Parsed maxValue("9223372036854775807");
    STIPPLE_CHECK_EQ(maxValue.root().toInt(), std::int64_t(9223372036854775807ll));

    Parsed minValue("-9223372036854775808");
    STIPPLE_CHECK_EQ(minValue.root().toInt(), std::int64_t(-9223372036854775807ll - 1));
}

STIPPLE_TEST(Json, IntegerOverflowReportsFallbackRatherThanWrapping) {
    // Wrapping would turn a hostile payload into a plausible-looking number.
    Parsed doc("99999999999999999999999");
    STIPPLE_CHECK(doc.ok());
    STIPPLE_CHECK_EQ(doc.root().toInt(-1), std::int64_t(-1));
}

STIPPLE_TEST(Json, IntegralFloatsConvertToInt) {
    STIPPLE_CHECK_EQ(Parsed("10.0").root().toInt(-1), std::int64_t(10));
    STIPPLE_CHECK_EQ(Parsed("1e3").root().toInt(-1), std::int64_t(1000));
}

STIPPLE_TEST(Json, NonIntegralFloatsDoNotSilentlyTruncate) {
    STIPPLE_CHECK_EQ(Parsed("10.5").root().toInt(-1), std::int64_t(-1));
}

STIPPLE_TEST(Json, HugeFloatsDoNotInventPrecision) {
    // Past 2^53 a double cannot represent every integer.
    STIPPLE_CHECK_EQ(Parsed("1e300").root().toInt(-1), std::int64_t(-1));
}

STIPPLE_TEST(Json, ParsesDoubles) {
    STIPPLE_CHECK_EQ(Parsed("1.5").root().toDouble(), 1.5);
    STIPPLE_CHECK_EQ(Parsed("-0.25").root().toDouble(), -0.25);
    STIPPLE_CHECK_EQ(Parsed("2e3").root().toDouble(), 2000.0);
}

STIPPLE_TEST(Json, BooleansReadCorrectly) {
    STIPPLE_CHECK(Parsed("true").root().toBool(false));
    STIPPLE_CHECK_FALSE(Parsed("false").root().toBool(true));
    STIPPLE_CHECK(Parsed("null").root().isNull());
}
