// SPDX-License-Identifier: GPL-3.0-or-later
//
// Emulator front-end. Owns the frame clock, the LED look, and the controls.
// It owns no application logic: the app carousel, scene parsing, rendering and
// input mapping all happen inside the WASM core.

(function () {
    'use strict';

    // Blueprint §9.4: 20-30 FPS for animation. The TC002 display API is
    // throttled at roughly 15 ms per frame, so pacing the emulator at 60 would
    // let us build animations the hardware cannot keep up with.
    var TARGET_FPS = 30;
    var FRAME_MS = 1000 / TARGET_FPS;

    var PITCH = 13;      // css px between LED centres
    var DOT = 5.4;       // lit dot diameter
    var UNLIT = '#16181d';
    var BOARD = '#08090b';

    // Mirrors notrix::platform::RawInput and ButtonPhase.
    var PHASE_DOWN = 0;
    var PHASE_UP = 1;
    var PHASE_TICK = 2;

    var el = {
        panel: document.getElementById('panel'),
        notice: document.getElementById('notice'),
        play: document.getElementById('play'),
        step: document.getElementById('step'),
        shot: document.getElementById('shot'),
        brightness: document.getElementById('brightness'),
        brightnessValue: document.getElementById('brightness-value'),
        statApp: document.getElementById('stat-app'),
        statDwell: document.getElementById('stat-dwell'),
        statQueue: document.getElementById('stat-queue'),
        statFrames: document.getElementById('stat-frames'),
        notifyLow: document.getElementById('notify-low'),
        notifyHigh: document.getElementById('notify-high'),
        statRender: document.getElementById('stat-render'),
        statFps: document.getElementById('stat-fps'),
        statCore: document.getElementById('stat-core'),
        geometry: document.getElementById('chip-geometry')
    };

    var ctx = el.panel.getContext('2d');

    var core = null;
    var width = 52;
    var height = 16;
    var framebufferPtr = 0;

    var bloomCanvas = document.createElement('canvas');
    var bloomCtx = bloomCanvas.getContext('2d');
    var bloomData = null;

    var startedAt = 0;
    var accumulator = 0;
    var lastTimestamp = 0;
    var renderMs = 0;
    var fpsFrames = 0;
    var fpsSince = 0;
    var statsSince = 0;

    /// Monotonic milliseconds since start, which is what the core's clock and
    /// carousel are driven by.
    function nowMillis() {
        return Math.max(0, Math.round(performance.now() - startedAt));
    }

    // --- canvas sizing ------------------------------------------------------

    function resizeCanvas() {
        var dpr = window.devicePixelRatio || 1;
        var cssWidth = width * PITCH;
        var cssHeight = height * PITCH;

        el.panel.width = Math.round(cssWidth * dpr);
        el.panel.height = Math.round(cssHeight * dpr);
        el.panel.style.aspectRatio = cssWidth + ' / ' + cssHeight;

        ctx = el.panel.getContext('2d');
        ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    }

    // --- drawing ------------------------------------------------------------

    function drawBoard() {
        ctx.fillStyle = BOARD;
        ctx.fillRect(0, 0, width * PITCH, height * PITCH);
    }

    function drawUnlitPanel() {
        drawBoard();
        ctx.fillStyle = UNLIT;
        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                ctx.beginPath();
                ctx.arc(x * PITCH + PITCH / 2, y * PITCH + PITCH / 2, DOT / 2, 0, Math.PI * 2);
                ctx.fill();
            }
        }
    }

    function drawFramebuffer(bytes) {
        drawBoard();

        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                var i = (y * width + x) * 3;
                var r = bytes[i];
                var g = bytes[i + 1];
                var b = bytes[i + 2];

                ctx.beginPath();
                ctx.arc(x * PITCH + PITCH / 2, y * PITCH + PITCH / 2, DOT / 2, 0, Math.PI * 2);
                ctx.fillStyle = (r === 0 && g === 0 && b === 0)
                    ? UNLIT
                    : 'rgb(' + r + ',' + g + ',' + b + ')';
                ctx.fill();

                var o = (y * width + x) * 4;
                bloomData.data[o] = r;
                bloomData.data[o + 1] = g;
                bloomData.data[o + 2] = b;
                bloomData.data[o + 3] = 255;
            }
        }

        // Cheap bloom: upscale the raw 52x16 frame with a blur and add it back.
        // One composited drawImage rather than 832 shadowBlur operations.
        bloomCtx.putImageData(bloomData, 0, 0);
        ctx.save();
        ctx.globalCompositeOperation = 'lighter';
        ctx.globalAlpha = 0.40;
        ctx.filter = 'blur(7px)';
        ctx.drawImage(bloomCanvas, 0, 0, width * PITCH, height * PITCH);
        ctx.restore();
    }

    // --- core bridge --------------------------------------------------------

    function readFramebuffer() {
        // ALLOW_MEMORY_GROWTH can detach the old heap view, so re-read it.
        return core.HEAPU8.subarray(framebufferPtr, framebufferPtr + width * height * 3);
    }

    function renderFrame() {
        var start = performance.now();
        core._notrix_render(nowMillis());
        drawFramebuffer(readFramebuffer());
        renderMs = performance.now() - start;
    }

    function sendInput(source, phase) {
        core._notrix_input(source, phase, nowMillis());
        renderFrame();
        syncPauseLabel();
    }

    function syncPauseLabel() {
        el.play.textContent = core._notrix_is_paused() ? 'Resume rotation' : 'Pause rotation';
    }

    // --- stats --------------------------------------------------------------

    function updateStats(now) {
        el.statApp.textContent = core.UTF8ToString(core._notrix_active_name()) || '-';

        if (core._notrix_showing_splash()) {
            el.statDwell.textContent = 'booting';
        } else {
            var dwell = core._notrix_dwell_millis(nowMillis()) / 1000;
            var duration = core._notrix_active_duration_seconds();
            el.statDwell.textContent = duration > 0
                ? dwell.toFixed(1) + 's / ' + duration + 's'
                : '-';
        }

        var queued = core._notrix_notification_count();
        var pending = core._notrix_notification_pending();
        el.statQueue.textContent = queued === 0
            ? '0'
            : (queued - pending) + ' + ' + pending;

        // Dirty rendering made visible: a static screen should skip far more
        // frames than it draws.
        var drawn = core._notrix_frames_rendered();
        var skipped = core._notrix_frames_skipped();
        el.statFrames.textContent = drawn + ' / ' + (drawn + skipped);

        el.statRender.textContent = renderMs.toFixed(2) + ' ms';

        var elapsed = now - fpsSince;
        if (elapsed > 0) {
            el.statFps.textContent = (fpsFrames * 1000 / elapsed).toFixed(1) + ' fps';
        }
        fpsFrames = 0;
        fpsSince = now;
    }

    // --- frame clock --------------------------------------------------------

    function tick(now) {
        var delta = now - lastTimestamp;
        lastTimestamp = now;

        accumulator += delta;
        if (accumulator >= FRAME_MS) {
            // Cap catch-up so returning to a backgrounded tab does not
            // fast-forward a burst of frames at once.
            var steps = Math.min(Math.floor(accumulator / FRAME_MS), 4);
            accumulator -= steps * FRAME_MS;
            if (accumulator > FRAME_MS * 4) {
                accumulator = 0;
            }
            renderFrame();
            fpsFrames++;
        }

        if (now - statsSince > 250) {
            updateStats(now);
            statsSince = now;
        }

        requestAnimationFrame(tick);
    }

    // --- controls -----------------------------------------------------------

    function wireControls() {
        // Middle button short-press toggles pause in the core's default input
        // map, so the on-screen control sends exactly that.
        el.play.addEventListener('click', function () {
            sendInput(1, PHASE_DOWN);
            sendInput(1, PHASE_UP);
        });

        el.step.addEventListener('click', function () {
            sendInput(2, PHASE_DOWN);
            sendInput(2, PHASE_UP);
        });

        el.brightness.addEventListener('input', function () {
            var value = parseInt(el.brightness.value, 10);
            el.brightnessValue.textContent = String(value);
            core._notrix_set_brightness(value);
            renderFrame();
        });

        el.shot.addEventListener('click', function () {
            el.panel.toBlob(function (blob) {
                if (!blob) {
                    return;
                }
                var url = URL.createObjectURL(blob);
                var link = document.createElement('a');
                link.href = url;
                link.download = 'notrix-' + nowMillis() + '.png';
                link.click();
                URL.revokeObjectURL(url);
            });
        });

        // Hardware controls send raw events; the core decides what they mean.
        // Rotary detents are momentary, so they arrive as a single Tick rather
        // than a Down/Up pair.
        el.notifyLow.addEventListener('click', function () {
            core._notrix_notify(1, 4, nowMillis());   // Priority::Normal
            renderFrame();
        });

        el.notifyHigh.addEventListener('click', function () {
            core._notrix_notify(3, 4, nowMillis());   // Priority::Urgent
            renderFrame();
        });

        var hardware = document.querySelectorAll('.btn-hw[data-source]');
        Array.prototype.forEach.call(hardware, function (button) {
            var source = parseInt(button.getAttribute('data-source'), 10);
            var isTick = button.getAttribute('data-tick') === '1';

            if (isTick) {
                button.addEventListener('click', function () {
                    sendInput(source, PHASE_TICK);
                });
                return;
            }

            // Real press duration, so a long press behaves like one.
            button.addEventListener('mousedown', function () { sendInput(source, PHASE_DOWN); });
            button.addEventListener('mouseup', function () { sendInput(source, PHASE_UP); });
            button.addEventListener('mouseleave', function (event) {
                if (event.buttons === 1) {
                    sendInput(source, PHASE_UP);
                }
            });
        });

        window.addEventListener('resize', function () {
            resizeCanvas();
            if (core) {
                drawFramebuffer(readFramebuffer());
            } else {
                drawUnlitPanel();
            }
        });
    }

    // --- startup ------------------------------------------------------------

    function showNotice(html) {
        el.notice.innerHTML = html;
        el.notice.hidden = false;
    }

    function coreUnavailable() {
        el.statCore.textContent = 'not built';
        [el.play, el.step, el.shot, el.brightness, el.notifyLow, el.notifyHigh]
            .forEach(function (control) {
                if (control) {
                    control.disabled = true;
                }
            });
        Array.prototype.forEach.call(document.querySelectorAll('.btn-hw'), function (button) {
            button.disabled = true;
        });
        showNotice(
            'The WebAssembly core is not built yet. Run <code>.\\dev.ps1 emulator</code> ' +
            '(needs the Emscripten SDK), then reload this page.'
        );
        resizeCanvas();
        drawUnlitPanel();
    }

    function start(module) {
        core = module;

        width = core._notrix_width();
        height = core._notrix_height();
        framebufferPtr = core._notrix_framebuffer();
        core._notrix_init();

        // Give the device a real time; without it the clock honestly shows
        // "--:--" because the wall clock was never set.
        var offsetSeconds = -new Date().getTimezoneOffset() * 60;
        core._notrix_set_wall_clock(Date.now() / 1000, offsetSeconds);

        bloomCanvas.width = width;
        bloomCanvas.height = height;
        bloomData = bloomCtx.createImageData(width, height);

        el.geometry.textContent = width + ' × ' + height;
        el.statCore.textContent = 'wasm · ' + core._notrix_app_count() + ' apps';

        resizeCanvas();
        wireControls();
        syncPauseLabel();

        startedAt = performance.now();
        lastTimestamp = performance.now();
        fpsSince = lastTimestamp;
        statsSince = lastTimestamp;

        renderFrame();
        requestAnimationFrame(tick);
    }

    if (window.__notrixCoreMissing || typeof createNotrixModule === 'undefined') {
        coreUnavailable();
        return;
    }

    createNotrixModule().then(start).catch(function (error) {
        el.statCore.textContent = 'error';
        showNotice('Failed to initialise the WebAssembly core: ' + error);
        resizeCanvas();
        drawUnlitPanel();
    });
})();
