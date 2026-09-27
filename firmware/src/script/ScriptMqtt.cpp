// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three rules that decide what a script may say to a broker and what it
// gets told back. Free functions in core, with no gateway and no client, so
// they can be tested exhaustively on their own — which matters more here than
// usual, because two of them are the security boundary and the third is the
// one everybody gets wrong.
#include "stipple/script/IScriptMqtt.h"

namespace stipple {
namespace script {
namespace {

/// Anything below space, plus DEL. A topic carrying a newline would be
/// accepted by some brokers and silently truncated by others, and a script
/// author would have no way to tell which had happened.
bool printable(char c) noexcept {
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x20 && u != 0x7F;
}

}  // namespace

bool validPublishLeaf(std::string_view leaf) noexcept {
    if (leaf.empty() || leaf.size() > IScriptMqtt::kMaxTopicBytes) {
        return false;
    }
    // A leading or trailing slash would produce an empty level, which is legal
    // MQTT and always a mistake here — it means somebody wrote the separator
    // twice by concatenating a prefix that already had one.
    if (leaf.front() == '/' || leaf.back() == '/') {
        return false;
    }
    for (std::size_t i = 0; i < leaf.size(); ++i) {
        const char c = leaf[i];
        // Wildcards are meaningless in a published topic and dangerous in one
        // built by concatenation: a script publishing to "#" would be
        // publishing to a filter, and what a broker does with that is not
        // something to find out on somebody's home network.
        if (c == '#' || c == '+' || !printable(c)) {
            return false;
        }
        if (c == '/' && i + 1 < leaf.size() && leaf[i + 1] == '/') {
            return false;  // empty level in the middle
        }
    }

    // ".." is an ordinary topic level in MQTT - no broker walks it up a tree -
    // so a script publishing to "../status" lands harmlessly inside its own
    // subtree. It is refused anyway, because the things downstream of a broker
    // are not all brokers: a bridge that maps topics onto file paths, or a
    // logger that does, would walk it, and no honest script has ever needed a
    // level named "..".
    std::size_t start = 0;
    while (start <= leaf.size()) {
        std::size_t end = leaf.find('/', start);
        if (end == std::string_view::npos) {
            end = leaf.size();
        }
        if (leaf.substr(start, end - start) == "..") {
            return false;
        }
        if (end == leaf.size()) {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool validWatchFilter(std::string_view filter) noexcept {
    if (filter.empty() || filter.size() > IScriptMqtt::kMaxTopicBytes) {
        return false;
    }
    if (filter.front() == '/') {
        return false;
    }
    for (const char c : filter) {
        if (!printable(c)) {
            return false;
        }
    }

    // Wildcards have to occupy a whole level. "sensor+/x" is not a filter that
    // matches anything, but a broker will happily accept the subscription, so
    // a script that wrote it would sit there showing nothing for ever with no
    // error anywhere. Rejecting it here is the only place anyone finds out.
    std::size_t start = 0;
    while (start <= filter.size()) {
        std::size_t end = filter.find('/', start);
        if (end == std::string_view::npos) {
            end = filter.size();
        }
        const std::string_view level = filter.substr(start, end - start);

        if (level.find('#') != std::string_view::npos) {
            // '#' must be a level of its own and the last one.
            if (level.size() != 1 || end != filter.size()) {
                return false;
            }
        }
        if (level.find('+') != std::string_view::npos && level.size() != 1) {
            return false;
        }

        if (end == filter.size()) {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool topicMatches(std::string_view filter, std::string_view topic) noexcept {
    // Walk both level by level. Written out rather than recursed because '#'
    // only ever appears last, so there is nothing to backtrack over — which is
    // exactly why MQTT's wildcards are the shape they are.
    std::size_t f = 0;
    std::size_t t = 0;

    while (f < filter.size()) {
        std::size_t fEnd = filter.find('/', f);
        if (fEnd == std::string_view::npos) {
            fEnd = filter.size();
        }
        const std::string_view level = filter.substr(f, fEnd - f);

        if (level == "#") {
            // Matches the rest, including nothing — but not a topic beginning
            // with '$', which by convention is the broker's own and is never
            // caught by a wildcard at the root.
            return !(f == 0 && !topic.empty() && topic.front() == '$');
        }

        if (t > topic.size()) {
            return false;  // filter has levels the topic does not
        }
        std::size_t tEnd = topic.find('/', t);
        if (tEnd == std::string_view::npos) {
            tEnd = topic.size();
        }
        const std::string_view here = topic.substr(t, tEnd - t);

        if (level == "+") {
            if (f == 0 && !here.empty() && here.front() == '$') {
                return false;
            }
        } else if (level != here) {
            return false;
        }

        const bool filterDone = fEnd == filter.size();
        const bool topicDone = tEnd == topic.size();
        if (topicDone && !filterDone) {
            // The parent matches its own multi-level wildcard: "home/#" covers
            // "home" as well as everything under it. That is in the spec and
            // is the case every hand-rolled matcher gets wrong, usually by
            // showing nothing for a sensor that publishes at the top level.
            return filter.substr(fEnd) == "/#";
        }
        if (filterDone || topicDone) {
            return filterDone && topicDone;
        }
        f = fEnd + 1;
        t = tEnd + 1;
    }

    return false;
}

}  // namespace script
}  // namespace stipple
