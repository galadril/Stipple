// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace stipple {
namespace web {

/// One file from `firmware/web/`, compiled into the binary.
///
/// Every field is a `string_view` over static storage, so serving a page copies
/// nothing and the whole UI costs no heap. On the device these land in .rodata
/// and are demand-paged from the executable rather than held in RAM.
struct Asset {
    /// Request path, with a leading slash: "/index.html".
    std::string_view path;
    std::string_view contentType;
    std::string_view body;

    /// Strong ETag over the file's bytes, so a browser that already has the
    /// page gets a 304 instead of the whole thing again. Worth having on a
    /// device whose uplink may be a long way from fast.
    std::string_view etag;
};

/// The generated table. Defined by WebAssets.generated.cpp, which CMake writes
/// from the contents of `firmware/web/` — do not edit that file by hand.
///
/// Ordered by path, but the table is small enough that lookup scans it; a sorted
/// search would be more code for no measurable gain at this size.
const Asset* assets() noexcept;
int assetCount() noexcept;

/// Exact-path lookup. Returns nullptr when nothing matches.
const Asset* findAsset(std::string_view path) noexcept;

}  // namespace web
}  // namespace stipple
