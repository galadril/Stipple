#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Write a version into every place that has to agree, and verify it took.
#
#     tooling/release/bump.sh 0.2.4
#
# The version lives in three places that drift silently: project(VERSION) in
# CMakeLists.txt, kVersion in Version.h (what the device reports over
# /api/v1/version and paints on the splash), and the git tag. This writes the
# first two; the tag is the release workflow's job.
#
# A script rather than three lines of sed in a workflow, because the release
# job is not the only thing that needs to do this and two copies of a
# substitution are two chances for them to disagree — which is the exact
# failure the version guard exists to catch.
#
# It also renames a `## Unreleased` changelog heading to the version, so the
# notes somebody wrote while working become the notes the release publishes
# without anybody having to remember a second edit.
set -euo pipefail

version="${1:-}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "usage: bump.sh <major.minor.patch>" >&2
    exit 2
fi

cmake="${root}/CMakeLists.txt"
header="${root}/firmware/include/stipple/core/Version.h"
changelog="${root}/CHANGELOG.md"

for f in "$cmake" "$header" "$changelog"; do
    [ -f "$f" ] || { echo "bump: missing $f" >&2; exit 1; }
done

# project(VERSION x.y.z)
sed -i -E "s/^([[:space:]]*VERSION[[:space:]]+)[0-9]+\.[0-9]+\.[0-9]+/\1${version}/" "$cmake"

# inline constexpr std::string_view kVersion = "x.y.z";
sed -i -E "s/(kVersion[[:space:]]*=[[:space:]]*\")[^\"]+(\")/\1${version}\2/" "$header"

# `## Unreleased` becomes `## x.y.z`, keeping any title after it. Only the
# first one: an older Unreleased heading further down would be somebody's
# mistake, and quietly renaming two of them would hide it.
if grep -qiE '^## +Unreleased' "$changelog"; then
    awk -v ver="$version" '
        !done && tolower($0) ~ /^## +unreleased/ {
            # Keep a trailing "— Title" if there is one.
            title = $0
            sub(/^## +[Uu]nreleased[[:space:]]*/, "", title)
            print (title == "" ? "## " ver : "## " ver " " title)
            done = 1
            next
        }
        { print }
    ' "$changelog" > "${changelog}.tmp"
    mv "${changelog}.tmp" "$changelog"
    echo "bump: renamed '## Unreleased' to '## ${version}'"
fi

# Verify rather than trust. A sed that matched nothing exits 0, so without
# this a typo in a pattern would produce a silent no-op and a release whose
# binary reports the previous version.
wrote_cmake="$(sed -nE 's/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' "$cmake" | head -1)"
wrote_header="$(sed -nE 's/.*kVersion[[:space:]]*=[[:space:]]*"([^"]+)".*/\1/p' "$header" | head -1)"

fail=0
[ "$wrote_cmake" = "$version" ] || { echo "bump: CMakeLists.txt is ${wrote_cmake:-empty}, wanted ${version}" >&2; fail=1; }
[ "$wrote_header" = "$version" ] || { echo "bump: Version.h is ${wrote_header:-empty}, wanted ${version}" >&2; fail=1; }

if ! awk -v ver="## ${version}" 'index($0, ver) == 1 { found = 1 } END { exit !found }' "$changelog"; then
    echo "bump: CHANGELOG.md has no '## ${version}' section" >&2
    echo "bump: write the notes under '## Unreleased' and run this again," >&2
    echo "bump: or add the section by hand. A release with no notes is one" >&2
    echo "bump: nobody can decide whether to install." >&2
    fail=1
fi

[ "$fail" -eq 0 ] || exit 1

echo "bump: ${version} in CMakeLists.txt, Version.h and CHANGELOG.md"
