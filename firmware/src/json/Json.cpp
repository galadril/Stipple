// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/json/Json.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace notrix {
namespace json {
namespace {

constexpr std::size_t kNpos = std::string_view::npos;

bool isWhitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

int hexDigit(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/// Recursive-descent parser. Depth is passed down and checked, so the recursion
/// cannot outrun the configured limit.
struct Parser {
    std::string_view in;
    Token* tokens = nullptr;
    int capacity = 0;
    int count = 0;
    std::size_t pos = 0;
    int maxDepth = 32;
    Error error = Error::None;

    bool fail(Error which) noexcept {
        error = which;
        return false;
    }

    void skipWhitespace() noexcept {
        while (pos < in.size() && isWhitespace(in[pos])) {
            ++pos;
        }
    }

    int allocate(TokenType type, std::size_t start) noexcept {
        if (count >= capacity) {
            fail(Error::TokenLimit);
            return -1;
        }
        Token& token = tokens[count];
        token.type = type;
        token.childCount = 0;
        token.start = static_cast<std::uint32_t>(start);
        token.end = static_cast<std::uint32_t>(start);
        return count++;
    }

    bool addChild(int parent) noexcept {
        if (tokens[parent].childCount == 0xFFFFu) {
            return fail(Error::TokenLimit);
        }
        ++tokens[parent].childCount;
        return true;
    }

    bool parseValue(int depth) noexcept {
        skipWhitespace();
        if (pos >= in.size()) {
            return fail(Error::UnexpectedEnd);
        }

        switch (in[pos]) {
            case '{':
                return parseObject(depth);
            case '[':
                return parseArray(depth);
            case '"': {
                int ignored = -1;
                return parseString(ignored);
            }
            case 't':
                return parseLiteral("true", TokenType::Boolean);
            case 'f':
                return parseLiteral("false", TokenType::Boolean);
            case 'n':
                return parseLiteral("null", TokenType::Null);
            default:
                break;
        }

        if (in[pos] == '-' || isDigit(in[pos])) {
            return parseNumber();
        }
        return fail(Error::UnexpectedCharacter);
    }

    bool parseObject(int depth) noexcept {
        if (depth >= maxDepth) {
            return fail(Error::DepthExceeded);
        }

        const int self = allocate(TokenType::Object, pos);
        if (self < 0) {
            return false;
        }
        ++pos;  // '{'

        skipWhitespace();
        if (pos < in.size() && in[pos] == '}') {
            ++pos;
            tokens[self].end = static_cast<std::uint32_t>(pos);
            return true;
        }

        for (;;) {
            skipWhitespace();
            if (pos >= in.size()) {
                return fail(Error::UnexpectedEnd);
            }
            if (in[pos] != '"') {
                return fail(Error::UnexpectedCharacter);
            }

            int key = -1;
            if (!parseString(key)) {
                return false;
            }

            skipWhitespace();
            if (pos >= in.size()) {
                return fail(Error::UnexpectedEnd);
            }
            if (in[pos] != ':') {
                return fail(Error::UnexpectedCharacter);
            }
            ++pos;

            if (!parseValue(depth + 1)) {
                return false;
            }

            // The key owns its value: one child, which is what lets skip() walk
            // a key/value pair as a single subtree.
            tokens[key].childCount = 1;
            if (!addChild(self)) {
                return false;
            }

            skipWhitespace();
            if (pos >= in.size()) {
                return fail(Error::UnexpectedEnd);
            }
            if (in[pos] == ',') {
                ++pos;
                continue;  // strict: a following '}' is a trailing comma
            }
            if (in[pos] == '}') {
                ++pos;
                tokens[self].end = static_cast<std::uint32_t>(pos);
                return true;
            }
            return fail(Error::UnexpectedCharacter);
        }
    }

    bool parseArray(int depth) noexcept {
        if (depth >= maxDepth) {
            return fail(Error::DepthExceeded);
        }

        const int self = allocate(TokenType::Array, pos);
        if (self < 0) {
            return false;
        }
        ++pos;  // '['

        skipWhitespace();
        if (pos < in.size() && in[pos] == ']') {
            ++pos;
            tokens[self].end = static_cast<std::uint32_t>(pos);
            return true;
        }

        for (;;) {
            if (!parseValue(depth + 1)) {
                return false;
            }
            if (!addChild(self)) {
                return false;
            }

            skipWhitespace();
            if (pos >= in.size()) {
                return fail(Error::UnexpectedEnd);
            }
            if (in[pos] == ',') {
                ++pos;
                continue;
            }
            if (in[pos] == ']') {
                ++pos;
                tokens[self].end = static_cast<std::uint32_t>(pos);
                return true;
            }
            return fail(Error::UnexpectedCharacter);
        }
    }

    /// Token range excludes the quotes and keeps escapes intact — nothing is
    /// decoded or copied until a caller asks for the text.
    bool parseString(int& outIndex) noexcept {
        const std::size_t contentStart = pos + 1;
        std::size_t i = contentStart;

        while (i < in.size()) {
            const char c = in[i];

            if (c == '"') {
                outIndex = allocate(TokenType::String, contentStart);
                if (outIndex < 0) {
                    return false;
                }
                tokens[outIndex].end = static_cast<std::uint32_t>(i);
                pos = i + 1;
                return true;
            }

            if (c == '\\') {
                if (i + 1 >= in.size()) {
                    pos = i;
                    return fail(Error::UnexpectedEnd);
                }
                const char escape = in[i + 1];
                switch (escape) {
                    case '"':
                    case '\\':
                    case '/':
                    case 'b':
                    case 'f':
                    case 'n':
                    case 'r':
                    case 't':
                        i += 2;
                        continue;
                    case 'u': {
                        if (i + 5 >= in.size()) {
                            pos = i;
                            return fail(Error::UnexpectedEnd);
                        }
                        for (std::size_t digit = i + 2; digit < i + 6; ++digit) {
                            if (hexDigit(in[digit]) < 0) {
                                pos = digit;
                                return fail(Error::InvalidEscape);
                            }
                        }
                        i += 6;
                        continue;
                    }
                    default:
                        pos = i + 1;
                        return fail(Error::InvalidEscape);
                }
            }

            // Raw control characters are not permitted inside a JSON string.
            if (static_cast<unsigned char>(c) < 0x20u) {
                pos = i;
                return fail(Error::InvalidString);
            }

            ++i;
        }

        pos = i;
        return fail(Error::UnexpectedEnd);
    }

    bool parseNumber() noexcept {
        const std::size_t start = pos;

        if (pos < in.size() && in[pos] == '-') {
            ++pos;
        }

        // Integer part: a single zero, or a non-zero digit run. "01" is invalid.
        if (pos >= in.size()) {
            return fail(Error::UnexpectedEnd);
        }
        if (in[pos] == '0') {
            ++pos;
        } else if (isDigit(in[pos])) {
            while (pos < in.size() && isDigit(in[pos])) {
                ++pos;
            }
        } else {
            return fail(Error::InvalidNumber);
        }

        if (pos < in.size() && in[pos] == '.') {
            ++pos;
            if (pos >= in.size() || !isDigit(in[pos])) {
                return fail(Error::InvalidNumber);  // "5." is not a number
            }
            while (pos < in.size() && isDigit(in[pos])) {
                ++pos;
            }
        }

        if (pos < in.size() && (in[pos] == 'e' || in[pos] == 'E')) {
            ++pos;
            if (pos < in.size() && (in[pos] == '+' || in[pos] == '-')) {
                ++pos;
            }
            if (pos >= in.size() || !isDigit(in[pos])) {
                return fail(Error::InvalidNumber);
            }
            while (pos < in.size() && isDigit(in[pos])) {
                ++pos;
            }
        }

        const int index = allocate(TokenType::Number, start);
        if (index < 0) {
            return false;
        }
        tokens[index].end = static_cast<std::uint32_t>(pos);
        return true;
    }

    bool parseLiteral(std::string_view text, TokenType type) noexcept {
        if (in.size() - pos < text.size() || in.compare(pos, text.size(), text) != 0) {
            return fail(Error::InvalidLiteral);
        }
        const int index = allocate(type, pos);
        if (index < 0) {
            return false;
        }
        pos += text.size();
        tokens[index].end = static_cast<std::uint32_t>(pos);
        return true;
    }
};

/// Emit `codepoint` as UTF-8 through `sink`.
template <typename Sink>
void encodeUtf8(std::uint32_t codepoint, Sink&& sink) {
    if (codepoint < 0x80u) {
        sink(static_cast<char>(codepoint));
    } else if (codepoint < 0x800u) {
        sink(static_cast<char>(0xC0u | (codepoint >> 6)));
        sink(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint < 0x10000u) {
        sink(static_cast<char>(0xE0u | (codepoint >> 12)));
        sink(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        sink(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        sink(static_cast<char>(0xF0u | (codepoint >> 18)));
        sink(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        sink(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        sink(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

std::uint32_t readHex4(std::string_view raw, std::size_t at) noexcept {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value = (value << 4) | static_cast<std::uint32_t>(hexDigit(raw[at + i]));
    }
    return value;
}

/// Walk an escaped string, pushing decoded bytes to `sink`.
///
/// The parser has already validated the escape syntax, so this cannot fail. A
/// lone surrogate is emitted as U+FFFD: dropping it would silently shorten the
/// text, and passing it through would produce invalid UTF-8 downstream.
template <typename Sink>
void decodeEscaped(std::string_view raw, Sink&& sink) {
    std::size_t i = 0;
    while (i < raw.size()) {
        const char c = raw[i];
        if (c != '\\') {
            sink(c);
            ++i;
            continue;
        }

        const char escape = raw[i + 1];
        switch (escape) {
            case '"': sink('"'); i += 2; break;
            case '\\': sink('\\'); i += 2; break;
            case '/': sink('/'); i += 2; break;
            case 'b': sink('\b'); i += 2; break;
            case 'f': sink('\f'); i += 2; break;
            case 'n': sink('\n'); i += 2; break;
            case 'r': sink('\r'); i += 2; break;
            case 't': sink('\t'); i += 2; break;
            case 'u': {
                std::uint32_t codepoint = readHex4(raw, i + 2);
                i += 6;

                if (codepoint >= 0xD800u && codepoint <= 0xDBFFu) {
                    // High surrogate: pair it with a following low surrogate.
                    if (i + 6 <= raw.size() && raw[i] == '\\' && raw[i + 1] == 'u') {
                        const std::uint32_t low = readHex4(raw, i + 2);
                        if (low >= 0xDC00u && low <= 0xDFFFu) {
                            codepoint = 0x10000u + ((codepoint - 0xD800u) << 10) + (low - 0xDC00u);
                            i += 6;
                        } else {
                            codepoint = 0xFFFDu;
                        }
                    } else {
                        codepoint = 0xFFFDu;
                    }
                } else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu) {
                    codepoint = 0xFFFDu;  // lone low surrogate
                }

                encodeUtf8(codepoint, sink);
                break;
            }
            default:
                sink(escape);
                i += 2;
                break;
        }
    }
}

}  // namespace

const char* describe(Error error) noexcept {
    switch (error) {
        case Error::None: return "ok";
        case Error::UnexpectedCharacter: return "unexpected character";
        case Error::UnexpectedEnd: return "unexpected end of input";
        case Error::InvalidNumber: return "invalid number";
        case Error::InvalidString: return "invalid string";
        case Error::InvalidEscape: return "invalid escape sequence";
        case Error::InvalidLiteral: return "invalid literal";
        case Error::DepthExceeded: return "nesting too deep";
        case Error::TokenLimit: return "too many tokens";
        case Error::InputTooLarge: return "input too large";
        case Error::TrailingContent: return "trailing content after value";
    }
    return "unknown error";
}

// --- Document ---------------------------------------------------------------

Error Document::parse(std::string_view input, const Limits& limits) noexcept {
    count_ = 0;
    input_ = {};
    error_ = Error::None;
    errorOffset_ = 0;

    if (input.size() > limits.maxInputBytes) {
        error_ = Error::InputTooLarge;
        return error_;
    }
    if (tokens_ == nullptr || capacity_ <= 0) {
        error_ = Error::TokenLimit;
        return error_;
    }

    Parser parser;
    parser.in = input;
    parser.tokens = tokens_;
    parser.capacity = capacity_;
    parser.maxDepth = limits.maxDepth < 1 ? 1 : limits.maxDepth;

    if (!parser.parseValue(0)) {
        error_ = parser.error;
        errorOffset_ = parser.pos;
        return error_;
    }

    parser.skipWhitespace();
    if (parser.pos != input.size()) {
        error_ = Error::TrailingContent;
        errorOffset_ = parser.pos;
        return error_;
    }

    count_ = parser.count;
    input_ = input;
    return Error::None;
}

int Document::skip(int index) const noexcept {
    if (!inRange(index)) {
        return count_;
    }
    int pending = 1;
    int i = index;
    while (pending > 0 && i < count_) {
        pending += tokens_[i].childCount;
        --pending;
        ++i;
    }
    return i;
}

Value Document::root() const noexcept {
    if (count_ == 0) {
        return Value{};
    }
    return Value(this, 0);
}

// --- Value ------------------------------------------------------------------

TokenType Value::type() const noexcept {
    if (!valid() || !document_->inRange(index_)) {
        return TokenType::Undefined;
    }
    return document_->token(index_).type;
}

int Value::size() const noexcept {
    const TokenType kind = type();
    if (kind != TokenType::Object && kind != TokenType::Array) {
        return 0;
    }
    return static_cast<int>(document_->token(index_).childCount);
}

Value Value::operator[](int index) const noexcept {
    if (type() != TokenType::Array || index < 0 || index >= size()) {
        return Value{};
    }
    int child = index_ + 1;
    for (int i = 0; i < index; ++i) {
        child = document_->skip(child);
    }
    return document_->inRange(child) ? Value(document_, child) : Value{};
}

Value Value::keyAt(int index) const noexcept {
    if (type() != TokenType::Object || index < 0 || index >= size()) {
        return Value{};
    }
    int key = index_ + 1;
    for (int i = 0; i < index; ++i) {
        key = document_->skip(key);
    }
    return document_->inRange(key) ? Value(document_, key) : Value{};
}

Value Value::valueAt(int index) const noexcept {
    const Value key = keyAt(index);
    if (!key.valid()) {
        return Value{};
    }
    const int child = key.index_ + 1;
    return document_->inRange(child) ? Value(document_, child) : Value{};
}

Value Value::operator[](std::string_view key) const noexcept {
    const int count = type() == TokenType::Object ? size() : 0;
    for (int i = 0; i < count; ++i) {
        if (keyAt(i).stringEquals(key)) {
            return valueAt(i);
        }
    }
    return Value{};
}

std::string_view Value::raw() const noexcept {
    if (!valid() || !document_->inRange(index_)) {
        return {};
    }
    const Token& token = document_->token(index_);
    const std::string_view input = document_->input();
    if (token.start > input.size() || token.end > input.size() || token.end < token.start) {
        return {};
    }
    return input.substr(token.start, token.end - token.start);
}

bool Value::toBool(bool fallback) const noexcept {
    if (type() != TokenType::Boolean) {
        return fallback;
    }
    return raw().size() == 4;  // "true" vs "false"
}

double Value::toDouble(double fallback) const noexcept {
    if (type() != TokenType::Number) {
        return fallback;
    }
    const std::string_view text = raw();

    // strtod needs a terminated buffer. A JSON number longer than this is not
    // something a pixel clock has any use for.
    char buffer[64];
    if (text.empty() || text.size() >= sizeof(buffer)) {
        return fallback;
    }
    std::memcpy(buffer, text.data(), text.size());
    buffer[text.size()] = '\0';

    char* parseEnd = nullptr;
    const double value = std::strtod(buffer, &parseEnd);
    if (parseEnd != buffer + text.size() || !std::isfinite(value)) {
        return fallback;
    }
    return value;
}

std::int64_t Value::toInt(std::int64_t fallback) const noexcept {
    if (type() != TokenType::Number) {
        return fallback;
    }
    const std::string_view text = raw();
    if (text.empty()) {
        return fallback;
    }

    if (text.find_first_of(".eE") == kNpos) {
        const bool negative = text[0] == '-';
        std::size_t i = negative ? 1u : 0u;
        if (i >= text.size()) {
            return fallback;
        }

        // Accumulate in unsigned and stop at the boundary, so overflow is
        // reported rather than wrapping into a plausible-looking number.
        const std::uint64_t limit = negative ? 9223372036854775808ull : 9223372036854775807ull;
        std::uint64_t accumulator = 0;
        for (; i < text.size(); ++i) {
            const std::uint64_t digit = static_cast<std::uint64_t>(text[i] - '0');
            if (accumulator > (limit - digit) / 10u) {
                return fallback;
            }
            accumulator = accumulator * 10u + digit;
        }

        if (negative) {
            if (accumulator == 9223372036854775808ull) {
                return (-9223372036854775807ll) - 1;  // INT64_MIN, without UB
            }
            return -static_cast<std::int64_t>(accumulator);
        }
        return static_cast<std::int64_t>(accumulator);
    }

    const double value = toDouble(static_cast<double>(fallback));
    if (!std::isfinite(value) || value != std::floor(value)) {
        return fallback;
    }
    // Beyond 2^53 a double can no longer represent every integer, so converting
    // would invent precision that the input never had.
    if (value > 9007199254740992.0 || value < -9007199254740992.0) {
        return fallback;
    }
    return static_cast<std::int64_t>(value);
}

bool Value::stringEquals(std::string_view other) const noexcept {
    if (type() != TokenType::String) {
        return false;
    }
    const std::string_view text = raw();

    // Fast path: nothing to decode.
    if (text.find('\\') == kNpos) {
        return text == other;
    }

    std::size_t matched = 0;
    bool equal = true;
    decodeEscaped(text, [&](char c) {
        if (!equal) {
            return;
        }
        if (matched >= other.size() || other[matched] != c) {
            equal = false;
            return;
        }
        ++matched;
    });
    return equal && matched == other.size();
}

std::string Value::toString(std::string_view fallback) const {
    if (type() != TokenType::String) {
        return std::string(fallback);
    }
    const std::string_view text = raw();
    if (text.find('\\') == kNpos) {
        return std::string(text);
    }

    std::string out;
    out.reserve(text.size());
    decodeEscaped(text, [&out](char c) { out.push_back(c); });
    return out;
}

}  // namespace json
}  // namespace notrix
