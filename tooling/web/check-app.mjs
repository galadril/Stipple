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

if (missing.size > 0) {
    console.error('app.js calls functions that are not defined:');
    for (const [name, count] of [...missing].sort()) {
        console.error(`  ${name}  (${count} call${count === 1 ? '' : 's'})`);
    }
    process.exit(1);
}

console.log(`check-app: ${defined.size} definitions, every call accounted for`);

// --- Third check: does the highlighter give back what it was given? ---------
//
// The editor draws the source twice: once in a textarea whose text is
// transparent, and once in a <pre> behind it. The caret, the selection and
// every click land according to the textarea; the glyphs a person sees come
// from the <pre>. The two only agree while the <pre> contains exactly the
// same characters, in the same order, as the textarea.
//
// So the one thing that must never happen is a tokeniser that drops, adds or
// reorders a character. A dropped tab is invisible in the output and moves
// every caret position on that line by two columns, which reads as the editor
// being haunted rather than as a bug in a function nobody is looking at.
//
// This runs highlightBerry over a set of awkward inputs, strips the tags it
// added, and requires the result to be byte-identical to the input.

const extract = /function highlightBerry\(source\)[\s\S]*?\n    \}\n/.exec(source);
if (!extract) {
    console.error('check-app: highlightBerry is gone from app.js');
    console.error('The script editor would show an empty layer behind the textarea.');
    process.exit(1);
}

// The helpers it leans on, lifted the same way.
const helpers = ['escapeHtml', 'span', 'isWordStart', 'isWord', 'isDigit']
    .map((name) => {
        const found = new RegExp(
            `function ${name}\\([\\s\\S]*?\\n    \\}\\n`).exec(source);
        if (!found) {
            console.error(`check-app: highlightBerry's helper ${name} is gone`);
            process.exit(1);
        }
        return found[0];
    })
    .join('\n');

const tables = ['BERRY_KEYWORDS', 'BERRY_BUILTINS']
    .map((name) => {
        const found = new RegExp(`var ${name} = \\{[\\s\\S]*?\\n    \\};`).exec(source);
        if (!found) {
            console.error(`check-app: the ${name} table is gone`);
            process.exit(1);
        }
        return found[0];
    })
    .join('\n');

let highlightBerry;
try {
    // eslint-disable-next-line no-new-func
    highlightBerry = new Function(
        `${tables}\n${helpers}\n${extract[0]}\nreturn highlightBerry;`)();
} catch (error) {
    console.error(`check-app: highlightBerry will not load: ${error.message}`);
    process.exit(1);
}

const strip = (html) => html
    .replace(/<[^>]*>/g, '')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&amp;/g, '&');

const cases = [
    ['empty', ''],
    ['one line', 'var x = 1'],
    ['a comment', '# name: Big Clock\nvar x = 1'],
    ['tabs', 'class App\n\tdef draw()\n\t\tpixel(0, 0, rgb(1, 2, 3))\n\tend\nend'],
    ['both quotes', `text(0, 0, 'a', rgb(1,2,3))\ntext(0, 0, "b", rgb(1,2,3))`],
    ['an escaped quote', `var s = 'it\\'s fine'`],
    // The case that made the string scanner stop at newlines: somebody
    // mid-edit has one open quote most of the time.
    ['an unterminated string', `var s = 'open\nvar t = 2`],
    ['a hash inside a string', `var s = "# not a comment"`],
    ['angle brackets', 'if a < b && c > d\nend'],
    ['an ampersand', 'var s = "a && b"'],
    ['numbers', 'var a = 0\nvar b = 3.14\nvar c = 0xFF\nvar d = 1e3'],
    ['trailing newline', 'var x = 1\n'],
    ['blank lines', '\n\n\nvar x = 1\n\n\n'],
    ['windows line endings', 'var x = 1\r\nvar y = 2\r\n'],
    ['a lone hash at the end', 'var x = 1\n#'],
    ['unicode', 'text(0, 0, "café — °C", rgb(1,2,3))'],
];

let failures = 0;
for (const [label, input] of cases) {
    const back = strip(highlightBerry(input));
    if (back !== input) {
        console.error(`check-app: highlightBerry changed the text (${label})`);
        console.error(`  in:  ${JSON.stringify(input)}`);
        console.error(`  out: ${JSON.stringify(back)}`);
        failures += 1;
    }
}

// And it must actually colour something, or the check above passes trivially
// on a function that returns its input.
if (!/tk-key/.test(highlightBerry('def draw()')) ||
    !/tk-com/.test(highlightBerry('# hello')) ||
    !/tk-str/.test(highlightBerry(`var s = 'x'`)) ||
    !/tk-fn/.test(highlightBerry('pixel(0, 0, 0)')) ||
    !/tk-num/.test(highlightBerry('var n = 42'))) {
    console.error('check-app: highlightBerry is not highlighting anything');
    failures += 1;
}

// Raw < and & must not reach the DOM as markup. A script containing
// "<img onerror=...>" in a comment would otherwise run it in the editor.
const injected = highlightBerry('# <img src=x onerror="alert(1)">');
if (/<img/.test(injected)) {
    console.error('check-app: highlightBerry does not escape markup');
    failures += 1;
}

if (failures > 0) {
    process.exit(1);
}
console.log('check-app: highlightBerry round-trips every awkward input');

