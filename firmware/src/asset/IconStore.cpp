// SPDX-License-Identifier: GPL-3.0-or-later
#include "notrix/asset/IconStore.h"

namespace notrix {
namespace asset {

const char* IconStore::describe(PutResult result) noexcept {
    switch (result) {
        case PutResult::Added: return "added";
        case PutResult::Replaced: return "replaced";
        case PutResult::InvalidId: return "id is empty or too long";
        case PutResult::InvalidGeometry:
            return "width, height or frame count is out of range, or the pixel count "
                   "does not match";
        case PutResult::TooManyIcons: return "too many icons";
        case PutResult::BudgetExceeded: return "not enough icon storage remaining";
    }
    return "unknown";
}

const Icon* IconStore::find(std::string_view id) const noexcept {
    for (const Icon& icon : icons_) {
        if (icon.id == id) {
            return &icon;
        }
    }
    return nullptr;
}

const Icon* IconStore::at(int index) const noexcept {
    if (index < 0 || index >= count()) {
        return nullptr;
    }
    return &icons_[static_cast<std::size_t>(index)];
}

IconStore::PutResult IconStore::put(Icon icon) {
    if (icon.id.empty() || icon.id.size() > kMaxIdBytes) {
        return PutResult::InvalidId;
    }

    if (icon.width <= 0 || icon.height <= 0 || icon.width > kMaxDimension ||
        icon.height > kMaxDimension || icon.frameCount <= 0 || icon.frameCount > kMaxFrames) {
        return PutResult::InvalidGeometry;
    }

    // The pixel count must match the declared geometry exactly. Trusting a
    // declared width against a shorter buffer is how a blit reads past the end.
    const std::size_t expected =
        icon.pixelsPerFrame() * static_cast<std::size_t>(icon.frameCount);
    if (icon.pixels.size() != expected) {
        return PutResult::InvalidGeometry;
    }

    if (icon.frameMillis == 0) {
        icon.frameMillis = 100;  // a zero would divide by zero when picking a frame
    }

    const std::size_t incoming = icon.byteSize();

    // Replacing frees the old one first, so updating an icon in place does not
    // spuriously fail on a budget it already fits inside.
    std::size_t index = icons_.size();
    std::size_t reclaimed = 0;
    for (std::size_t i = 0; i < icons_.size(); ++i) {
        if (icons_[i].id == icon.id) {
            index = i;
            reclaimed = icons_[i].byteSize();
            break;
        }
    }

    if (bytesUsed_ - reclaimed + incoming > kMaxTotalBytes) {
        return PutResult::BudgetExceeded;
    }

    if (index < icons_.size()) {
        bytesUsed_ = bytesUsed_ - reclaimed + incoming;
        icons_[index] = std::move(icon);
        ++revision_;
        return PutResult::Replaced;
    }

    if (count() >= kMaxIcons) {
        return PutResult::TooManyIcons;
    }

    bytesUsed_ += incoming;
    icons_.push_back(std::move(icon));
    ++revision_;
    return PutResult::Added;
}

bool IconStore::remove(std::string_view id) {
    for (std::size_t i = 0; i < icons_.size(); ++i) {
        if (icons_[i].id == id) {
            bytesUsed_ -= icons_[i].byteSize();
            icons_.erase(icons_.begin() + static_cast<std::ptrdiff_t>(i));
            ++revision_;
            return true;
        }
    }
    return false;
}

void IconStore::clear() {
    icons_.clear();
    bytesUsed_ = 0;
    ++revision_;
}

int IconStore::frameAt(const Icon& icon, std::uint64_t elapsedMillis) noexcept {
    if (icon.frameCount <= 1 || icon.frameMillis == 0) {
        return 0;
    }
    const std::uint64_t index =
        (elapsedMillis / icon.frameMillis) % static_cast<std::uint64_t>(icon.frameCount);
    return static_cast<int>(index);
}

BitmapView IconStore::frameView(const Icon& icon, int frameIndex) noexcept {
    if (frameIndex < 0 || frameIndex >= icon.frameCount || icon.pixels.empty()) {
        return BitmapView{};
    }

    const std::size_t offset =
        static_cast<std::size_t>(frameIndex) * icon.pixelsPerFrame();
    if (offset + icon.pixelsPerFrame() > icon.pixels.size()) {
        return BitmapView{};
    }

    BitmapView view;
    view.pixels = icon.pixels.data() + offset;
    view.width = icon.width;
    view.height = icon.height;
    return view;
}

}  // namespace asset
}  // namespace notrix
