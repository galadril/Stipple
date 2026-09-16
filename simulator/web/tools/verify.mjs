// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drives the built WASM module under Node, with no browser and no hand-written
// glue beyond what Emscripten generates.
//
// The C++ suite already proves the core behaves. What it cannot prove is that
// the *emulator build* exposes it correctly: a missing export, a stale
// EXPORTED_FUNCTIONS list or a bridge that silently returns nothing would all
// pass 500 unit tests and still leave a dead page in the browser. This closes
// that gap without needing a human to look at a screen.
//
// Run with `.\dev.ps1 verify` after `.\dev.ps1 emulator`.

import { createRequire } from 'node:module';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const publicDir = join(here, '..', 'public');
const loader = join(publicDir, 'notrix-core.js');
const binary = join(publicDir, 'notrix-core.wasm');

if (!existsSync(loader) || !existsSync(binary)) {
    console.error('No emulator build found. Run `.\\dev.ps1 emulator` first.');
    process.exit(2);
}

const require = createRequire(import.meta.url);
const factory = require(loader);

// The module is built with ENVIRONMENT=web and would fetch its own wasm, which
// Node has no origin for. Handing over the bytes keeps this headless.
const core = await factory({ wasmBinary: readFileSync(binary) });

core._notrix_init();
core._notrix_set_wall_clock(1700000000, 0);
core._notrix_render(0);

function request(method, path, body) {
    const status = core.ccall('notrix_http_request', 'number',
        ['string', 'string', 'string'], [method, path, body || '']);
    return {
        status,
        body: core.UTF8ToString(core._notrix_http_body()),
        contentType: core.UTF8ToString(core._notrix_http_content_type()),
        length: core._notrix_http_body_length(),
    };
}

let failures = 0;
function check(label, condition, detail) {
    if (condition) {
        console.log(`  ok    ${label}`);
    } else {
        console.log(`  FAIL  ${label}${detail ? ' -- ' + detail : ''}`);
        failures++;
    }
}

function group(name) {
    console.log(name);
}

group('the UI is served from the firmware');
const index = request('GET', '/');
check('GET / is 200', index.status === 200, `got ${index.status}`);
check('serves html', index.contentType.includes('text/html'), index.contentType);
check('body is the real page', index.body.includes('data-setting="clock.theme"'));
check('reported length matches the body',
      index.length === Buffer.byteLength(index.body, 'utf8'),
      `${index.length} vs ${Buffer.byteLength(index.body, 'utf8')}`);

const css = request('GET', '/app.css');
check('GET /app.css is 200', css.status === 200, `got ${css.status}`);
check('css content type', css.contentType.includes('text/css'), css.contentType);

const script = request('GET', '/app.js');
check('GET /app.js is 200', script.status === 200, `got ${script.status}`);
check('the page can find the emulator bridge', script.body.includes('NOTRIX_BRIDGE'));
check('unknown pages 404', request('GET', '/nope').status === 404);

group('the API answers through the same entry point');
const settings = request('GET', '/api/v1/settings');
check('GET settings is 200', settings.status === 200, `got ${settings.status}`);
const parsed = JSON.parse(settings.body);
check('settings carry the clock block', !!parsed.clock, settings.body.slice(0, 80));
check('colours are hex text', /^#[0-9A-F]{6}$/.test(parsed.clock.color), parsed.clock.color);

group('the UI can actually change the device');
const patch = request('PATCH', '/api/v1/settings',
                      JSON.stringify({ clock: { theme: 'calendar' } }));
check('PATCH accepted', patch.status === 200,
      `${patch.status}: ${patch.body.slice(0, 120)}`);
check('and it stuck',
      JSON.parse(request('GET', '/api/v1/settings').body).clock.theme === 'calendar');

check('an unknown face is refused, not silently defaulted',
      request('PATCH', '/api/v1/settings',
              JSON.stringify({ clock: { theme: 'holographic' } })).status === 422);

check('the panel can be switched off',
      request('PATCH', '/api/v1/settings',
              JSON.stringify({ display: { power: false } })).status === 200);

// The real proof that settings reach the renderer, not just the config store.
core._notrix_render(5000);
const pixels = core.HEAPU8.subarray(
    core._notrix_framebuffer(),
    core._notrix_framebuffer() + core._notrix_width() * core._notrix_height() * 3);
check('and the panel really went dark', pixels.every((byte) => byte === 0));

group('diagnostics');
const logs = request('GET', '/api/v1/logs');
check('GET logs is 200', logs.status === 200, `got ${logs.status}`);
const logBody = JSON.parse(logs.body);
check('boot wrote log lines', logBody.entries.length > 0);
check('lost history is reported', typeof logBody.totalWritten === 'number');
check('mutating calls are logged',
      logBody.entries.some((entry) => entry.message.includes('PATCH')));

group('mqtt');
// Off until asked for: a device must never dial out to a broker on its own.
const mqttBefore = JSON.parse(request('GET', '/api/v1/settings').body).mqtt;
check('mqtt is off by default', mqttBefore.enabled === false);
check('no password is reported', mqttBefore.passwordSet === false);
check('the password field is absent entirely',
      !Object.prototype.hasOwnProperty.call(mqttBefore, 'password'));

check('a wildcard base topic is refused',
      request('PATCH', '/api/v1/settings',
              JSON.stringify({ mqtt: { baseTopic: 'home/#' } })).status === 422);

const enabled = request('PATCH', '/api/v1/settings', JSON.stringify({
    mqtt: { enabled: true, host: 'broker.local', password: 'hunter2-do-not-leak' },
}));
check('mqtt can be configured', enabled.status === 200,
      `${enabled.status}: ${enabled.body.slice(0, 120)}`);

const mqttAfter = JSON.parse(request('GET', '/api/v1/settings').body).mqtt;
check('the device knows a password is set', mqttAfter.passwordSet === true);
check('but never returns it', !enabled.body.includes('hunter2-do-not-leak'));
check('nor on a later read',
      !request('GET', '/api/v1/settings').body.includes('hunter2-do-not-leak'));

// The credential must not reach diagnostics either.
for (const path of ['/api/v1/logs', '/api/v1/diagnostics', '/api/v1/device']) {
    check(`no credential leaks through ${path}`,
          !request('GET', path).body.includes('hunter2-do-not-leak'));
}

const device = JSON.parse(request('GET', '/api/v1/device').body);
check('capabilities are reported', typeof device.capabilities.audio === 'boolean');
check('panel geometry is right',
      device.display.width === 52 && device.display.height === 16);

console.log(failures === 0
    ? '\nAll emulator checks passed.'
    : `\n${failures} emulator check(s) failed.`);
process.exit(failures === 0 ? 0 : 1);
