// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/script/ScriptStore.h"

#include "stipple/script/ScriptHost.h"

namespace stipple {
namespace script {

bool validScriptId(std::string_view id) noexcept {
    if (id.empty() || id.size() > ScriptStore::kMaxIdBytes) {
        return false;
    }
    for (const char c : id) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                             c == '-' || c == '_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

ScriptStore::ScriptStore() = default;

// Out of line because Entry holds a unique_ptr<ScriptHost> and ScriptHost is
// only forward declared in the header. The destructor has to be where the
// complete type is.
ScriptStore::~ScriptStore() = default;

ScriptStore::Entry* ScriptStore::findEntry(std::string_view id) noexcept {
    for (Entry& entry : entries_) {
        if (entry.info.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

void ScriptStore::refresh(Entry& entry) noexcept {
    if (entry.host == nullptr) {
        entry.info.ok = false;
        return;
    }
    entry.info.ok = entry.host->ready();
    entry.info.lastInstructions = entry.host->lastInstructions();
    entry.info.memoryBytes = entry.host->memoryBytes();
}

ScriptPutResult ScriptStore::put(std::string id, std::string name, std::string source) {
    if (!validScriptId(id)) {
        return ScriptPutResult::InvalidId;
    }
    if (source.size() > ScriptHost::kMaxSourceBytes) {
        return ScriptPutResult::SourceTooLarge;
    }
    if (name.size() > kMaxNameBytes) {
        name.resize(kMaxNameBytes);
    }

    Entry* existing = findEntry(id);
    if (existing == nullptr && count() >= kMaxScripts) {
        return ScriptPutResult::TooManyScripts;
    }

    // A fresh interpreter every time, including on a replacement. Reusing one
    // would leave the previous script's globals and instances in place, so an
    // edit would run against state the new source never created - and it would
    // appear to work right up until the device restarted.
    auto host = std::make_unique<ScriptHost>();
    std::string problem;
    const bool compiled = host->load(source, problem);

    Entry entry;
    entry.info.id = std::move(id);
    entry.info.name = std::move(name);
    entry.info.source = std::move(source);
    entry.info.problem = problem;
    entry.host = std::move(host);
    refresh(entry);

    ++revision_;

    if (existing != nullptr) {
        // Position is kept deliberately: saving an edit must not shuffle the
        // carousel under the person who made it.
        *existing = std::move(entry);
        return compiled ? ScriptPutResult::Replaced : ScriptPutResult::DidNotCompile;
    }

    entries_.push_back(std::move(entry));
    return compiled ? ScriptPutResult::Added : ScriptPutResult::DidNotCompile;
}

bool ScriptStore::remove(std::string_view id) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].info.id == id) {
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
            ++revision_;
            return true;
        }
    }
    return false;
}

void ScriptStore::clear() {
    if (!entries_.empty()) {
        entries_.clear();
        ++revision_;
    }
}

const Script* ScriptStore::at(int index) const noexcept {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(index)].info;
}

const Script* ScriptStore::find(std::string_view id) const noexcept {
    for (const Entry& entry : entries_) {
        if (entry.info.id == id) {
            return &entry.info;
        }
    }
    return nullptr;
}

bool ScriptStore::has(std::string_view id) const noexcept {
    return find(id) != nullptr;
}

std::string_view ScriptStore::problem(std::string_view id) const noexcept {
    const Script* script = find(id);
    if (script == nullptr) {
        return "no such script";
    }
    return script->problem;
}

bool ScriptStore::draw(std::string_view id, Canvas& canvas, std::uint64_t elapsedMillis) {
    Entry* entry = findEntry(id);
    if (entry == nullptr || entry->host == nullptr) {
        return false;
    }
    if (!entry->host->ready()) {
        return false;  // already failed; the reason is in info.problem
    }

    std::string problem;
    const bool drew = entry->host->draw(canvas, elapsedMillis, problem);
    if (!drew) {
        // Keep the first failure. A script that dies on frame one and a script
        // that dies on frame ten thousand need the same message, and it is the
        // one from the frame that actually broke.
        entry->info.problem = problem;
    }
    refresh(*entry);
    return drew;
}

bool ScriptStore::button(std::string_view id, std::string_view name) {
    Entry* entry = findEntry(id);
    if (entry == nullptr || entry->host == nullptr || !entry->host->ready()) {
        return false;
    }

    std::string problem;
    const ScriptHost::EventResult result = entry->host->button(name, problem);
    if (result == ScriptHost::EventResult::Failed) {
        entry->info.problem = problem;
    }
    refresh(*entry);
    return result == ScriptHost::EventResult::Handled;
}

void ScriptStore::collectGarbage(std::string_view id) {
    if (Entry* entry = findEntry(id); entry != nullptr && entry->host != nullptr) {
        entry->info.memoryBytes = entry->host->collectGarbage();
    }
}

std::size_t ScriptStore::maxSourceBytes() const noexcept {
    return ScriptHost::kMaxSourceBytes;
}

std::size_t ScriptStore::memoryBytes() const noexcept {
    std::size_t total = 0;
    for (const Entry& entry : entries_) {
        total += entry.info.memoryBytes;
    }
    return total;
}


// --- persistence -------------------------------------------------------------
//
// Length-prefixed, not delimited. A script is arbitrary text that will contain
// newlines, quotes and very likely whatever separator seemed safe at the time,
// so nothing here scans for one: every field says how long it is and the
// reader takes exactly that many bytes. The format cannot be confused by its
// own contents.

namespace {

constexpr char kMagic[] = "SBS";       // Stipple Berry Scripts
constexpr std::uint8_t kFormatVersion = 1;

void pushByte(std::string& out, std::uint8_t value) {
    out.push_back(static_cast<char>(value));
}

void pushUint32(std::string& out, std::uint32_t value) {
    pushByte(out, static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    pushByte(out, static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    pushByte(out, static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    pushByte(out, static_cast<std::uint8_t>(value & 0xFFu));
}

/// Reads forward through a blob, refusing to run off the end.
///
/// Every read is checked, because this blob comes off storage that may have
/// been interrupted mid-write, and a truncated one must be rejected rather
/// than read past.
class Reader {
public:
    explicit Reader(std::string_view blob) : blob_(blob) {}

    bool byte(std::uint8_t& out) {
        if (at_ >= blob_.size()) { return false; }
        out = static_cast<std::uint8_t>(blob_[at_++]);
        return true;
    }

    bool uint32(std::uint32_t& out) {
        std::uint8_t b[4];
        for (std::uint8_t& each : b) {
            if (!byte(each)) { return false; }
        }
        out = (static_cast<std::uint32_t>(b[0]) << 24) |
              (static_cast<std::uint32_t>(b[1]) << 16) |
              (static_cast<std::uint32_t>(b[2]) << 8) |
              static_cast<std::uint32_t>(b[3]);
        return true;
    }

    bool text(std::uint32_t length, std::string& out) {
        if (length > blob_.size() - at_) { return false; }
        out.assign(blob_, at_, length);
        at_ += length;
        return true;
    }

    bool exhausted() const { return at_ == blob_.size(); }

private:
    std::string_view blob_;
    std::size_t at_ = 0;
};

}  // namespace

std::string ScriptStore::serialize() const {
    std::string out;
    out += kMagic;
    pushByte(out, kFormatVersion);
    pushByte(out, static_cast<std::uint8_t>(entries_.size()));

    for (const Entry& entry : entries_) {
        pushByte(out, static_cast<std::uint8_t>(entry.info.id.size()));
        out += entry.info.id;
        pushByte(out, static_cast<std::uint8_t>(entry.info.name.size()));
        out += entry.info.name;
        pushUint32(out, static_cast<std::uint32_t>(entry.info.source.size()));
        out += entry.info.source;
    }
    return out;
}

bool ScriptStore::deserialize(std::string_view blob) {
    Reader reader(blob);

    for (const char expected : std::string_view(kMagic)) {
        std::uint8_t actual = 0;
        if (!reader.byte(actual) || actual != static_cast<std::uint8_t>(expected)) {
            return false;
        }
    }

    std::uint8_t version = 0;
    if (!reader.byte(version) || version != kFormatVersion) {
        return false;
    }

    std::uint8_t stored = 0;
    if (!reader.byte(stored) || stored > kMaxScripts) {
        return false;
    }

    // Read the whole blob before touching the store. A half-applied restore
    // would leave the device with some of the old library and some of the new,
    // which is worse than either.
    struct Pending {
        std::string id;
        std::string name;
        std::string source;
    };
    std::vector<Pending> pending;
    pending.reserve(stored);

    for (std::uint8_t i = 0; i < stored; ++i) {
        Pending entry;
        std::uint8_t idLength = 0;
        std::uint8_t nameLength = 0;
        std::uint32_t sourceLength = 0;
        if (!reader.byte(idLength) || !reader.text(idLength, entry.id)) { return false; }
        if (!reader.byte(nameLength) || !reader.text(nameLength, entry.name)) { return false; }
        if (!reader.uint32(sourceLength)) { return false; }
        if (sourceLength > ScriptHost::kMaxSourceBytes) { return false; }
        if (!reader.text(sourceLength, entry.source)) { return false; }
        if (!validScriptId(entry.id)) { return false; }
        pending.push_back(std::move(entry));
    }

    // Trailing bytes mean this is not the blob it claims to be.
    if (!reader.exhausted()) {
        return false;
    }

    entries_.clear();
    for (Pending& entry : pending) {
        // Through put(), so every script is compiled on the way in and a
        // stored script that no longer compiles - because the firmware's
        // builtins changed under it, say - comes back with its source intact
        // and its reason attached, exactly as if it had just been typed.
        put(std::move(entry.id), std::move(entry.name), std::move(entry.source));
    }
    return true;
}

}  // namespace script
}  // namespace stipple
