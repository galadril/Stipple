// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/json/Json.h"

#include <string>

#include "support/TestFramework.h"

using notrix::json::Document;
using notrix::json::Error;
using notrix::json::Limits;
using notrix::json::Token;
using notrix::json::Value;

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

NOTRIX_TEST(Json, ParsesScalars) {
    NOTRIX_CHECK(accepts("true"));
    NOTRIX_CHECK(accepts("false"));
    NOTRIX_CHECK(accepts("null"));
    NOTRIX_CHECK(accepts("0"));
    NOTRIX_CHECK(accepts("-1.5e10"));
    NOTRIX_CHECK(accepts("\"text\""));
}

NOTRIX_TEST(Json, ParsesEmptyContainers) {
    Parsed object("{}");
    NOTRIX_CHECK(object.ok());
    NOTRIX_CHECK(object.root().isObject());
    NOTRIX_CHECK_EQ(object.root().size(), 0);

    Parsed array("[]");
    NOTRIX_CHECK(array.ok());
    NOTRIX_CHECK(array.root().isArray());
    NOTRIX_CHECK_EQ(array.root().size(), 0);
}

NOTRIX_TEST(Json, ReadsObjectMembers) {
    Parsed doc(R"({"name":"living-room","duration":10,"enabled":true})");
    NOTRIX_CHECK(doc.ok());

    const Value root = doc.root();
    NOTRIX_CHECK_EQ(root.size(), 3);
    NOTRIX_CHECK_EQ(root["name"].toString(), std::string("living-room"));
    NOTRIX_CHECK_EQ(root["duration"].toInt(), std::int64_t(10));
    NOTRIX_CHECK(root["enabled"].toBool());
}

NOTRIX_TEST(Json, ReadsArrayElements) {
    Parsed doc("[10,20,30]");
    NOTRIX_CHECK(doc.ok());

    const Value root = doc.root();
    NOTRIX_CHECK_EQ(root.size(), 3);
    NOTRIX_CHECK_EQ(root[0].toInt(), std::int64_t(10));
    NOTRIX_CHECK_EQ(root[2].toInt(), std::int64_t(30));
}

NOTRIX_TEST(Json, NavigatesNestedStructures) {
    // Element access has to skip whole subtrees correctly, so nest both kinds.
    Parsed doc(R"({"a":{"b":[1,{"c":[2,3]},4]},"d":5})");
    NOTRIX_CHECK(doc.ok());

    const Value root = doc.root();
    NOTRIX_CHECK_EQ(root["d"].toInt(), std::int64_t(5));

    const Value b = root["a"]["b"];
    NOTRIX_CHECK_EQ(b.size(), 3);
    NOTRIX_CHECK_EQ(b[0].toInt(), std::int64_t(1));
    NOTRIX_CHECK_EQ(b[2].toInt(), std::int64_t(4));
    NOTRIX_CHECK_EQ(b[1]["c"][1].toInt(), std::int64_t(3));
}

NOTRIX_TEST(Json, ParsesARealisticScene) {
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
    NOTRIX_CHECK(doc.ok());

    const Value elements = doc.root()["elements"];
    NOTRIX_CHECK_EQ(elements.size(), 3);
    NOTRIX_CHECK(elements[0]["type"].stringEquals("icon"));
    NOTRIX_CHECK_EQ(elements[1]["rect"][2].toInt(), std::int64_t(40));
    NOTRIX_CHECK_EQ(elements[1]["text"].toString(), std::string("21.4\xC2\xB0" "C"));
    NOTRIX_CHECK(elements[2]["scroll"].stringEquals("auto"));
}

NOTRIX_TEST(Json, IteratesObjectMembersByPosition) {
    Parsed doc(R"({"x":1,"y":2,"z":3})");
    NOTRIX_CHECK(doc.ok());

    const Value root = doc.root();
    NOTRIX_CHECK(root.keyAt(0).stringEquals("x"));
    NOTRIX_CHECK(root.keyAt(2).stringEquals("z"));
    NOTRIX_CHECK_EQ(root.valueAt(1).toInt(), std::int64_t(2));
    NOTRIX_CHECK_FALSE(root.keyAt(3).valid());
}

NOTRIX_TEST(Json, ToleratesWhitespaceEverywhere) {
    NOTRIX_CHECK(accepts("  {\n\t\"a\" : [ 1 , 2 ]\r\n}  "));
}

// --- missing and mistyped access ---------------------------------------------

NOTRIX_TEST(Json, MissingFieldsYieldFallbacks) {
    // A malformed or partial scene should degrade, not crash.
    Parsed doc(R"({"a":1})");
    const Value root = doc.root();

    NOTRIX_CHECK_FALSE(root["missing"].valid());
    NOTRIX_CHECK_EQ(root["missing"].toInt(42), std::int64_t(42));
    NOTRIX_CHECK_EQ(root["missing"].toDouble(1.5), 1.5);
    NOTRIX_CHECK(root["missing"].toBool(true));
    NOTRIX_CHECK_EQ(root["missing"].toString("fallback"), std::string("fallback"));
    NOTRIX_CHECK_EQ(root["missing"].size(), 0);
}

NOTRIX_TEST(Json, WrongTypeYieldsFallback) {
    Parsed doc(R"({"text":"hello","number":7})");
    const Value root = doc.root();

    NOTRIX_CHECK_EQ(root["text"].toInt(-1), std::int64_t(-1));
    NOTRIX_CHECK_EQ(root["number"].toString("none"), std::string("none"));
    NOTRIX_CHECK_FALSE(root["number"].toBool(false));
}

NOTRIX_TEST(Json, OutOfRangeIndexIsSafe) {
    Parsed doc("[1,2]");
    const Value root = doc.root();

    NOTRIX_CHECK_FALSE(root[-1].valid());
    NOTRIX_CHECK_FALSE(root[2].valid());
    NOTRIX_CHECK_FALSE(root[9999].valid());
}

NOTRIX_TEST(Json, DefaultConstructedValueIsInert) {
    Value nothing;
    NOTRIX_CHECK_FALSE(nothing.valid());
    NOTRIX_CHECK_EQ(nothing.size(), 0);
    NOTRIX_CHECK_FALSE(nothing["any"].valid());
    NOTRIX_CHECK_FALSE(nothing[0].valid());
    NOTRIX_CHECK_EQ(nothing.toInt(5), std::int64_t(5));
}

// --- strictness --------------------------------------------------------------

NOTRIX_TEST(Json, RejectsTrailingCommas) {
    NOTRIX_CHECK(rejects("[1,2,]"));
    NOTRIX_CHECK(rejects(R"({"a":1,})"));
}

NOTRIX_TEST(Json, RejectsCommentsAndSingleQuotes) {
    NOTRIX_CHECK(rejects("{} // comment"));
    NOTRIX_CHECK(rejects("/* c */ {}"));
    NOTRIX_CHECK(rejects("{'a':1}"));
}

NOTRIX_TEST(Json, RejectsUnquotedKeys) {
    NOTRIX_CHECK(rejects("{a:1}"));
}

NOTRIX_TEST(Json, RejectsNonFiniteLiterals) {
    // Accepting these would mean the device and every client disagree about
    // what a scene means.
    NOTRIX_CHECK(rejects("NaN"));
    NOTRIX_CHECK(rejects("Infinity"));
    NOTRIX_CHECK(rejects("-Infinity"));
}

NOTRIX_TEST(Json, RejectsMalformedNumbers) {
    NOTRIX_CHECK(rejects("01"));       // leading zero
    NOTRIX_CHECK(rejects("+1"));       // leading plus
    NOTRIX_CHECK(rejects(".5"));       // no integer part
    NOTRIX_CHECK(rejects("5."));       // no fraction digits
    NOTRIX_CHECK(rejects("1e"));       // no exponent digits
    NOTRIX_CHECK(rejects("1e+"));
    NOTRIX_CHECK(rejects("-"));
    NOTRIX_CHECK(accepts("0"));
    NOTRIX_CHECK(accepts("-0"));
    NOTRIX_CHECK(accepts("1e-3"));
}

NOTRIX_TEST(Json, RejectsTrailingContent) {
    Parsed doc("{} {}");
    NOTRIX_CHECK_EQ(code(doc.error), code(Error::TrailingContent));
}

NOTRIX_TEST(Json, RejectsEmptyInput) {
    NOTRIX_CHECK(rejects(""));
    NOTRIX_CHECK(rejects("   "));
}

NOTRIX_TEST(Json, RejectsUnterminatedContainers) {
    NOTRIX_CHECK(rejects("["));
    NOTRIX_CHECK(rejects("[1"));
    NOTRIX_CHECK(rejects("{"));
    NOTRIX_CHECK(rejects(R"({"a")"));
    NOTRIX_CHECK(rejects(R"({"a":)"));
    NOTRIX_CHECK(rejects(R"({"a":1)"));
}

NOTRIX_TEST(Json, RejectsBadStrings) {
    NOTRIX_CHECK(rejects("\"unterminated"));
    NOTRIX_CHECK(rejects("\"raw\nnewline\""));   // control character
    NOTRIX_CHECK(rejects("\"bad\\xescape\""));
    NOTRIX_CHECK(rejects("\"\\u12\""));          // short hex
    NOTRIX_CHECK(rejects("\"\\uZZZZ\""));
}

NOTRIX_TEST(Json, RejectsBadLiterals) {
    NOTRIX_CHECK(rejects("tru"));
    NOTRIX_CHECK(rejects("TRUE"));
    NOTRIX_CHECK(rejects("nul"));
}

// --- bounds ------------------------------------------------------------------

NOTRIX_TEST(Json, EnforcesDepthLimit) {
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
    NOTRIX_CHECK_EQ(code(doc.error), code(Error::DepthExceeded));
}

NOTRIX_TEST(Json, DepthLimitIsConfigurable) {
    Limits shallow;
    shallow.maxDepth = 2;

    // maxDepth counts nested containers: two are allowed, the third is not.
    NOTRIX_CHECK_EQ(code(Parsed("[1]", shallow).error), code(Error::None));
    NOTRIX_CHECK_EQ(code(Parsed("[[1]]", shallow).error), code(Error::None));
    NOTRIX_CHECK_EQ(code(Parsed("[[[1]]]", shallow).error), code(Error::DepthExceeded));
    NOTRIX_CHECK_EQ(code(Parsed(R"({"a":{"b":{"c":1}}})", shallow).error),
                    code(Error::DepthExceeded));
}

NOTRIX_TEST(Json, EnforcesTokenLimit) {
    Token storage[4];
    Document document(storage, 4);
    NOTRIX_CHECK_EQ(code(document.parse("[1,2,3,4,5,6,7,8]")), code(Error::TokenLimit));
}

NOTRIX_TEST(Json, EnforcesInputSizeLimit) {
    Limits tiny;
    tiny.maxInputBytes = 8;
    NOTRIX_CHECK_EQ(code(Parsed(R"({"aaaaaaaaaa":1})", tiny).error), code(Error::InputTooLarge));
}

NOTRIX_TEST(Json, ReportsWhereParsingFailed) {
    Parsed doc("[1, 2, x]");
    NOTRIX_CHECK_FALSE(doc.ok());
    NOTRIX_CHECK_EQ(doc.document.errorOffset(), std::size_t(7));
}

NOTRIX_TEST(Json, EveryErrorHasADescription) {
    for (int i = 0; i <= static_cast<int>(Error::TrailingContent); ++i) {
        const char* text = notrix::json::describe(static_cast<Error>(i));
        NOTRIX_CHECK(text != nullptr && text[0] != '\0');
    }
}

// --- robustness --------------------------------------------------------------

NOTRIX_TEST(Json, EveryTruncationOfAValidDocumentTerminates) {
    // Truncation is what a dropped connection looks like. No prefix may hang,
    // read out of bounds, or report success.
    const std::string full =
        R"({"name":"a","elements":[{"type":"text","rect":[1,2,3,4],"text":"hi\u00b0"}],"n":-1.5e3})";

    for (std::size_t length = 0; length < full.size(); ++length) {
        Token storage[kTokens];
        Document document(storage, kTokens);
        const Error error = document.parse(full.substr(0, length));
        NOTRIX_CHECK(error != Error::None);
    }

    NOTRIX_CHECK(accepts(full));
}

NOTRIX_TEST(Json, HandlesEveryByteValueInsideAString) {
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
            NOTRIX_CHECK(error != Error::None);
        } else if (byte != '"' && byte != '\\') {
            NOTRIX_CHECK_EQ(code(error), code(Error::None));
        }
    }
}

// --- escapes -----------------------------------------------------------------

NOTRIX_TEST(Json, DecodesSimpleEscapes) {
    Parsed doc(R"(["a\"b","c\\d","e\/f","g\nh","i\tj"])");
    NOTRIX_CHECK(doc.ok());

    const Value root = doc.root();
    NOTRIX_CHECK_EQ(root[0].toString(), std::string("a\"b"));
    NOTRIX_CHECK_EQ(root[1].toString(), std::string("c\\d"));
    NOTRIX_CHECK_EQ(root[2].toString(), std::string("e/f"));
    NOTRIX_CHECK_EQ(root[3].toString(), std::string("g\nh"));
    NOTRIX_CHECK_EQ(root[4].toString(), std::string("i\tj"));
}

NOTRIX_TEST(Json, DecodesUnicodeEscapes) {
    Parsed doc(R"(["\u0041","\u00b0","\u20ac"])");
    NOTRIX_CHECK(doc.ok());

    const Value root = doc.root();
    NOTRIX_CHECK_EQ(root[0].toString(), std::string("A"));
    NOTRIX_CHECK_EQ(root[1].toString(), std::string("\xC2\xB0"));       // degree
    NOTRIX_CHECK_EQ(root[2].toString(), std::string("\xE2\x82\xAC"));   // euro
}

NOTRIX_TEST(Json, CombinesSurrogatePairs) {
    Parsed doc(R"("\ud83d\ude00")");  // U+1F600
    NOTRIX_CHECK(doc.ok());
    NOTRIX_CHECK_EQ(doc.root().toString(), std::string("\xF0\x9F\x98\x80"));
}

NOTRIX_TEST(Json, LoneSurrogateBecomesReplacementCharacter) {
    // Passing it through would produce invalid UTF-8 downstream; dropping it
    // would silently shorten the text.
    Parsed high(R"("\ud83d")");
    NOTRIX_CHECK(high.ok());
    NOTRIX_CHECK_EQ(high.root().toString(), std::string("\xEF\xBF\xBD"));

    Parsed low(R"("\ude00")");
    NOTRIX_CHECK(low.ok());
    NOTRIX_CHECK_EQ(low.root().toString(), std::string("\xEF\xBF\xBD"));
}

NOTRIX_TEST(Json, StringEqualsHandlesEscapes) {
    Parsed doc(R"({"a\u0062c":1})");
    NOTRIX_CHECK(doc.ok());
    NOTRIX_CHECK(doc.root().keyAt(0).stringEquals("abc"));
    NOTRIX_CHECK_FALSE(doc.root().keyAt(0).stringEquals("ab"));
    NOTRIX_CHECK_FALSE(doc.root().keyAt(0).stringEquals("abcd"));
    NOTRIX_CHECK_EQ(doc.root()["abc"].toInt(), std::int64_t(1));
}

NOTRIX_TEST(Json, RawKeepsEscapesIntact) {
    Parsed doc(R"("a\nb")");
    NOTRIX_CHECK_EQ(std::string(doc.root().raw()), std::string("a\\nb"));
}

// --- numbers -----------------------------------------------------------------

NOTRIX_TEST(Json, ParsesIntegerExtremes) {
    Parsed maxValue("9223372036854775807");
    NOTRIX_CHECK_EQ(maxValue.root().toInt(), std::int64_t(9223372036854775807ll));

    Parsed minValue("-9223372036854775808");
    NOTRIX_CHECK_EQ(minValue.root().toInt(), std::int64_t(-9223372036854775807ll - 1));
}

NOTRIX_TEST(Json, IntegerOverflowReportsFallbackRatherThanWrapping) {
    // Wrapping would turn a hostile payload into a plausible-looking number.
    Parsed doc("99999999999999999999999");
    NOTRIX_CHECK(doc.ok());
    NOTRIX_CHECK_EQ(doc.root().toInt(-1), std::int64_t(-1));
}

NOTRIX_TEST(Json, IntegralFloatsConvertToInt) {
    NOTRIX_CHECK_EQ(Parsed("10.0").root().toInt(-1), std::int64_t(10));
    NOTRIX_CHECK_EQ(Parsed("1e3").root().toInt(-1), std::int64_t(1000));
}

NOTRIX_TEST(Json, NonIntegralFloatsDoNotSilentlyTruncate) {
    NOTRIX_CHECK_EQ(Parsed("10.5").root().toInt(-1), std::int64_t(-1));
}

NOTRIX_TEST(Json, HugeFloatsDoNotInventPrecision) {
    // Past 2^53 a double cannot represent every integer.
    NOTRIX_CHECK_EQ(Parsed("1e300").root().toInt(-1), std::int64_t(-1));
}

NOTRIX_TEST(Json, ParsesDoubles) {
    NOTRIX_CHECK_EQ(Parsed("1.5").root().toDouble(), 1.5);
    NOTRIX_CHECK_EQ(Parsed("-0.25").root().toDouble(), -0.25);
    NOTRIX_CHECK_EQ(Parsed("2e3").root().toDouble(), 2000.0);
}

NOTRIX_TEST(Json, BooleansReadCorrectly) {
    NOTRIX_CHECK(Parsed("true").root().toBool(false));
    NOTRIX_CHECK_FALSE(Parsed("false").root().toBool(true));
    NOTRIX_CHECK(Parsed("null").root().isNull());
}
