// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "stipple/script/IScriptRunner.h"

namespace stipple {

class Canvas;

namespace script {

class ScriptHost;

/// One script, and the interpreter running it.
struct Script {
    std::string id;
    std::string name;
    std::string source;

    /// Whether it compiled and has not since failed.
    bool ok = false;

    /// Why it is not ok. Empty when it is.
    ///
    /// Kept rather than only logged, because this is the one piece of
    /// information the author is actually waiting for. The editor shows it
    /// next to the code that caused it.
    std::string problem;

    /// Instructions the last frame spent. A script close to the budget is one
    /// about to break on a busier frame, and that is worth seeing before it
    /// does.
    std::uint32_t lastInstructions = 0;

    /// Bytes its interpreter is holding.
    std::size_t memoryBytes = 0;
};

/// Bounded collection of scripts.
///
/// Each script gets its own interpreter rather than sharing one. Sharing would
/// save a few kilobytes and cost the property that matters: scripts arrive over
/// the network from whoever can reach the device, and one of them clobbering
/// another's globals is a bug nobody would ever find. A measured interpreter is
/// about 4 KB (see ScriptHost's memory test), so isolation is affordable here
/// in a way it would not be on an ESP32.
///
/// A script that fails is kept, not dropped. Its source is what the author
/// needs to fix, and deleting their work because it did not compile would be
/// the worst possible response to a typo.
class ScriptStore final : public IScriptRunner {
public:
    /// Sixteen scripts at roughly 4 KB of interpreter each is about 64 KB,
    /// plus source. Bounded because everything here is bounded, not because
    /// the device is near a limit at this number.
    static constexpr int kMaxScripts = 16;
    static constexpr std::size_t kMaxIdBytes = 48;
    static constexpr std::size_t kMaxNameBytes = 64;

    enum class PutResult {
        Added,
        Replaced,
        InvalidId,
        SourceTooLarge,
        TooManyScripts,
        /// Stored, but it does not run. Deliberately not an error: the source
        /// is saved and `problem` says what is wrong, so the author can fix it
        /// in place rather than losing it to a missing `end`.
        DidNotCompile,
    };

    static const char* describe(PutResult result) noexcept;

    ScriptStore();
    ~ScriptStore() override;

    ScriptStore(const ScriptStore&) = delete;
    ScriptStore& operator=(const ScriptStore&) = delete;

    /// Insert or replace by id. A replacement keeps its position, so saving an
    /// edit must not shuffle the carousel under the person who made it.
    PutResult put(std::string id, std::string name, std::string source);

    bool remove(std::string_view id);
    void clear();

    int count() const noexcept { return static_cast<int>(entries_.size()); }
    bool empty() const noexcept { return entries_.empty(); }

    const Script* at(int index) const noexcept;
    const Script* find(std::string_view id) const noexcept;

    /// Draw one frame of a script onto the panel.
    ///
    /// False when it is missing or has failed; the reason is in the script's
    /// `problem`. A failed script is not retried every frame - see ScriptHost -
    /// so this stays false until the source is saved again.
    bool draw(std::string_view id, Canvas& canvas, std::uint64_t elapsedMillis) override;

    bool has(std::string_view id) const noexcept override;
    std::string_view problem(std::string_view id) const noexcept override;

    /// Collect a script's garbage. Worth doing when it leaves the screen,
    /// which is a moment the device has time to spare and the next frame does
    /// not.
    void collectGarbage(std::string_view id);

    /// Total bytes every interpreter is holding.
    std::size_t memoryBytes() const noexcept;

private:
    struct Entry {
        Script info;
        std::unique_ptr<ScriptHost> host;
    };

    std::vector<Entry> entries_;

    Entry* findEntry(std::string_view id) noexcept;
    void refresh(Entry& entry) noexcept;
};

/// Whether an id is one the device will accept.
///
/// Lowercase letters, digits, dash and underscore. Restrictive because a
/// script id becomes part of an API path and a config key, and the set of
/// characters that are safe in both is smaller than the set that looks
/// harmless.
bool validScriptId(std::string_view id) noexcept;

}  // namespace script
}  // namespace stipple
