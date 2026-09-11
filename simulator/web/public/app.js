// SPDX-License-Identifier: GPL-3.0-or-later
//
// Emulator front-end. Owns the frame clock, the LED look, and the controls.
// It owns no rendering logic: every pixel comes out of the WASM core.

(function () {
    'use strict';

    // Blueprint §9.4: 20-30 FPS for animation. The display API is throttled at
    // roughly 15 ms per frame on real hardware, so pacing the emulator at 60
    // would let us build animations the TC002 cannot actually keep up with.
    var TARGET_FPS = 30;
    var FRAME_MS = 1000 / TARGET_FPS;

    var PITCH = 13;      // css px between LED centres
    var DOT = 5.4;       // lit dot diameter
    var UNLIT = '#16181d';
    var BOARD = '#08090b';

    var el = {
        panel: document.getElementById('panel'),
        notice: document.getElementById('notice'),
        play: document.getElementById('play'),
        step: document.getElementById('step'),
        shot: document.getElementById('shot'),
        brightness: document.getElementById('brightness'),
        brightnessValue: document.getElementById('brightness-value'),
        statFrame: document.getElementById('stat-frame'),
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

    var frame = 0;
    var running = true;
    var accumulator = 0;
    var lastTimestamp = 0;
    var renderMs = 0;
    var fpsFrames = 0;
    var fpsSince = 0;
    var statsSince = 0;

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
        var heap = core.HEAPU8;
        return heap.subarray(framebufferPtr, framebufferPtr + width * height * 3);
    }

    function renderFrame() {
        var start = performance.now();
        core._notrix_render(frame);
        drawFramebuffer(readFramebuffer());
        renderMs = performance.now() - start;
    }

    // --- stats --------------------------------------------------------------

    function updateStats(now) {
        el.statFrame.textContent = String(frame);
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

        if (running) {
            accumulator += delta;
            if (accumulator >= FRAME_MS) {
                // Cap catch-up so returning to a backgrounded tab does not
                // fast-forward hundreds of frames at once.
                var steps = Math.min(Math.floor(accumulator / FRAME_MS), 4);
                accumulator -= steps * FRAME_MS;
                if (accumulator > FRAME_MS * 4) {
                    accumulator = 0;
                }
                frame += steps;
                renderFrame();
                fpsFrames++;
            }
        }

        if (now - statsSince > 250) {
            updateStats(now);
            statsSince = now;
        }

        requestAnimationFrame(tick);
    }

    // --- controls -----------------------------------------------------------

    function setRunning(value) {
        running = value;
        el.play.textContent = running ? 'Pause' : 'Play';
        el.step.disabled = running;
    }

    function wireControls() {
        el.play.addEventListener('click', function () {
            setRunning(!running);
        });

        el.step.addEventListener('click', function () {
            frame++;
            renderFrame();
            el.statFrame.textContent = String(frame);
            el.statRender.textContent = renderMs.toFixed(2) + ' ms';
        });

        el.brightness.addEventListener('input', function () {
            var value = parseInt(el.brightness.value, 10);
            el.brightnessValue.textContent = String(value);
            core._notrix_set_brightness(value);
            if (!running) {
                renderFrame();
            }
        });

        el.shot.addEventListener('click', function () {
            el.panel.toBlob(function (blob) {
                if (!blob) {
                    return;
                }
                var url = URL.createObjectURL(blob);
                var link = document.createElement('a');
                link.href = url;
                link.download = 'notrix-frame-' + frame + '.png';
                link.click();
                URL.revokeObjectURL(url);
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
        [el.play, el.step, el.shot, el.brightness].forEach(function (control) {
            control.disabled = true;
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

        bloomCanvas.width = width;
        bloomCanvas.height = height;
        bloomData = bloomCtx.createImageData(width, height);

        el.geometry.textContent = width + ' × ' + height;
        el.statCore.textContent = 'wasm';

        resizeCanvas();
        wireControls();
        setRunning(true);

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
