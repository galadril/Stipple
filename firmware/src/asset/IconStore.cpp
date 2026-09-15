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

// --- persistence -------------------------------------------------------------

namespace {

void pushByte(std::string& out, std::uint8_t value) {
    out.push_back(static_cast<char>(value));
}

void pushUint16(std::string& out, std::uint16_t value) {
    pushByte(out, static_cast<std::uint8_t>(value & 0xFFu));
    pushByte(out, static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

/// Reads sequentially, refusing to run past the end. Every read is checked, so
/// a truncated or corrupt blob fails cleanly instead of walking off the buffer.
class Reader {
public:
    explicit Reader(std::string_view data) noexcept : data_(data) {}

    bool byte(std::uint8_t& out) noexcept {
        if (offset_ >= data_.size()) {
            return false;
        }
        out = static_cast<std::uint8_t>(data_[offset_++]);
        return true;
    }

    bool uint16(std::uint16_t& out) noexcept {
        std::uint8_t low = 0;
        std::uint8_t high = 0;
        if (!byte(low) || !byte(high)) {
            return false;
        }
        out = static_cast<std::uint16_t>(low | (high << 8));
        return true;
    }

    bool bytes(std::size_t count, std::string_view& out) noexcept {
        if (count > data_.size() - offset_) {
            return false;
        }
        out = data_.substr(offset_, count);
        offset_ += count;
        return true;
    }

    bool atEnd() const noexcept { return offset_ >= data_.size(); }

private:
    std::string_view data_;
    std::size_t offset_ = 0;
};

}  // namespace

std::string IconStore::serialize() const {
    std::string out;
    out.reserve(bytesUsed_ + static_cast<std::size_t>(count()) * 64u + 8u);

    out += "NIC";
    pushByte(out, kFormatVersion);
    pushByte(out, static_cast<std::uint8_t>(count()));

    for (const Icon& icon : icons_) {
        pushByte(out, static_cast<std::uint8_t>(icon.id.size()));
        out += icon.id;

        pushByte(out, static_cast<std::uint8_t>(icon.width));
        pushByte(out, static_cast<std::uint8_t>(icon.height));
        pushByte(out, static_cast<std::uint8_t>(icon.frameCount));
        pushByte(out, icon.hasTransparency ? 1u : 0u);
        pushUint16(out, static_cast<std::uint16_t>(icon.frameMillis > 0xFFFFu
                                                       ? 0xFFFFu
                                                       : icon.frameMillis));
        pushByte(out, icon.transparent.r);
        pushByte(out, icon.transparent.g);
        pushByte(out, icon.transparent.b);

        for (const Rgb& pixel : icon.pixels) {
            pushByte(out, pixel.r);
            pushByte(out, pixel.g);
            pushByte(out, pixel.b);
        }
    }
    return out;
}

bool IconStore::deserialize(std::string_view blob) {
    clear();

    Reader reader(blob);

    std::string_view magic;
    std::uint8_t version = 0;
    std::uint8_t iconCount = 0;
    if (!reader.bytes(3, magic) || magic != "NIC" || !reader.byte(version) ||
        version != kFormatVersion || !reader.byte(iconCount)) {
        return false;
    }

    for (std::uint8_t i = 0; i < iconCount; ++i) {
        std::uint8_t idLength = 0;
        std::string_view id;
        if (!reader.byte(idLength) || !reader.bytes(idLength, id)) {
            clear();
            return false;
        }

        std::uint8_t width = 0;
        std::uint8_t height = 0;
        std::uint8_t frames = 0;
        std::uint8_t flags = 0;
        std::uint16_t frameMillis = 0;
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
        if (!reader.byte(width) || !reader.byte(height) || !reader.byte(frames) ||
            !reader.byte(flags) || !reader.uint16(frameMillis) || !reader.byte(r) ||
            !reader.byte(g) || !reader.byte(b)) {
            clear();
            return false;
        }

        // Validate the geometry before it is used to size anything.
        if (width == 0 || height == 0 || frames == 0 || width > kMaxDimension ||
            height > kMaxDimension || frames > kMaxFrames) {
            clear();
            return false;
        }

        const std::size_t pixelCount =
            static_cast<std::size_t>(width) * height * frames;
        std::string_view pixelBytes;
        if (!reader.bytes(pixelCount * 3u, pixelBytes)) {
            clear();
            return false;
        }

        Icon icon;
        icon.id = std::string(id);
        icon.width = width;
        icon.height = height;
        icon.frameCount = frames;
        icon.frameMillis = frameMillis == 0 ? 100u : frameMillis;
        icon.hasTransparency = (flags & 1u) != 0u;
        icon.transparent = Rgb{r, g, b};

        icon.pixels.reserve(pixelCount);
        for (std::size_t p = 0; p < pixelCount; ++p) {
            icon.pixels.push_back(Rgb{static_cast<std::uint8_t>(pixelBytes[p * 3u]),
                                      static_cast<std::uint8_t>(pixelBytes[p * 3u + 1u]),
                                      static_cast<std::uint8_t>(pixelBytes[p * 3u + 2u])});
        }

        const PutResult result = put(std::move(icon));
        if (result != PutResult::Added && result != PutResult::Replaced) {
            // A blob that no longer fits the current limits is not usable, and
            // silently keeping a partial set would be worse than starting clean.
            clear();
            return false;
        }
    }

    return reader.atEnd();
}

}  // namespace asset
}  // namespace notrix
