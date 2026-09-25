// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace stipple {
namespace platform {

/// Persistent key/value storage.
///
/// Deliberately not a filesystem. Phase 4's configuration layer needs
/// transactional writes, a checksum, a backup copy and migration (blueprint
/// §21); building that on "read a blob, write a blob atomically" is far easier
/// to reason about — and to make crash-safe — than on arbitrary file
/// operations. It also keeps API input from ever naming a path (§23).
class IStorage {
public:
    virtual ~IStorage() = default;

    virtual bool exists(std::string_view key) const = 0;

    /// Read a value. Returns false if the key is absent; `out` is then cleared.
    virtual bool read(std::string_view key, std::string& out) const = 0;

    /// Write a value, replacing any previous one.
    ///
    /// Implementations must be atomic from the caller's point of view: after a
    /// power cut the key holds either the old value or the new one, never a
    /// torn mixture. Returns false if the value exceeds `maxValueBytes()` or
    /// the write fails.
    virtual bool write(std::string_view key, std::string_view value) = 0;

    virtual bool remove(std::string_view key) = 0;

    /// Largest value this backend accepts. Bounded by design (§38) — callers
    /// check before building a payload rather than discovering the limit on
    /// failure.
    virtual std::size_t maxValueBytes() const = 0;
};

}  // namespace platform
}  // namespace stipple
