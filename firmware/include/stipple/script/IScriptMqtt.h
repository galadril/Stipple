// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace stipple {
namespace script {

/// The broker, as a script may use it.
///
/// Behind an interface for the same reason the speaker is: `stipple_core`
/// cannot link an MQTT client and the script layer must not decide whether
/// this device has a broker (blueprint §53). The host passes one in, or does
/// not, and the builtins report which.
///
/// **Publishing is scoped and subscribing is not, and that asymmetry is
/// deliberate.** A script writes only under `.../script/{its own id}/`, so it
/// cannot forge a status message, answer a command on the device's behalf, or
/// overwrite another script's output — three things that would each turn a
/// shared script into a way to lie about somebody's device. Reading is
/// different: the whole point of a script on a broker is to show what is
/// already there, the broker is the owner's, and the owner installed the
/// script. So a watch may name any topic.
///
/// Everything is bounded and nothing blocks. A publish either goes now or
/// fails now; there is no queue to grow (blueprint §38), because a device that
/// swallowed a thousand messages during an outage and released them on
/// reconnect is worse than one that dropped them and said so.
class IScriptMqtt {
public:
    /// Topics one script may watch at once.
    ///
    /// Six is enough for a dashboard app and small enough that sixteen scripts
    /// all watching their fill is still a list a person could read.
    static constexpr int kMaxWatchesPerScript = 6;

    /// Longest payload kept, and longest a script may publish.
    ///
    /// A script shows things on a panel 52 pixels wide. Anything longer than
    /// this is not going to be drawn, and keeping it would mean the memory a
    /// script costs depended on what somebody else published.
    static constexpr std::size_t kMaxPayloadBytes = 256;

    /// Longest topic accepted, published or watched.
    static constexpr std::size_t kMaxTopicBytes = 128;

    /// Publishes one script may make per frame.
    ///
    /// The broker would take far more, which is the problem: a script looping
    /// over a publish is a device that floods somebody's whole home
    /// automation from inside their network, and it would look like the broker
    /// misbehaving rather than like a script anyone would think to suspect.
    static constexpr int kMaxPublishesPerCall = 2;

    virtual ~IScriptMqtt() = default;

    /// Whether the device is connected to a broker right now.
    ///
    /// False covers both "MQTT is switched off" and "the broker is
    /// unreachable". A script cannot tell those apart and should not try —
    /// either way it has no data, and what it owes the person looking at the
    /// panel is to say so rather than draw a stale number as if it were live.
    virtual bool connected() const noexcept = 0;

    /// Publish under this script's own subtree.
    ///
    /// `leaf` is appended to `stipple/{deviceId}/script/{scriptId}/`, so a
    /// script publishing "temperature" lands on
    /// `stipple/kitchen-clock/script/thermostat/temperature`. A leaf that is
    /// empty, too long, or contains `#`, `+` or a leading slash is refused.
    ///
    /// False means nothing was sent — disconnected, over the per-frame
    /// budget, or the arguments were unusable.
    virtual bool publish(std::string_view scriptId, std::string_view leaf,
                         std::string_view payload, bool retain) = 0;

    /// Ask for a topic to be delivered to this script from now on.
    ///
    /// Idempotent: watching the same filter twice costs nothing and does not
    /// consume a second slot. False when the list is full or the filter is
    /// unusable, and a script is expected to check — silently dropping the
    /// seventh watch would show up as one reading on a dashboard that never
    /// updates, which is the hardest kind of bug to find.
    ///
    /// Wildcards (`+`, `#`) are accepted, and then `latest()` returns the most
    /// recent message matching the filter whatever its exact topic.
    virtual bool watch(std::string_view scriptId, std::string_view filter) = 0;

    /// The last payload seen for a watched filter.
    ///
    /// Null when nothing has arrived yet, which is **not** the same as an
    /// empty payload — a thermostat that published "" and a thermostat that
    /// has said nothing since the device booted are different states, and a
    /// script that cannot tell them apart will draw one as the other. ADR
    /// 0013, one layer further out.
    virtual const std::string* latest(std::string_view scriptId,
                                      std::string_view filter) const = 0;

    /// Milliseconds since that payload arrived, or a negative number when
    /// nothing has.
    ///
    /// The reason this exists: a retained message from a sensor whose battery
    /// died three weeks ago arrives the instant the device connects and looks
    /// exactly like a live reading. Age is the only thing that tells them
    /// apart, so a script that wants to grey out stale data can.
    virtual std::int64_t ageMillis(std::string_view scriptId,
                                   std::string_view filter) const = 0;

    /// Drop everything held for a script.
    ///
    /// Called when one is deleted or replaced. Without it a script that has
    /// been gone for a month still has the device subscribed on its behalf,
    /// and the only way anybody would find out is by reading the broker's
    /// subscription list and wondering what `old-thermostat` was.
    virtual void forget(std::string_view scriptId) = 0;
};

/// Whether a leaf is one a script may publish to.
///
/// Free function rather than a method so the rule can be tested on its own and
/// so the implementation and the tests cannot disagree about it.
bool validPublishLeaf(std::string_view leaf) noexcept;

/// Whether a filter is one a script may watch.
///
/// Looser than a leaf: wildcards are allowed, because watching
/// `home/+/temperature` is the ordinary case. Still rejects an empty filter,
/// one over `kMaxTopicBytes`, a leading `/`, and control characters.
bool validWatchFilter(std::string_view filter) noexcept;

/// Whether a concrete topic matches an MQTT filter.
///
/// The standard rules: `+` matches exactly one level, `#` matches the rest and
/// must be last. Here rather than in the platform adapter because the cache
/// below has to decide which watch a delivered message belongs to, and the
/// broker does not tell it.
bool topicMatches(std::string_view filter, std::string_view topic) noexcept;

}  // namespace script
}  // namespace stipple
