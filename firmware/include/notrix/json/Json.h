// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace notrix {
namespace json {

/// Strict RFC 8259 parser with hard bounds and zero allocation (ADR 0014).
///
/// The caller owns the token array, so a hostile payload cannot grow the heap
/// and the worst case is known before the input arrives. Tokens store offsets
/// into the caller's buffer; no string is copied until something asks for one.
///
///     json::Token storage[256];
///     json::Document doc(storage, 256);
///     if (doc.parse(text) != json::Error::None) { ... }
///     const int duration = static_cast<int>(doc.root()["duration"].toInt(5));
///
/// The input buffer must outlive every Value taken from the Document.

enum class TokenType : std::uint8_t {
    Undefined,
    Object,
    Array,
    String,
    Number,
    Boolean,
    Null,
};

enum class Error : std::uint8_t {
    None,
    UnexpectedCharacter,
    UnexpectedEnd,
    InvalidNumber,
    InvalidString,
    InvalidEscape,
    InvalidLiteral,
    DepthExceeded,
    TokenLimit,
    InputTooLarge,
    TrailingContent,
};

const char* describe(Error error) noexcept;

struct Token {
    TokenType type = TokenType::Undefined;
    /// Immediate child tokens. For an array, its elements; for an object, its
    /// keys; for an object key, exactly one (its value). Everything else: zero.
    std::uint16_t childCount = 0;
    /// Byte range within the parsed input. For strings this excludes the
    /// surrounding quotes and still contains any escape sequences.
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

struct Limits {
    /// How many nested containers are permitted. `[[1]]` is a depth of two.
    /// Guards against stack exhaustion from input like `[[[[[[...`.
    int maxDepth = 32;
    std::size_t maxInputBytes = 64u * 1024u;
};

class Document;

/// A cursor onto one token. Invalid cursors are safe: every accessor returns the
/// supplied fallback, so a missing field needs no special-casing at the call
/// site and a malformed scene degrades instead of crashing.
class Value {
public:
    Value() = default;
    Value(const Document* document, int index) noexcept : document_(document), index_(index) {}

    bool valid() const noexcept { return document_ != nullptr && index_ >= 0; }
    TokenType type() const noexcept;

    bool isObject() const noexcept { return type() == TokenType::Object; }
    bool isArray() const noexcept { return type() == TokenType::Array; }
    bool isString() const noexcept { return type() == TokenType::String; }
    bool isNumber() const noexcept { return type() == TokenType::Number; }
    bool isBoolean() const noexcept { return type() == TokenType::Boolean; }
    bool isNull() const noexcept { return type() == TokenType::Null; }

    /// Elements of an array, or members of an object. Zero for anything else.
    int size() const noexcept;

    /// Array element. Out-of-range gives an invalid Value, never a crash.
    Value operator[](int index) const noexcept;

    /// Object member by name. Absent gives an invalid Value.
    Value operator[](std::string_view key) const noexcept;

    /// Object member key and value by position, for iterating an object whose
    /// field names are not known ahead of time.
    Value keyAt(int index) const noexcept;
    Value valueAt(int index) const noexcept;

    bool toBool(bool fallback = false) const noexcept;

    /// Numbers outside the exactly-representable integer range report the
    /// fallback rather than silently rounding.
    std::int64_t toInt(std::int64_t fallback = 0) const noexcept;
    double toDouble(double fallback = 0.0) const noexcept;

    /// Unescaped text. Allocates, so this is for parse time, never the render
    /// path. Invalid escapes become U+FFFD rather than failing the whole read.
    std::string toString(std::string_view fallback = {}) const;

    /// Compare against a literal without allocating, handling escapes.
    bool stringEquals(std::string_view other) const noexcept;

    /// Raw slice as it appears in the input: no quotes, escapes intact.
    std::string_view raw() const noexcept;

private:
    const Document* document_ = nullptr;
    int index_ = -1;
};

class Document {
public:
    /// `tokens` must remain valid for the lifetime of this Document.
    Document(Token* tokens, int capacity) noexcept : tokens_(tokens), capacity_(capacity) {}

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    Error parse(std::string_view input, const Limits& limits = Limits{}) noexcept;

    Error error() const noexcept { return error_; }

    /// Byte offset where parsing stopped. Useful in an API error response, and
    /// the first thing you want when a hand-written scene will not load.
    std::size_t errorOffset() const noexcept { return errorOffset_; }

    int tokenCount() const noexcept { return count_; }
    int capacity() const noexcept { return capacity_; }

    Value root() const noexcept;

private:
    friend class Value;

    const Token& token(int index) const noexcept { return tokens_[index]; }
    std::string_view input() const noexcept { return input_; }
    bool inRange(int index) const noexcept { return index >= 0 && index < count_; }

    /// One past the end of the subtree rooted at `index`, found by walking child
    /// counts. Iterative: a recursive walk would reintroduce the stack
    /// exhaustion the depth limit exists to prevent.
    int skip(int index) const noexcept;

    Token* tokens_;
    int capacity_;
    int count_ = 0;
    std::string_view input_;
    Error error_ = Error::None;
    std::size_t errorOffset_ = 0;
};

}  // namespace json
}  // namespace notrix
