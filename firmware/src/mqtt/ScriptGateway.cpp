// SPDX-License-Identifier: GPL-3.0-or-later
#include "stipple/mqtt/ScriptGateway.h"

#include <utility>

namespace stipple {
namespace mqtt {

using platform::MqttMessage;

void ScriptGateway::setBase(std::string base) {
    // Trailing slash trimmed once here rather than guessed at every publish.
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    base_ = std::move(base);
}

void ScriptGateway::setConnected(bool connected) noexcept {
    // Going offline does not clear the cache.
    //
    // The last reading is still the last reading, and a script can see from
    // ageMillis() that it is getting old. Blanking it would turn a broker
    // outage into every script on the device losing its data at once, which is
    // both less useful and less honest than a value that is visibly stale.
    connected_ = connected;
}

bool ScriptGateway::connected() const noexcept {
    return connected_ && client_ != nullptr;
}

bool ScriptGateway::publish(std::string_view scriptId, std::string_view leaf,
                            std::string_view payload, bool retain) {
    if (!connected() || base_.empty() || scriptId.empty()) {
        return false;
    }
    if (!script::validPublishLeaf(leaf) ||
        payload.size() > script::IScriptMqtt::kMaxPayloadBytes) {
        return false;
    }

    MqttMessage message;
    message.topic.reserve(base_.size() + scriptId.size() + leaf.size() + 10);
    message.topic = base_;
    message.topic += "/script/";
    message.topic.append(scriptId);
    message.topic += '/';
    message.topic.append(leaf);
    message.payload.assign(payload);
    message.retained = retain;

    return client_->publish(message);
}

bool ScriptGateway::watch(std::string_view scriptId, std::string_view filter) {
    if (scriptId.empty() || !script::validWatchFilter(filter)) {
        return false;
    }

    // Already watching it is a success, not a wasted slot. Scripts call this
    // from draw(), because there is nowhere else to call it from — there is no
    // "the broker just connected" callback a script could hook — so it has to
    // be safe to call thirty times a second for ever.
    if (find(scriptId, filter) != nullptr) {
        return true;
    }

    int mine = 0;
    for (const Watch& held : watches_) {
        if (held.scriptId == scriptId) {
            ++mine;
        }
    }
    if (mine >= script::IScriptMqtt::kMaxWatchesPerScript ||
        static_cast<int>(watches_.size()) >= kMaxWatchesTotal) {
        return false;
    }

    Watch added;
    added.scriptId.assign(scriptId);
    added.filter.assign(filter);
    watches_.push_back(std::move(added));

    // Subscribe now if we can; resubscribe() covers the case where we cannot.
    if (connected()) {
        client_->subscribe(filter, 0);
    }
    return true;
}

const std::string* ScriptGateway::latest(std::string_view scriptId,
                                         std::string_view filter) const {
    const Watch* held = find(scriptId, filter);
    if (held == nullptr || !held->seen) {
        return nullptr;
    }
    return &held->payload;
}

std::int64_t ScriptGateway::ageMillis(std::string_view scriptId,
                                      std::string_view filter) const {
    const Watch* held = find(scriptId, filter);
    if (held == nullptr || !held->seen) {
        return -1;
    }
    if (now_ < held->arrivedMillis) {
        // Should not happen with one monotonic clock, and returning a negative
        // number here would mean "nothing has arrived", which is a different
        // and worse answer than "just now".
        return 0;
    }
    return static_cast<std::int64_t>(now_ - held->arrivedMillis);
}

void ScriptGateway::forget(std::string_view scriptId) {
    // The subscriptions are left in place at the broker.
    //
    // Unsubscribing would need to know that no other script wants the same
    // filter, and getting that wrong takes data away from a script that is
    // still running — a worse failure than a broker sending us a message
    // nothing wants, which costs one comparison in deliver() and is dropped.
    // The next reconnect subscribes only to what is still watched.
    std::size_t write = 0;
    for (std::size_t read = 0; read < watches_.size(); ++read) {
        if (watches_[read].scriptId != scriptId) {
            if (write != read) {
                watches_[write] = std::move(watches_[read]);
            }
            ++write;
        }
    }
    watches_.resize(write);
}

bool ScriptGateway::deliver(const MqttMessage& message) {
    bool wanted = false;
    for (Watch& held : watches_) {
        if (!script::topicMatches(held.filter, message.topic)) {
            continue;
        }
        wanted = true;
        // Truncated rather than dropped. A script watching something that
        // turned out to be a 4 KB JSON document should still get the first
        // part of it, which is usually where the field it wanted is — and a
        // silent nothing would look identical to a sensor going quiet.
        if (message.payload.size() > script::IScriptMqtt::kMaxPayloadBytes) {
            held.payload.assign(message.payload, 0,
                                script::IScriptMqtt::kMaxPayloadBytes);
        } else {
            held.payload = message.payload;
        }
        held.arrivedMillis = now_;
        held.seen = true;
    }
    return wanted;
}

void ScriptGateway::resubscribe() {
    if (!connected()) {
        return;
    }
    for (const Watch& held : watches_) {
        client_->subscribe(held.filter, 0);
    }
}

const ScriptGateway::Watch* ScriptGateway::find(std::string_view scriptId,
                                                std::string_view filter) const noexcept {
    for (const Watch& held : watches_) {
        if (held.scriptId == scriptId && held.filter == filter) {
            return &held;
        }
    }
    return nullptr;
}

}  // namespace mqtt
}  // namespace stipple
