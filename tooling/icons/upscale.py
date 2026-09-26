#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Scale a device's stored icons up by a whole number, in place.

    python3 tooling/icons/upscale.py --host 192.168.1.238 --password ... --dry-run
    python3 tooling/icons/upscale.py --host 192.168.1.238 --password ... --apply

For icons drawn for a 32x8 panel and now living on a 52x16 one, where an 8x8
glyph uses half the height available to it.

**This makes them bigger, not better.** Nearest neighbour is the only correct
way to scale pixel art - anything smoothing turns crisp edges into mud on a
display where every pixel is a physical LED - but it adds no detail. Each
pixel becomes a 2x2 block, so a doubled 8x8 icon is an 8x8 icon drawn with
chunkier pixels. If the originals came from somewhere that has them at higher
resolution, re-exporting at 16x16 will look better than anything this can do.

Backs up everything it is about to touch first, to a file it names. The device
is the only copy of these, and "replace" is the only write the API has.
"""

import argparse
import base64
import json
import sys
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def call(host, path, password, method="GET", body=None):
    url = "http://%s%s" % (host, path)
    data = json.dumps(body).encode("utf-8") if body is not None else None
    request = urllib.request.Request(url, data=data, method=method)
    request.add_header("Content-Type", "application/json")
    if password:
        token = base64.b64encode(("admin:%s" % password).encode("utf-8")).decode("ascii")
        request.add_header("Authorization", "Basic %s" % token)
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            payload = response.read()
            return json.loads(payload) if payload else None
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", "replace")
        raise SystemExit("upscale: %s %s -> %d %s" % (method, path, error.code, detail))
    except urllib.error.URLError as error:
        raise SystemExit("upscale: cannot reach %s: %s" % (url, error.reason))


def scale(pixels, width, height, factor):
    """Nearest neighbour, which for pixel art is not an approximation."""
    out = []
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        wide = [value for value in row for _ in range(factor)]
        for _ in range(factor):
            out.extend(wide)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="device address")
    parser.add_argument("--password", default="", help="the web password, if one is set")
    parser.add_argument("--factor", type=int, default=2, help="whole-number scale (default 2)")
    parser.add_argument("--max-size", type=int, default=16,
                        help="skip an icon that would exceed this (default 16, the panel height)")
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--dry-run", action="store_true", help="say what would happen")
    action.add_argument("--apply", action="store_true", help="do it")
    args = parser.parse_args()

    if args.factor < 2:
        raise SystemExit("upscale: --factor must be 2 or more")

    listing = call(args.host, "/api/v1/assets", args.password)
    icons = listing.get("assets", [])
    if not icons:
        raise SystemExit("upscale: the device has no icons")

    budget = listing.get("bytesUsed", 0) + listing.get("bytesFree", 0)

    plan = []
    skipped = []
    for icon in icons:
        new_w = icon["width"] * args.factor
        new_h = icon["height"] * args.factor
        if max(new_w, new_h) > args.max_size:
            skipped.append((icon["id"], "%dx%d would exceed %d"
                            % (new_w, new_h, args.max_size)))
            continue
        plan.append(icon)

    after = sum(i["width"] * args.factor * i["height"] * args.factor * 3 * max(1, i.get("frames", 1))
                for i in plan)
    after += sum(i["width"] * i["height"] * 3 * max(1, i.get("frames", 1))
                 for i in icons if i not in plan)

    print("icons on the device : %d" % len(icons))
    print("would be scaled     : %d (x%d)" % (len(plan), args.factor))
    for name, why in skipped:
        print("  skipped %-10s %s" % (name, why))
    print("bytes after         : %d of %d" % (after, budget))

    if after > budget:
        raise SystemExit(
            "upscale: that would need %d bytes and the device allows %d.\n"
            "         Raise IconStore::kMaxTotalBytes, or scale fewer of them."
            % (after, budget))

    if args.dry_run:
        print("\nNothing was changed. Re-run with --apply.")
        return 0

    # The device is the only copy. Whatever happens next, this file is what
    # puts them back.
    stamp = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
    backup_dir = ROOT / "restore" / "icons"
    backup_dir.mkdir(parents=True, exist_ok=True)
    backup = backup_dir / ("icons-%s.json" % stamp)

    originals = []
    for icon in icons:
        full = call(args.host, "/api/v1/assets/%s" % icon["id"], args.password)
        originals.append(full)
    backup.write_text(json.dumps(originals, indent=1), encoding="utf-8")
    print("\nbacked up %d icons to %s" % (len(originals), backup.relative_to(ROOT)))

    changed = 0
    for full in originals:
        if not any(p["id"] == full["id"] for p in plan):
            continue

        frames = [scale(frame, full["width"], full["height"], args.factor)
                  for frame in full["pixels"]]
        body = {
            "id": full["id"],
            "width": full["width"] * args.factor,
            "height": full["height"] * args.factor,
            "frameMillis": full.get("frameMillis", 100),
            "frames": frames,
        }
        if full.get("transparent") is not None:
            body["transparent"] = full["transparent"]

        call(args.host, "/api/v1/assets", args.password, method="POST", body=body)
        changed += 1
        print("  %-10s %dx%d -> %dx%d" % (full["id"], full["width"], full["height"],
                                          body["width"], body["height"]))

    after_listing = call(args.host, "/api/v1/assets", args.password)
    print("\n%d icons scaled. Device now holds %d icons, %d bytes."
          % (changed, after_listing.get("count", 0), after_listing.get("bytesUsed", 0)))
    print("Originals are in %s if you want them back." % backup.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
