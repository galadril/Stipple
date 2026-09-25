// SPDX-License-Identifier: GPL-3.0-or-later
//
// Checks the website's panel data without a browser.
//
// The site draws real device frames and sets its clock in the device's own
// 5x7 font, and both come from generated data. Generated data is exactly the
// kind of thing that rots silently: a fixture changes size, the font table
// gains a column, and the page renders nonsense that nobody notices because
// it still renders *something*.
//
//     node tooling/site/check-site.mjs
//
// Pass --show to print the frames as ASCII, which is how you review what the
// hero actually draws when you have no browser to hand.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const site = join(here, '..', '..', 'site');

const { PANEL, FRAMES, FONT } = await import(
  'file://' + join(site, 'frames.js').replace(/\\/g, '/')
);

const W = PANEL.width;
const H = PANEL.height;
const show = process.argv.includes('--show');

let failures = 0;
const fail = (message) => {
  console.error('  ' + message);
  failures += 1;
};

/* --- the panel ------------------------------------------------------------ */

if (W !== 52 || H !== 16) {
  fail(`panel is ${W}x${H}, expected 52x16`);
}

for (const frame of FRAMES) {
  const bytes = Buffer.from(frame.rgb, 'base64');
  if (bytes.length !== W * H * 3) {
    fail(`${frame.id}: ${bytes.length} bytes, expected ${W * H * 3}`);
  }
  if (!frame.caption || frame.caption.length < 4) {
    fail(`${frame.id}: no caption, so it would render as an unlabelled image`);
  }
  // A frame that is entirely black is a fixture that failed to generate.
  if (bytes.every((b) => b === 0)) {
    fail(`${frame.id}: every pixel is black`);
  }
}

/* --- the font ------------------------------------------------------------- */

const TRACKING = 1;
const glyph = (ch) => FONT[ch.codePointAt(0)] || null;

const measure = (text) => {
  let width = 0;
  for (const ch of text) {
    const g = glyph(ch);
    if (g) width += g[0] + TRACKING;
  }
  return Math.max(0, width - TRACKING);
};

// Every character the clock can produce must exist, or the time would render
// with holes in it and still look like a clock.
for (const ch of '0123456789:') {
  if (!glyph(ch)) fail(`the font has no glyph for '${ch}'`);
}

// 23:59 is the widest time, and it has to fit across the panel.
const widest = measure('23:59');
if (widest > W) {
  fail(`the widest time is ${widest}px on a ${W}px panel`);
}

/* --- render a clock, the way the page does -------------------------------- */

function clockAscii(hh, mm) {
  const grid = Array.from({ length: H }, () => Array(W).fill(' '));
  const text = `${hh}:${mm}`;
  let cursor = Math.round((W - measure(text)) / 2);

  for (const ch of text) {
    const g = glyph(ch);
    if (!g) continue;
    for (let row = 0; row < 7; row++) {
      for (let col = 0; col < 5; col++) {
        if (g[row + 1] & (1 << (4 - col))) {
          const x = cursor + col;
          if (x >= 0 && x < W) grid[1 + row][x] = ch === ':' ? '+' : '#';
        }
      }
    }
    cursor += g[0] + TRACKING;
  }

  // The seconds bar, at 45 seconds.
  const filled = Math.round((W * 45) / 60);
  for (let x = 0; x < filled; x++) {
    grid[14][x] = '+';
    grid[15][x] = '+';
  }
  return grid.map((row) => row.join('')).join('\n');
}

const rendered = clockAscii('23', '59');
// If the glyphs were misaligned the panel would come back empty.
const lit = [...rendered].filter((c) => c === '#').length;
if (lit < 40) {
  fail(`the clock rendered only ${lit} lit pixels, which means the font is misaligned`);
}

if (show) {
  console.log('\nThe hero clock, as the page draws it:\n');
  console.log(rendered);
  console.log();
  for (const frame of FRAMES) {
    const bytes = Buffer.from(frame.rgb, 'base64');
    console.log(`\n${frame.id} — ${frame.caption}`);
    for (let y = 0; y < H; y++) {
      let line = '';
      for (let x = 0; x < W; x++) {
        const i = (y * W + x) * 3;
        const sum = bytes[i] + bytes[i + 1] + bytes[i + 2];
        line += sum === 0 ? '.' : sum > 360 ? '#' : '+';
      }
      console.log(line);
    }
  }
}

if (failures) {
  console.error(`\ncheck-site: ${failures} problem(s)`);
  process.exit(1);
}

console.log(
  `check-site: ${FRAMES.length} frames, ${Object.keys(FONT).length} glyphs, ` +
    `widest time ${widest}px of ${W}, ${lit} lit pixels in 23:59`
);
