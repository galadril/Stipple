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
    // Square emitters with a dark grid between them, which is what the TC002's
    // panel actually looks like. Circles read as a dot-matrix display and made
    // the preview subtly unlike the hardware.
    var LED = 10;        // emitter size
    var INSET = (PITCH - LED) / 2;
    var RADIUS = 1.5;    // the tiniest rounding; real emitters are not razor-edged
    var UNLIT = '#16181d';
    var BOARD = '#08090b';

    // Mirrors stipple::platform::RawInput and ButtonPhase.
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
        clockTheme: document.getElementById('clock-theme'),
        dropzone: document.getElementById('dropzone'),
        iconFile: document.getElementById('icon-file'),
        iconStatus: document.getElementById('icon-status'),
        logview: document.getElementById('logview'),
        logMeta: document.getElementById('log-meta'),
        notifyLow: document.getElementById('notify-low'),
        notifyHigh: document.getElementById('notify-high'),
        statRender: document.getElementById('stat-render'),
        statFps: document.getElementById('stat-fps'),
        statCore: document.getElementById('stat-core'),
        geometry: document.getElementById('chip-geometry'),
        uiframe: document.getElementById('uiframe'),
        uiReload: document.getElementById('ui-reload')
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

    /// One emitter. Uses roundRect where available and falls back to a plain
    /// square, since the rounding is cosmetic.
    function emitter(x, y) {
        var px = x * PITCH + INSET;
        var py = y * PITCH + INSET;
        ctx.beginPath();
        if (ctx.roundRect) {
            ctx.roundRect(px, py, LED, LED, RADIUS);
        } else {
            ctx.rect(px, py, LED, LED);
        }
        ctx.fill();
    }

    function drawUnlitPanel() {
        drawBoard();
        ctx.fillStyle = UNLIT;
        for (var y = 0; y < height; y++) {
            for (var x = 0; x < width; x++) {
                emitter(x, y);
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

                ctx.fillStyle = (r === 0 && g === 0 && b === 0)
                    ? UNLIT
                    : 'rgb(' + r + ',' + g + ',' + b + ')';
                emitter(x, y);

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
        ctx.globalAlpha = 0.32;
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
        core._stipple_render(nowMillis());
        drawFramebuffer(readFramebuffer());
        renderMs = performance.now() - start;
    }

    function sendInput(source, phase) {
        core._stipple_input(source, phase, nowMillis());
        renderFrame();
        syncPauseLabel();
    }

    function syncPauseLabel() {
        el.play.textContent = core._stipple_is_paused() ? 'Resume rotation' : 'Pause rotation';
    }

    // --- stats --------------------------------------------------------------

    function updateStats(now) {
        el.statApp.textContent = core.UTF8ToString(core._stipple_active_name()) || '-';

        if (core._stipple_showing_splash()) {
            el.statDwell.textContent = 'booting';
        } else {
            var dwell = core._stipple_dwell_millis(nowMillis()) / 1000;
            var duration = core._stipple_active_duration_seconds();
            el.statDwell.textContent = duration > 0
                ? dwell.toFixed(1) + 's / ' + duration + 's'
                : '-';
        }

        var queued = core._stipple_notification_count();
        var pending = core._stipple_notification_pending();
        el.statQueue.textContent = queued === 0
            ? '0'
            : (queued - pending) + ' + ' + pending;

        // Dirty rendering made visible: a static screen should skip far more
        // frames than it draws.
        var drawn = core._stipple_frames_rendered();
        var skipped = core._stipple_frames_skipped();
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
            refreshLog();
            statsSince = now;
        }

        requestAnimationFrame(tick);
    }

    // --- controls -----------------------------------------------------------

    // Resolved from the core rather than hard-coded: these ordinals shifted
    // twice while the input model was being corrected, and a browser has no way
    // to notice that its buttons have started sending the wrong events.
    function inputSource(name) {
        return core.ccall('stipple_input_source', 'number', ['string'], [name]);
    }

    function wireControls() {
        // A short knob press toggles pause in the core's default input map, so
        // the on-screen control sends exactly that rather than reaching past the
        // mapper to the carousel.
        el.play.addEventListener('click', function () {
            sendInput(inputSource('rotaryPress'), PHASE_DOWN);
            sendInput(inputSource('rotaryPress'), PHASE_UP);
        });

        el.step.addEventListener('click', function () {
            sendInput(inputSource('rotaryRight'), PHASE_TICK);
        });

        el.brightness.addEventListener('input', function () {
            var value = parseInt(el.brightness.value, 10);
            el.brightnessValue.textContent = String(value);
            core._stipple_set_brightness(value);
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
                link.download = 'stipple-' + nowMillis() + '.png';
                link.click();
                URL.revokeObjectURL(url);
            });
        });

        // Hardware controls send raw events; the core decides what they mean.
        // Rotary detents are momentary, so they arrive as a single Tick rather
        // than a Down/Up pair.
        // Clock faces, named by the core so the list cannot drift out of sync.
        var themeCount = core._stipple_clock_theme_count();
        for (var i = 0; i < themeCount; i++) {
            var option = document.createElement('option');
            option.value = String(i);
            option.textContent = core.UTF8ToString(core._stipple_clock_theme_name(i));
            el.clockTheme.appendChild(option);
        }
        el.clockTheme.value = String(core._stipple_clock_theme());

        el.clockTheme.addEventListener('change', function () {
            core._stipple_set_clock_theme(parseInt(el.clockTheme.value, 10), nowMillis());
            renderFrame();
        });

        el.notifyLow.addEventListener('click', function () {
            core._stipple_notify(1, 4, nowMillis());   // Priority::Normal
            renderFrame();
        });

        el.notifyHigh.addEventListener('click', function () {
            core._stipple_notify(3, 4, nowMillis());   // Priority::Urgent
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

    // --- icon upload --------------------------------------------------------
    //
    // The whole point of doing conversion here: the browser already decodes PNG
    // and GIF, so the firmware never needs an inflate or LZW decoder against
    // untrusted input. Any image source works, not just one gallery.

    var TRANSPARENT_KEY = 0xff00ff;   // magenta; nothing legible uses it

    function loadImage(file) {
        return new Promise(function (resolve, reject) {
            var url = URL.createObjectURL(file);
            var image = new Image();
            image.onload = function () { URL.revokeObjectURL(url); resolve(image); };
            image.onerror = function () { URL.revokeObjectURL(url); reject(new Error('not an image')); };
            image.src = url;
        });
    }

    /// Scale to fit the panel and decode to RGB, writing into the staging buffer
    /// at `frameIndex`. Anything half-transparent becomes the colour key.
    function writeFrame(source, w, h, frameIndex) {
        var work = document.createElement('canvas');
        work.width = w;
        work.height = h;
        var workCtx = work.getContext('2d', { willReadFrequently: true });

        // Nearest-neighbour: smoothing a 64x64 glyph down to 16x16 turns crisp
        // pixel art into grey mush on a panel that cannot blend.
        workCtx.imageSmoothingEnabled = false;
        workCtx.drawImage(source, 0, 0, w, h);

        var data = workCtx.getImageData(0, 0, w, h).data;
        var staging = core._stipple_icon_staging() + frameIndex * w * h * 3;
        var heap = core.HEAPU8;

        for (var i = 0; i < w * h; i++) {
            var alpha = data[i * 4 + 3];
            var r, g, b;
            if (alpha < 128) {
                r = (TRANSPARENT_KEY >> 16) & 0xff;
                g = (TRANSPARENT_KEY >> 8) & 0xff;
                b = TRANSPARENT_KEY & 0xff;
            } else {
                r = data[i * 4];
                g = data[i * 4 + 1];
                b = data[i * 4 + 2];
            }
            heap[staging + i * 3] = r;
            heap[staging + i * 3 + 1] = g;
            heap[staging + i * 3 + 2] = b;
        }
    }

    function targetSize(sourceWidth, sourceHeight) {
        var maxSide = Math.min(16, height);
        var scale = Math.min(maxSide / sourceWidth, maxSide / sourceHeight, 1);
        return {
            width: Math.max(1, Math.round(sourceWidth * scale)),
            height: Math.max(1, Math.round(sourceHeight * scale))
        };
    }

    function writeId(id) {
        var idBuffer = core._stipple_icon_id_buffer();
        var capacity = core._stipple_icon_id_capacity();
        var bytes = new TextEncoder().encode(id).subarray(0, capacity);
        core.HEAPU8.set(bytes, idBuffer);
        core.HEAPU8[idBuffer + bytes.length] = 0;
    }

    /// Decode every frame of an animation.
    ///
    /// ImageDecoder is the only way to reach individual GIF frames from script:
    /// an <img> element only ever exposes whichever frame it happens to be
    /// showing. Resolves to null when the file turns out not to be animated.
    function decodeAnimated(file, maxFrames) {
        return file.arrayBuffer().then(function (buffer) {
            var decoder = new ImageDecoder({ data: buffer, type: file.type });
            return decoder.completed.then(function () {
                var track = decoder.tracks.selectedTrack;
                var total = track ? track.frameCount : 1;
                if (total <= 1) {
                    return null;
                }

                // Sample evenly rather than taking the first N, so a long
                // animation keeps its whole loop instead of its opening moment.
                var count = Math.min(total, maxFrames);
                var indices = [];
                for (var i = 0; i < count; i++) {
                    indices.push(Math.floor(i * total / count));
                }

                var size = null;
                var durations = [];
                var chain = Promise.resolve();

                indices.forEach(function (frameIndex, slot) {
                    chain = chain.then(function () {
                        return decoder.decode({ frameIndex: frameIndex });
                    }).then(function (result) {
                        if (size === null) {
                            size = targetSize(result.image.displayWidth,
                                              result.image.displayHeight);
                        }
                        writeFrame(result.image, size.width, size.height, slot);
                        // duration is in microseconds, and may be absent.
                        durations.push((result.image.duration || 100000) / 1000);
                        result.image.close();
                    });
                });

                return chain.then(function () {
                    var total_ms = durations.reduce(function (a, b) { return a + b; }, 0);
                    return {
                        width: size.width,
                        height: size.height,
                        frames: count,
                        frameMillis: Math.max(20, Math.round(total_ms / durations.length)),
                        sampledFrom: total
                    };
                });
            });
        });
    }

    function iconStatus(text) {
        el.iconStatus.textContent = text;
    }

    function refreshIconStatus() {
        var count = core._stipple_icon_count();
        if (count === 0) {
            iconStatus('none stored');
            return;
        }
        iconStatus(count + ' stored · ' + core._stipple_icon_bytes_used() + ' bytes');
    }

    function commit(size) {
        var result = core._stipple_icon_commit(size.width, size.height, size.frames || 1,
                                              size.frameMillis || 100, TRANSPARENT_KEY);
        if (result !== 0) {
            iconStatus('rejected by the device (code ' + result + ')');
            return false;
        }
        core._stipple_show_icon_app(nowMillis());
        refreshIconStatus();
        renderFrame();
        return true;
    }

    function acceptStill(file, id) {
        return loadImage(file).then(function (image) {
            var size = targetSize(image.width, image.height);
            writeFrame(image, size.width, size.height, 0);
            writeId(id);
            if (commit({ width: size.width, height: size.height, frames: 1 })) {
                iconStatus('stored "' + id + '" (' + size.width + 'x' + size.height + ')');
            }
        });
    }

    function acceptFile(file) {
        if (!file) {
            return;
        }
        var id = file.name.replace(/\.[^.]+$/, '').slice(0, 40) || 'icon';
        var maxFrames = 16;
        var animated = file.type === 'image/gif' || file.type === 'image/webp';

        if (animated && typeof ImageDecoder === 'function') {
            decodeAnimated(file, maxFrames).then(function (size) {
                if (size === null) {
                    return acceptStill(file, id);
                }
                writeId(id);
                if (commit(size)) {
                    var note = size.sampledFrom > size.frames
                        ? ' (' + size.frames + ' of ' + size.sampledFrom + ' frames)'
                        : ' (' + size.frames + ' frames)';
                    iconStatus('stored "' + id + '"' + note);
                }
            }).catch(function () {
                // Any decode failure falls back to a single frame rather than
                // rejecting the file outright.
                acceptStill(file, id).catch(function (error) {
                    iconStatus(String(error.message || error));
                });
            });
            return;
        }

        if (animated) {
            iconStatus('this browser cannot split GIF frames; storing one');
        }
        acceptStill(file, id).catch(function (error) {
            iconStatus(String(error.message || error));
        });
    }

    function wireDropzone() {
        el.dropzone.addEventListener('click', function () { el.iconFile.click(); });
        el.iconFile.addEventListener('change', function () {
            acceptFile(el.iconFile.files && el.iconFile.files[0]);
            el.iconFile.value = '';
        });

        ['dragenter', 'dragover'].forEach(function (name) {
            el.dropzone.addEventListener(name, function (event) {
                event.preventDefault();
                el.dropzone.classList.add('is-over');
            });
        });
        ['dragleave', 'drop'].forEach(function (name) {
            el.dropzone.addEventListener(name, function (event) {
                event.preventDefault();
                el.dropzone.classList.remove('is-over');
            });
        });
        el.dropzone.addEventListener('drop', function (event) {
            acceptFile(event.dataTransfer && event.dataTransfer.files[0]);
        });
    }

    // --- device log ---------------------------------------------------------
    //
    // Reads the firmware's own ring buffer (blueprint §22) rather than anything
    // the page keeps for itself, so what is shown here is exactly what a device
    // would report over the API.

    var lastLogCount = -1;

    function refreshLog() {
        var count = core._stipple_log_count();
        if (count === lastLogCount) {
            return;   // the ring only ever grows between redraws
        }
        lastLogCount = count;

        el.logview.textContent = '';
        if (count === 0) {
            var empty = document.createElement('li');
            empty.className = 'empty';
            empty.textContent = 'no entries';
            el.logview.appendChild(empty);
            el.logMeta.textContent = '-';
            return;
        }

        for (var i = 0; i < count; i++) {
            var raw = core.UTF8ToString(core._stipple_log_line(i));
            var split = raw.indexOf('  ');
            var level = split > 0 ? raw.slice(0, split) : 'INFO';
            var message = split > 0 ? raw.slice(split + 2) : raw;

            var row = document.createElement('li');

            var when = document.createElement('span');
            when.className = 'when';
            when.textContent = String(i);

            var lvl = document.createElement('span');
            lvl.className = 'lvl lvl-' + level;
            lvl.textContent = level;

            var msg = document.createElement('span');
            msg.className = 'msg';
            // textContent, not innerHTML: log lines can carry anything, and an
            // app name is attacker-controlled once the API is reachable.
            msg.textContent = message;

            row.appendChild(when);
            row.appendChild(lvl);
            row.appendChild(msg);
            el.logview.appendChild(row);
        }

        el.logMeta.textContent = count + ' of ' + count + ' retained';
        el.logview.scrollTop = el.logview.scrollHeight;
    }

    // --- startup ------------------------------------------------------------

    function showNotice(html) {
        el.notice.innerHTML = html;
        el.notice.hidden = false;
    }

    function coreUnavailable() {
        el.statCore.textContent = 'not built';
        [el.play, el.step, el.shot, el.brightness, el.notifyLow, el.notifyHigh,
         el.clockTheme]
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


    // --- device web UI ------------------------------------------------------
    //
    // The configuration page is embedded in the firmware, so the emulator does
    // not have its own copy to drift out of step. It is fetched through the
    // WASM module exactly as a browser would fetch it from the device, then
    // inlined into an iframe because the page's absolute paths (/app.css) would
    // otherwise resolve against this page instead of the simulated device.

    function coreRequest(method, path, body) {
        var status = core.ccall('stipple_http_request', 'number',
            ['string', 'string', 'string'], [method, path, body || '']);
        return { status: status, body: core.UTF8ToString(core._stipple_http_body()) };
    }

    // The iframe calls this through `parent`. Same-origin srcdoc, so it can.
    window.STIPPLE_EMULATOR_REQUEST = function (method, path, body) {
        try {
            return Promise.resolve(coreRequest(method, path, body));
        } catch (error) {
            return Promise.reject(error);
        }
    };

    function mountDeviceUi() {
        var page = coreRequest('GET', '/index.html');
        if (page.status !== 200) {
            el.uiframe.srcdoc = '<p style="font:14px system-ui;color:#f2545b;padding:20px">'
                + 'The firmware did not serve /index.html (status ' + page.status + ').</p>';
            return;
        }

        var css = coreRequest('GET', '/app.css').body;
        var js = coreRequest('GET', '/app.js').body;

        // Defined before the page's own script runs, so app.js sees it on load
        // and never tries a fetch that would hit this page's origin.
        var shim = '<script>window.STIPPLE_BRIDGE=function(m,p,b){'
            + 'return parent.STIPPLE_EMULATOR_REQUEST(m,p,b);};<\/script>';

        var html = page.body
            .replace('<link rel="stylesheet" href="/app.css">', '<style>' + css + '</style>')
            .replace('<script src="/app.js"><\/script>', shim + '<script>' + js + '<\/script>');

        el.uiframe.srcdoc = html;
    }

    function wireDeviceUi() {
        el.uiReload.addEventListener('click', mountDeviceUi);
    }

    function start(module) {
        core = module;

        width = core._stipple_width();
        height = core._stipple_height();
        framebufferPtr = core._stipple_framebuffer();
        core._stipple_init();

        // Give the device a real time; without it the clock honestly shows
        // "--:--" because the wall clock was never set.
        var offsetSeconds = -new Date().getTimezoneOffset() * 60;
        core._stipple_set_wall_clock(Date.now() / 1000, offsetSeconds);

        bloomCanvas.width = width;
        bloomCanvas.height = height;
        bloomData = bloomCtx.createImageData(width, height);

        el.geometry.textContent = width + ' × ' + height;
        el.statCore.textContent = 'wasm · ' + core._stipple_app_count() + ' apps';

        resizeCanvas();
        wireControls();
        wireDropzone();
        refreshIconStatus();
        refreshLog();
        syncPauseLabel();
        wireDeviceUi();
        mountDeviceUi();

        startedAt = performance.now();
        lastTimestamp = performance.now();
        fpsSince = lastTimestamp;
        statsSince = lastTimestamp;

        renderFrame();
        requestAnimationFrame(tick);
    }

    if (window.__stippleCoreMissing || typeof createStippleModule === 'undefined') {
        coreUnavailable();
        return;
    }

    createStippleModule().then(start).catch(function (error) {
        el.statCore.textContent = 'error';
        showNotice('Failed to initialise the WebAssembly core: ' + error);
        resizeCanvas();
        drawUnlitPanel();
    });
})();
