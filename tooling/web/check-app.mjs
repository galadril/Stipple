// SPDX-License-Identifier: GPL-3.0-or-later
//
// Two checks on the device's web UI, which has no build step and no test
// runner, so nothing else is looking.
//
// **Does it parse.** An edit that put a real newline inside a string literal
// shipped a configuration page that would not load at all. That page is the
// one thing you need when the device is misbehaving, and it is compiled into
// the firmware.
//
// **Does it call anything that does not exist.** Parsing alone happily
// accepts that, because it is a runtime error in JavaScript - and an edit
// that removed two functions shipped a page dying on load with
// "wireControls is not defined", which is what got this written.
//
//     node tooling/web/check-app.mjs
//
// It lives here rather than under firmware/web/ because everything in that
// directory is compiled into the firmware image, and a development checker has
// no business on a device with 8 MiB of flash. EmbedWebAssets refused it, which
// is the build system working.
//
// **The stripping is a scanner, not a stack of regexes, and it has to be.**
// The first version replaced block comments, then line comments, then strings,
// in that order. Any `//` inside a string then began a comment that ate the
// closing quote, after which the string pattern paired the wrong quotes and
// swallowed whole functions. It was discarding more than half of app.js and
// reporting `$`, `send` and `toast` as undefined - and because it reported
// something every time, the signal was indistinguishable from the noise. It
// had been failing CI for at least six commits.
//
// One pass, tracking what it is inside, cannot get that wrong.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const path = join(here, '..', '..', 'firmware', 'web', 'app.js');
const source = readFileSync(path, 'utf8');

// Does it parse at all?
//
// This check was built to catch calls to functions that no longer exist, and
// said nothing about syntax - so an edit that put a real newline inside a
// string literal shipped a page that would not load. The device's web UI is
// the one thing you need when the device is misbehaving, and it is compiled
// into the firmware, so a broken app.js is a flash away from being fixed.
//
// `new Function` parses without running: a syntax error throws here, a call
// to something undefined does not. Both matter, and they are different
// checks.
try {
    // eslint-disable-next-line no-new-func
    new Function(source);
} catch (error) {
    console.error(`app.js does not parse: ${error.message}`);
    console.error('The device would serve a configuration page that dies on load.');
    process.exit(1);
}

/// Replace every comment, string and regex literal with whitespace, keeping
/// the length and the line structure so anything reported still lines up.
function stripLiterals(text) {
    const out = [];
    let i = 0;

    // Whether a `/` here starts a regex or is division. A regex can only
    // follow an operator, a keyword or an opening bracket - never a value.
    let regexAllowed = true;

    const keep = (ch) => out.push(ch === '\n' ? '\n' : ' ');

    while (i < text.length) {
        const ch = text[i];
        const next = text[i + 1];

        if (ch === '/' && next === '*') {
            i += 2;
            while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) {
                keep(text[i]);
                i += 1;
            }
            i += 2;
            out.push(' ', ' ');
            regexAllowed = true;
            continue;
        }

        if (ch === '/' && next === '/') {
            while (i < text.length && text[i] !== '\n') {
                keep(text[i]);
                i += 1;
            }
            regexAllowed = true;
            continue;
        }

        if (ch === '"' || ch === "'" || ch === '`') {
            const quote = ch;
            out.push(' ');
            i += 1;
            while (i < text.length && text[i] !== quote) {
                // An escape consumes the next character whatever it is, which
                // is what stops a backslash-quote from ending the string.
                if (text[i] === '\\') {
                    keep(text[i]);
                    i += 1;
                    if (i < text.length) {
                        keep(text[i]);
                        i += 1;
                    }
                    continue;
                }
                keep(text[i]);
                i += 1;
            }
            out.push(' ');
            i += 1;
            regexAllowed = false;
            continue;
        }

        if (ch === '/' && regexAllowed) {
            // A regex literal. Character classes matter: `/[/]/` is legal and
            // the `/` inside the class does not end it.
            let inClass = false;
            out.push(' ');
            i += 1;
            while (i < text.length) {
                const c = text[i];
                if (c === '\\') {
                    keep(c);
                    i += 1;
                    if (i < text.length) { keep(text[i]); i += 1; }
                    continue;
                }
                if (c === '[') { inClass = true; }
                else if (c === ']') { inClass = false; }
                else if (c === '/' && !inClass) { break; }
                else if (c === '\n') { break; }
                keep(c);
                i += 1;
            }
            out.push(' ');
            i += 1;
            regexAllowed = false;
            continue;
        }

        if (!/\s/.test(ch)) {
            // After a value, a `/` is division; after anything else it can
            // start a regex.
            regexAllowed = !/[A-Za-z0-9_$)\]]/.test(ch);
        }
        out.push(ch);
        i += 1;
    }

    return out.join('');
}

const code = stripLiterals(source);

// A sanity check on the scanner itself, because a stripper that quietly ate
// the file is precisely the failure this tool just had. If most of the source
// has vanished, the answer is not "everything is undefined".
const kept = code.replace(/\s+/g, '').length;
const original = source.replace(/\s+/g, '').length;
if (kept < original * 0.4) {
    console.error(`check-app: the scanner discarded ${Math.round(100 - (kept / original) * 100)}% ` +
                  'of app.js, which means it is broken rather than the page');
    process.exit(2);
}

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
// Arrow parameters, single and parenthesised.
for (const m of code.matchAll(/\(([^()]*)\)\s*=>/g)) {
    for (const part of m[1].split(',')) {
        const name = part.trim();
        if (name) { defined.add(name); }
    }
}
for (const m of code.matchAll(/\b([A-Za-z_$][\w$]*)\s*=>/g)) {
    defined.add(m[1]);
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
    // Browser globals the page actually calls. `confirm` was missing, which
    // meant the tool reported one real-looking name on every run - a standing
    // false positive is how a check stops being read.
    'confirm', 'alert', 'prompt', 'navigator', 'location', 'localStorage',
    'requestAnimationFrame', 'cancelAnimationFrame', 'queueMicrotask',
    'CustomEvent', 'Event', 'AbortController', 'TextDecoder', 'TextEncoder',
    'WebSocket', 'EventSource', 'structuredClone', 'isFinite', 'Symbol',
]);

// The lookbehind is what keeps this useful rather than noisy.
//
// Without it, `thing.appendChild(...)` matches and reports `appendChild` as
// an undefined function - and so does every other method call in the file,
// sixty-odd names of pure noise that bury the one real answer. A method call
// is the object's problem, not this file's; only a bare call can refer to
// something that was supposed to be declared here.
const missing = new Map();
for (const m of code.matchAll(/(?<![.\w$])([A-Za-z_$][\w$]*)\s*\(/g)) {
    const name = m[1];
    if (defined.has(name) || known.has(name)) { continue; }
    missing.set(name, (missing.get(name) || 0) + 1);
}

if (missing.size === 0) {
    console.log(`check-app: ${defined.size} definitions, every call accounted for`);
    process.exit(0);
}

console.error('app.js calls functions that are not defined:');
for (const [name, count] of [...missing].sort()) {
    console.error(`  ${name}  (${count} call${count === 1 ? '' : 's'})`);
}
process.exit(1);
