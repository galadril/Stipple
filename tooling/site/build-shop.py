#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the script shop page from scripts/.

    python3 tooling/site/build-shop.py

The shop is the directory. There is no database, no upload form and no
server: a script is a file in the repository, a submission is a pull request,
and this turns the one into a page. That is not a limitation worked around -
it is what lets every published script be compiled and run by the test suite
before it reaches anybody, which a form could never promise.

Metadata lives in comment lines at the top of each script, so a file stays a
single thing you can download and paste into the editor without stripping a
header off it first:

    # name: Big Clock
    # summary: One sentence, shown in the listing.
    # author: Who wrote it
    # tags: clock, time
    # panel: 52x16

Deliberately no template engine and no Markdown library. This runs in CI,
which must not install anything (ADR 0012's reasoning applied to tooling), and
the page is one shape repeated - a templating dependency would be bought for
nothing.
"""

import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
OUT = ROOT / "site" / "shop" / "index.html"

# Kept in step with firmware/tests/test_shop_scripts.cpp by check-shop below.
# A script on the page that the tests do not run is a script nobody has
# checked, and it would look exactly like one they had.
TEST_FILE = ROOT / "firmware" / "tests" / "test_shop_scripts.cpp"

FIELD = re.compile(r"^#\s*([a-z]+):\s*(.+?)\s*$")


def parse(path):
    """Metadata and body for one script."""
    text = path.read_text(encoding="utf-8")
    meta = {"file": path.name}

    for line in text.splitlines():
        if not line.startswith("#"):
            # The header ends at the first line that is not a comment. A
            # `# tags:` written halfway down the file is a comment about the
            # code there, not metadata, and picking it up would be a guess.
            if line.strip() == "":
                continue
            break
        match = FIELD.match(line)
        if match:
            meta[match.group(1)] = match.group(2)

    meta["source"] = text
    meta["lines"] = len(text.splitlines())
    meta["bytes"] = len(text.encode("utf-8"))
    return meta


def required(meta, field):
    if field not in meta or not meta[field]:
        raise SystemExit(
            "build-shop: %s has no '# %s:' line" % (meta["file"], field))
    return meta[field]


def card(meta):
    name = html.escape(required(meta, "name"))
    summary = html.escape(required(meta, "summary"))
    author = html.escape(meta.get("author", "unattributed"))
    tags = [t.strip() for t in meta.get("tags", "").split(",") if t.strip()]

    chips = "".join(
        '<li>%s</li>' % html.escape(tag) for tag in tags)

    return """
      <article class="card" id="{anchor}">
        <header class="card__head">
          <h3>{name}</h3>
          <p class="card__by">{author} · {lines} lines</p>
        </header>
        <p class="card__summary">{summary}</p>
        <ul class="card__tags">{chips}</ul>
        <details class="card__source">
          <summary>Read it</summary>
          <pre><code>{source}</code></pre>
        </details>
        <p class="card__get">
          <a href="https://github.com/galadril/Stipple/blob/main/scripts/{file}">View on GitHub</a>
          ·
          <a href="https://raw.githubusercontent.com/galadril/Stipple/main/scripts/{file}">Raw</a>
        </p>
      </article>
""".format(
        anchor=html.escape(Path(meta["file"]).stem),
        name=name,
        author=author,
        lines=meta["lines"],
        summary=summary,
        chips=chips,
        source=html.escape(meta["source"]),
        file=html.escape(meta["file"]),
    )


PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Scripts — Stipple</title>
<meta name="description" content="Berry scripts for the Stipple firmware. Every one is compiled and run by the test suite before it is published.">
<link rel="icon" href="../favicon.svg" type="image/svg+xml">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Archivo:wght@400;500;600;800&display=swap" rel="stylesheet">
<link rel="stylesheet" href="../stipple.css">
<link rel="stylesheet" href="shop.css">
</head>
<body>

<a class="skip" href="#main">Skip to content</a>

<header class="bar">
  <a class="bar__brand" href="../">
    <span class="bar__mark" aria-hidden="true"></span>
    <span class="bar__name">Stipple</span>
  </a>
  <nav class="bar__nav" aria-label="Main">
    <a href="../emulator/">Emulator</a>
    <a href="../#install">Install</a>
    <a href="../api/">API</a>
    <a href="https://github.com/galadril/Stipple">Source</a>
  </nav>
</header>

<main id="main" class="shop">

  <section class="shop__lede">
    <h1>Scripts</h1>
    <p>
      A script is a Berry class with a <code>draw()</code> method. It runs on
      the device, inside a sandbox with no filesystem and no loader, under a
      budget that stops a runaway loop costing anything more than its own
      frame.
    </p>
    <p>
      Copy one into <strong>Scripts</strong> in your device's web page and
      save. It joins the carousel immediately. Or try it in the
      <a href="../emulator/">emulator</a> first &mdash; that runs the same
      interpreter, so what you see there is what the panel does.
    </p>
    <p class="shop__gate">
      Every script here is compiled and run by the test suite before it is
      published: ninety frames on a real 52&nbsp;&times;&nbsp;16 framebuffer,
      buttons pressed, then six hundred more frames checked for leaks. Nothing
      reaches this page without surviving that.
    </p>
  </section>

  <section class="shop__grid">
{cards}
  </section>

  <section class="shop__submit">
    <h2>Add one</h2>
    <p>
      Open a pull request that adds a <code>.be</code> file to
      <a href="https://github.com/galadril/Stipple/tree/main/scripts">scripts/</a>
      and a line naming it in
      <code>firmware/tests/test_shop_scripts.cpp</code>. This page is built
      from that directory, so there is nothing else to update.
    </p>
    <p>
      Start the file with the header the others have &mdash;
      <code>name</code>, <code>summary</code>, <code>author</code>,
      <code>tags</code> &mdash; and the build will tell you if anything is
      missing.
    </p>
    <p>
      Two things the tests will hold you to, both learned the hard way. Text
      that runs past pixel&nbsp;51 is clipped without complaint, so measure it
      rather than centring by eye. And if your script shows the time or the
      battery, check <code>time_known()</code> and
      <code>battery_known()</code> first: a device that has never synchronised
      its clock does not have a time, and drawing 00:00 invents one.
    </p>
  </section>

</main>

<footer class="foot">
  <p>
    Stipple is GPL-3.0-or-later. Scripts here are published under the same
    licence unless their header says otherwise.
  </p>
</footer>

</body>
</html>
"""


def main():
    if not SCRIPTS.is_dir():
        raise SystemExit("build-shop: no scripts/ directory")

    files = sorted(SCRIPTS.glob("*.be"))
    if not files:
        raise SystemExit("build-shop: scripts/ is empty")

    # Every published script must be one the tests run. A page listing a
    # script the suite does not touch is a page making a promise the project
    # has not kept - and it would look identical to one that had.
    tested = TEST_FILE.read_text(encoding="utf-8") if TEST_FILE.exists() else ""
    untested = [f.name for f in files if '"%s"' % f.name not in tested]
    if untested:
        raise SystemExit(
            "build-shop: not named in test_shop_scripts.cpp, so never run: %s"
            % ", ".join(untested))

    cards = "".join(card(parse(path)) for path in files)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    # Newlines pinned to LF. Without it Python translates to CRLF on Windows,
    # so the file a Windows developer regenerates differs from the one Linux
    # CI regenerates - and the diff check that exists to catch real drift
    # would fail on every line instead.
    with open(OUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(PAGE.format(cards=cards))
    print("build-shop: %d scripts -> %s" % (len(files), OUT.relative_to(ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
