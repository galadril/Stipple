// SPDX-License-Identifier: GPL-3.0-or-later
//
// Catches the failure that got this written: a call to a function that no
// longer exists.
//
// `node --check` only parses - it happily accepts a call to something
// undefined, because that is a runtime error in JavaScript. The device web UI
// has no build step and no test runner, so nothing else was looking, and an
// edit that removed two functions shipped a page that died on load with
// "wireControls is not defined".
//
// Deliberately crude: extract every identifier that is called, subtract the
// ones declared here, subtract known globals, and complain about the rest. It
// will never catch a typo'd property access, and it does not need to - it
// catches the one class of mistake that takes the whole page down.
//
//     node tooling/web/check-app.mjs
//
// It lives here rather than under firmware/web/ because everything in that
// directory is compiled into the firmware image, and a development checker has
// no business on a device with 8 MiB of flash. EmbedWebAssets refused it, which
// is the build system working.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const source = readFileSync(join(here, '..', '..', 'firmware', 'web', 'app.js'), 'utf8');

// Strip comments and strings first, so a function name mentioned in prose or
// inside a template does not read as a definition or a call.
const code = source
    .replace(/\/\*[\s\S]*?\*\//g, ' ')
    .replace(/\/\/[^\n]*/g, ' ')
    .replace(/'(?:[^'\\]|\\.)*'/g, "''")
    .replace(/"(?:[^"\\]|\\.)*"/g, '""');

const defined = new Set();
for (const m of code.matchAll(/\bfunction\s+([A-Za-z_$][\w$]*)\s*\(/g)) {
    defined.add(m[1]);
}
// `var name = function (...)` and `var name = (...) => ...`
for (const m of code.matchAll(/\b(?:var|let|const)\s+([A-Za-z_$][\w$]*)\s*=\s*(?:function\b|\()/g)) {
    defined.add(m[1]);
}
// Plain variables, so a call through one is not reported.
for (const m of code.matchAll(/\b(?:var|let|const)\s+([A-Za-z_$][\w$]*)/g)) {
    defined.add(m[1]);
}
// Parameters, which are callable when a callback is passed in.
for (const m of code.matchAll(/function\s*[A-Za-z_$\w]*\s*\(([^)]*)\)/g)) {
    for (const part of m[1].split(',')) {
        const name = part.trim();
        if (name) { defined.add(name); }
    }
}

const known = new Set([
    'if', 'for', 'while', 'switch', 'catch', 'return', 'typeof', 'function',
    'new', 'delete', 'void', 'in', 'of', 'do', 'else', 'try',
    'Array', 'Object', 'String', 'Number', 'Boolean', 'Math', 'JSON', 'Date',
    'Promise', 'Error', 'RegExp', 'Set', 'Map', 'Uint8Array', 'Blob', 'URL',
    'parseInt', 'parseFloat', 'isNaN', 'atob', 'btoa', 'encodeURIComponent',
    'decodeURIComponent', 'setTimeout', 'setInterval', 'clearTimeout',
    'clearInterval', 'fetch', 'require', 'document', 'window', 'console',
    'Option', 'FileReader', 'Image',
]);

const missing = new Map();
for (const m of code.matchAll(/(^|[^.\w$])([A-Za-z_$][\w$]*)\s*\(/g)) {
    const name = m[2];
    if (defined.has(name) || known.has(name)) { continue; }
    missing.set(name, (missing.get(name) ?? 0) + 1);
}

if (missing.size === 0) {
    console.log('app.js: every called function is defined');
    process.exit(0);
}

console.error('app.js calls functions that are not defined:');
for (const [name, count] of [...missing].sort()) {
    console.error(`  ${name}  (${count} call${count === 1 ? '' : 's'})`);
}
process.exit(1);
