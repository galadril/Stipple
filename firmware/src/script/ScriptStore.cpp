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
            return true;
        }
    }
    return false;
}

void ScriptStore::clear() {
    entries_.clear();
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

}  // namespace script
}  // namespace stipple
