// SPDX-License-Identifier: GPL-3.0-or-later
//
// The device's own configuration UI. Talks to /api/v1/* and nothing else, so
// everything it can do is something an integration could also do.
//
// Written against the same API surface whether it is loaded from the device or
// from the emulator: the only difference is how a request is dispatched, which
// is settled once in send() below. That is what lets the whole UI be exercised
// before any hardware exists.

(function () {
    'use strict';

    // --- transport ----------------------------------------------------------

    // The emulator installs this to route requests straight into the WASM
    // module, because there is no socket to talk to. On a real device it is
    // absent and we use fetch. Nothing else in this file knows the difference.
    var bridge = window.NOTRIX_BRIDGE || null;

    function send(method, path, body) {
        if (bridge) {
            return bridge(method, path, body === undefined ? '' : JSON.stringify(body))
                .then(function (result) {
                    return decode(result.status, result.body);
                });
        }

        var init = { method: method, headers: {} };
        if (body !== undefined) {
            init.headers['Content-Type'] = 'application/json';
            init.body = JSON.stringify(body);
        }
        return fetch(path, init).then(function (response) {
            return response.text().then(function (text) {
                return decode(response.status, text);
            });
        });
    }

    function decode(status, text) {
        var payload = null;
        if (text) {
            try {
                payload = JSON.parse(text);
            } catch (e) {
                payload = null;
            }
        }
        if (status >= 200 && status < 300) {
            return payload;
        }
        // Every error shares one shape, so there is exactly one place that has
        // to know how to read one.
        var message = payload && payload.error && payload.error.message
            ? payload.error.message
            : 'request failed (' + status + ')';
        throw new Error(message);
    }

    // --- dom helpers --------------------------------------------------------

    function $(id) { return document.getElementById(id); }

    function el(tag, className, text) {
        var node = document.createElement(tag);
        if (className) { node.className = className; }
        if (text !== undefined) { node.textContent = text; }
        return node;
    }

    var toastTimer = null;
    function toast(message, isError) {
        var node = $('toast');
        node.textContent = message;
        node.className = 'toast show' + (isError ? ' bad' : '');
        if (toastTimer) { clearTimeout(toastTimer); }
        toastTimer = setTimeout(function () { node.className = 'toast'; }, 2600);
    }

    function fail(error) {
        toast(error.message || String(error), true);
    }

    // --- settings binding ---------------------------------------------------
    //
    // Controls declare which setting they own with data-setting="clock.theme".
    // Reading and writing both walk that path, so adding a setting to the page
    // is one attribute rather than two hand-written mapping functions that
    // drift apart.

    var settings = null;
    var controls = [];

    function pathGet(root, dotted) {
        var parts = dotted.split('.');
        var node = root;
        for (var i = 0; i < parts.length && node !== undefined && node !== null; ++i) {
            node = node[parts[i]];
        }
        return node;
    }

    function patchFor(dotted, value) {
        var parts = dotted.split('.');
        var body = {};
        var node = body;
        for (var i = 0; i < parts.length - 1; ++i) {
            node[parts[i]] = {};
            node = node[parts[i]];
        }
        node[parts[parts.length - 1]] = value;
        return body;
    }

    function readControl(input) {
        if (input.type === 'checkbox') { return input.checked; }
        if (input.type === 'range' || input.type === 'number') {
            return parseInt(input.value, 10);
        }
        if (input.tagName === 'SELECT') {
            // Selects carry numbers as strings; the API is strict about types,
            // so convert the ones that are genuinely numeric.
            var numeric = input.getAttribute('data-numeric') === '1';
            return numeric ? parseInt(input.value, 10) : input.value;
        }
        return input.value;
    }

    function writeControl(input, value) {
        if (value === undefined || value === null) { return; }
        if (input.type === 'checkbox') {
            input.checked = !!value;
        } else {
            input.value = String(value);
        }
        if (input.type === 'range') { updateOutput(input); }
    }

    function updateOutput(input) {
        var out = $(input.id + '-out');
        if (out) { out.textContent = input.value; }
    }

    // Ranges fire continuously while dragging. Sending a PATCH per pixel would
    // hammer a device that also has a panel to render, so coalesce.
    function debounce(fn, delayMillis) {
        var timer = null;
        return function () {
            var args = arguments;
            var self = this;
            if (timer) { clearTimeout(timer); }
            timer = setTimeout(function () { fn.apply(self, args); }, delayMillis);
        };
    }

    function applySetting(input) {
        var dotted = input.getAttribute('data-setting');
        var value = readControl(input);
        send('PATCH', '/api/v1/settings', patchFor(dotted, value))
            .then(function (updated) {
                settings = updated;
                toast('Saved');
            })
            .catch(function (error) {
                // Put the control back to what the device actually holds, so the
                // page never shows a value the device rejected.
                writeControl(input, pathGet(settings, dotted));
                fail(error);
            });
    }

    function bindControls() {
        controls = Array.prototype.slice.call(
            document.querySelectorAll('[data-setting]'));

        controls.forEach(function (input) {
            var debounced = debounce(function () { applySetting(input); }, 250);

            if (input.type === 'range') {
                input.addEventListener('input', function () {
                    updateOutput(input);
                    debounced();
                });
            } else if (input.type === 'text') {
                input.addEventListener('change', function () { applySetting(input); });
            } else {
                input.addEventListener('change', function () { applySetting(input); });
            }
        });
    }

    function loadSettings() {
        return send('GET', '/api/v1/settings').then(function (loaded) {
            settings = loaded;
            controls.forEach(function (input) {
                writeControl(input, pathGet(settings, input.getAttribute('data-setting')));
            });
        });
    }

    // --- time zones ---------------------------------------------------------

    function fillOffsets() {
        var select = $('utcOffsetSeconds');
        select.setAttribute('data-numeric', '1');
        for (var hour = -12; hour <= 14; ++hour) {
            var label = 'UTC' + (hour === 0 ? '' : (hour > 0 ? '+' : '') + hour);
            select.appendChild(new Option(label, String(hour * 3600)));
        }
    }

    // --- mqtt ---------------------------------------------------------------
    //
    // The password is the one setting that does not round-trip: the API accepts
    // it and never returns it. So it cannot use the data-setting binding, which
    // assumes a value can be read back.

    function wireMqtt() {
        var field = $('mqtt-password');

        field.addEventListener('change', function () {
            send('PATCH', '/api/v1/settings', { mqtt: { password: field.value } })
                .then(function (updated) {
                    settings = updated;
                    field.value = '';  // never hold a credential in the DOM
                    describePassword();
                refreshMeridiem();
                    toast(updated.mqtt.passwordSet ? 'Password saved' : 'Password cleared');
                })
                .catch(fail);
        });

        $('mqtt-baseTopic').addEventListener('input', previewTopic);
        $('deviceName').addEventListener('input', previewTopic);
    }

    function describePassword() {
        $('mqtt-password-help').textContent =
            settings && settings.mqtt && settings.mqtt.passwordSet
                ? 'A password is set. Type to replace it, or clear the box and save to remove it.'
                : 'Not set.';
    }

    // Mirrors mqtt::deviceIdFromName. Duplicated deliberately and only for the
    // preview: showing the wrong topic is a cosmetic bug, whereas asking the
    // device for it on every keystroke would not be.
    function previewTopic() {
        var base = $('mqtt-baseTopic').value || 'notrix';
        var id = $('deviceName').value
            .toLowerCase()
            .replace(/[^a-z0-9]+/g, '-')
            .replace(/^-+|-+$/g, '');
        $('mqtt-topic-preview').textContent = 'Topics: ' + base + '/' + (id || 'device') + '/...';
    }

    // --- device -------------------------------------------------------------

    function loadDevice() {
        return send('GET', '/api/v1/device').then(function (device) {
            $('device-name').textContent = device.name || 'notrix';
            $('device-version').textContent = 'v' + device.version;

            var facts = $('device-facts');
            facts.textContent = '';

            addFact(facts, 'Platform', device.platform);
            addFact(facts, 'Firmware', device.version);
            addFact(facts, 'API', device.apiVersion);
            if (device.display) {
                addFact(facts, 'Panel',
                    device.display.width + ' x ' + device.display.height);
                if (device.display.minimumFrameIntervalMillis !== undefined) {
                    addFact(facts, 'Frame floor',
                        device.display.minimumFrameIntervalMillis + ' ms');
                }
            }
            if (device.network === null) {
                addFact(facts, 'Network', 'no interface on this build');
            } else if (device.network && device.network.connected) {
                addFact(facts, 'Address', device.network.ipv4 || 'unknown');
                if (device.network.hostname) {
                    addFact(facts, 'Hostname', device.network.hostname);
                }
                if (device.network.rssiDbm) {
                    addFact(facts, 'Signal', device.network.rssiDbm + ' dBm');
                }
            } else {
                addFact(facts, 'Network', 'not connected');
            }

            var can = device.capabilities || {};

            // A device without a speaker should say so rather than offer a
            // volume slider that silently does nothing.
            if (can.audio === false) {
                $('volume').disabled = true;
                $('volume-help').textContent = 'This device has no speaker.';
            }
            if (can.reboot === false) {
                $('reboot').disabled = true;
            }
        });
    }

    function addFact(list, term, value) {
        list.appendChild(el('dt', null, term));
        list.appendChild(el('dd', null, value === undefined ? 'unknown' : String(value)));
    }

    // --- apps ---------------------------------------------------------------

    function loadApps() {
        return send('GET', '/api/v1/apps').then(function (result) {
            var list = $('app-list');
            list.textContent = '';

            (result.apps || []).forEach(function (app) {
                var row = el('li');

                var toggle = el('input');
                toggle.type = 'checkbox';
                toggle.checked = app.enabled;
                toggle.addEventListener('change', function () {
                    send('PATCH', '/api/v1/apps/' + encodeURIComponent(app.id),
                         { enabled: toggle.checked })
                        .then(function () { toast(app.name + (toggle.checked ? ' on' : ' off')); })
                        .catch(function (error) {
                            toggle.checked = app.enabled;
                            fail(error);
                        });
                });

                var body = el('div', 'grow');
                body.appendChild(el('span', 'name', app.name));
                body.appendChild(el('span', 'sub',
                    app.source + (app.durationSeconds ? ' - ' + app.durationSeconds + 's' : '')));

                var show = el('button', 'btn', 'Show');
                show.type = 'button';
                show.addEventListener('click', function () {
                    send('POST', '/api/v1/apps/' + encodeURIComponent(app.id) + '/activate')
                        .then(function () { toast('Showing ' + app.name); })
                        .catch(fail);
                });

                row.appendChild(toggle);
                row.appendChild(body);
                row.appendChild(show);
                list.appendChild(row);
            });

            if (!result.apps || !result.apps.length) {
                list.appendChild(el('li', null, 'No apps installed.'));
            }
        });
    }

    // --- notifications ------------------------------------------------------

    function loadNotifications() {
        return send('GET', '/api/v1/notifications').then(function (result) {
            var list = $('notify-list');
            list.textContent = '';

            var items = result.notifications || [];
            if (!items.length) {
                list.appendChild(el('li', null, 'Nothing queued.'));
                return;
            }

            items.forEach(function (item) {
                var row = el('li');
                var body = el('div', 'grow');
                body.appendChild(el('span', 'name', item.text));
                body.appendChild(el('span', 'sub',
                    item.priority + ' - ' + item.durationSeconds + 's'));

                var dismiss = el('button', 'btn', 'Dismiss');
                dismiss.type = 'button';
                dismiss.addEventListener('click', function () {
                    send('DELETE', '/api/v1/notifications/' + encodeURIComponent(item.id))
                        .then(loadNotifications)
                        .catch(fail);
                });

                row.appendChild(body);
                row.appendChild(dismiss);
                list.appendChild(row);
            });
        });
    }

    function wireNotify() {
        $('notify-send').addEventListener('click', function () {
            var text = $('notify-text').value.trim();
            if (!text) {
                toast('Type something to send', true);
                return;
            }
            send('POST', '/api/v1/notifications', {
                text: text,
                priority: $('notify-priority').value,
                durationSeconds: parseInt($('notify-duration').value, 10)
            }).then(function () {
                $('notify-text').value = '';
                toast('Sent');
                return loadNotifications();
            }).catch(fail);
        });
    }

    // --- logs ---------------------------------------------------------------

    function loadLogs() {
        return send('GET', '/api/v1/logs').then(function (result) {
            var view = $('logview');
            var follow = $('log-follow').checked;
            view.textContent = '';

            (result.entries || []).forEach(function (entry) {
                var level = String(entry.level).toLowerCase();
                var row = el('li', level);
                row.appendChild(el('span', 'at', formatUptime(entry.at)));
                row.appendChild(el('span', 'lvl', level));
                row.appendChild(el('span', 'grow', entry.message));
                view.appendChild(row);
            });

            var lost = result.totalWritten - result.count;
            $('log-meta').textContent = lost > 0
                ? result.count + ' shown, ' + lost + ' older lines dropped'
                : result.count + ' of ' + result.capacity;

            if (follow) { view.scrollTop = view.scrollHeight; }
        });
    }

    function formatUptime(millis) {
        var total = Math.floor(millis / 1000);
        var seconds = total % 60;
        var minutes = Math.floor(total / 60) % 60;
        var hours = Math.floor(total / 3600);
        return pad(hours) + ':' + pad(minutes) + ':' + pad(seconds);
    }

    function pad(value) {
        return (value < 10 ? '0' : '') + value;
    }

    // --- system -------------------------------------------------------------

    function wireReboot() {
        $('reboot').addEventListener('click', function () {
            if (!window.confirm('Restart the device now?')) { return; }
            send('POST', '/api/v1/system/reboot')
                .then(function () { toast('Restarting'); })
                .catch(fail);
        });
    }

    // --- tabs ---------------------------------------------------------------

    // Which panel is open decides what gets polled, so a page left open on
    // Display is not also fetching logs every two seconds.
    var activePanel = 'panel-display';

    function showPanel(id) {
        activePanel = id;
        Array.prototype.forEach.call(document.querySelectorAll('.panel'), function (panel) {
            panel.className = panel.id === id ? 'panel active' : 'panel';
        });
        Array.prototype.forEach.call(document.querySelectorAll('.tab'), function (tab) {
            tab.className = tab.getAttribute('data-panel') === id ? 'tab active' : 'tab';
        });
        refreshActive();
    }

    function wireTabs() {
        Array.prototype.forEach.call(document.querySelectorAll('.tab'), function (tab) {
            tab.addEventListener('click', function () {
                showPanel(tab.getAttribute('data-panel'));
            });
        });
    }

    function refreshActive() {
        if (activePanel === 'panel-apps') { return loadApps().catch(fail); }
        if (activePanel === 'panel-notify') { return loadNotifications().catch(fail); }
        if (activePanel === 'panel-logs') { return loadLogs().catch(fail); }
        return Promise.resolve();
    }

    // --- live view and on-screen controls -----------------------------------

    // Polled rather than streamed. The device answers HTTP from its render loop
    // one request at a time (see Tc002HttpServer), so a websocket would buy no
    // concurrency and cost a second protocol. Five frames a second is plenty to
    // watch a clock and leaves the panel's own budget alone.
    var LIVE_INTERVAL_MS = 200;
    var liveTimer = null;
    var liveBusy = false;

    // One LED per pixel, drawn with a gap. A 52x16 image scaled up is a smear;
    // what makes this read as a panel is the dark space between the pixels, so
    // the geometry is explicit rather than left to CSS scaling.
    var LED_SIZE = 9;
    var LED_GAP = 1;
    var LED_PITCH = LED_SIZE + LED_GAP;

    var lastFrame = null;   // the decoded RGB of the most recent frame
    var recording = null;   // { frames: [], started: number }

    function drawFrame(payload) {
        var canvas = $('live');
        if (!canvas || !payload || payload.format !== 'rgb888') { return; }

        var width = payload.width;
        var height = payload.height;

        // Sized once, when the panel geometry is first known. Doing it every
        // frame would reset the context and flash.
        var wanted = width * LED_PITCH - LED_GAP;
        var wantedHigh = height * LED_PITCH - LED_GAP;
        if (canvas.width !== wanted || canvas.height !== wantedHigh) {
            canvas.width = wanted;
            canvas.height = wantedHigh;
        }

        var binary = atob(payload.pixels);
        lastFrame = { pixels: binary, width: width, height: height };

        var ctx = canvas.getContext('2d');
        ctx.fillStyle = '#05070a';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        for (var y = 0; y < height; ++y) {
            for (var x = 0; x < width; ++x) {
                var i = (y * width + x) * 3;
                var r = binary.charCodeAt(i);
                var g = binary.charCodeAt(i + 1);
                var b = binary.charCodeAt(i + 2);

                // An unlit LED is not invisible - it is a dark grey dot. A grid
                // that vanished where the panel was black would stop reading as
                // hardware, which is the whole point of this view.
                if (r === 0 && g === 0 && b === 0) {
                    ctx.fillStyle = '#12161c';
                } else {
                    ctx.fillStyle = 'rgb(' + r + ',' + g + ',' + b + ')';
                }
                ctx.fillRect(x * LED_PITCH, y * LED_PITCH, LED_SIZE, LED_SIZE);
            }
        }

        if (recording) {
            recording.frames.push(binary);
            $('live-rec').textContent = 'Stop (' + recording.frames.length + ')';
        }
    }

    // AM/PM is meaningless on a 24-hour clock, and the faces that already use
    // all 52 columns cannot show it at all. Rather than leave a switch that
    // silently does nothing - the failure this project keeps finding - the
    // control says so and disables itself.
    var NO_MERIDIEM_FACES = { seconds: 1, calendar: 1 };

    function refreshMeridiem() {
        var toggle = $('showAmPm');
        var help = $('ampm-help');
        if (!toggle) { return; }

        var twentyFour = $('twentyFourHour') && $('twentyFourHour').checked;
        var face = $('theme') ? $('theme').value : '';
        var noRoom = !!NO_MERIDIEM_FACES[face];

        toggle.disabled = twentyFour || noRoom;
        if (help) {
            help.textContent = twentyFour
                ? 'Switch off 24-hour to use AM/PM.'
                : (noRoom
                    ? 'This face already fills the panel - no room for AM/PM.'
                    : 'Shown beside the time.');
        }
    }

    // --- colour ---------------------------------------------------------------

    // A swatch grid instead of <input type="color">.
    //
    // The OS picker offers sixteen million colours to a device that shows them
    // on 832 LEDs behind a diffuser, and it opens a modal that hides the live
    // view - so you cannot see what you picked while picking it. These are
    // colours chosen to be legible on this panel, and clicking one applies it
    // immediately with the live view still on screen.
    var SWATCHES = [
        '#FFFFFF', '#C9C9C9', '#8A8A8A',
        '#FF3B30', '#FF8000', '#FFD400',
        '#4FC96F', '#00C8A0', '#00BEFF',
        '#5C7FBF', '#9B5CFF', '#FF5CA8'
    ];

    function buildSwatches(input) {
        var host = el('div', 'swatches');

        SWATCHES.forEach(function (hex) {
            var cell = el('button', 'swatch');
            cell.type = 'button';
            cell.style.background = hex;
            cell.title = hex;
            cell.setAttribute('aria-label', hex);
            cell.addEventListener('click', function () {
                input.value = hex;
                markSelected(host, hex);
                applySetting(input);
            });
            host.appendChild(cell);
        });

        // The full picker stays available as an escape hatch, just not as the
        // primary control.
        var custom = el('button', 'swatch swatch-custom', '+');
        custom.type = 'button';
        custom.title = 'Any colour';
        custom.addEventListener('click', function () { input.click(); });
        host.appendChild(custom);

        input.addEventListener('input', function () { markSelected(host, input.value); });
        return host;
    }

    function markSelected(host, hex) {
        var want = String(hex).toUpperCase();
        Array.prototype.forEach.call(host.querySelectorAll('.swatch'), function (cell) {
            var mine = (cell.title || '').toUpperCase();
            cell.classList.toggle('on', mine === want);
        });
    }

    function wireSwatches() {
        Array.prototype.forEach.call(
            document.querySelectorAll('input[type="color"][data-setting]'),
            function (input) {
                input.classList.add('color-hidden');
                input.parentNode.insertBefore(buildSwatches(input), input.nextSibling);
            });
    }

    // --- capture ------------------------------------------------------------

    function downloadCanvas(name) {
        var canvas = $('live');
        if (!canvas) { return; }
        canvas.toBlob(function (blob) {
            var url = URL.createObjectURL(blob);
            var link = el('a');
            link.href = url;
            link.download = name;
            link.click();
            // Revoked on the next turn of the loop: revoking immediately can
            // beat the download starting in some browsers.
            setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
        });
    }

    function stamp() {
        var d = new Date();
        function two(n) { return (n < 10 ? '0' : '') + n; }
        return d.getFullYear() + two(d.getMonth() + 1) + two(d.getDate()) +
               '-' + two(d.getHours()) + two(d.getMinutes()) + two(d.getSeconds());
    }

    function wireCapture() {
        var shot = $('live-shot');
        if (shot) {
            shot.addEventListener('click', function () {
                if (!lastFrame) { return toast('no frame yet', true); }
                downloadCanvas('notrix-' + stamp() + '.png');
            });
        }

        var rec = $('live-rec');
        if (rec) {
            rec.addEventListener('click', function () {
                if (recording) {
                    var count = recording.frames.length;
                    // Written as an APNG-free animated strip: every captured
                    // frame stacked vertically in one PNG. No encoder, no
                    // dependency, and it opens anywhere.
                    saveStrip(recording.frames, count);
                    recording = null;
                    rec.textContent = 'Record';
                    rec.classList.remove('btn-live');
                } else {
                    recording = { frames: [], started: Date.now() };
                    rec.textContent = 'Stop (0)';
                    rec.classList.add('btn-live');
                    toast('recording - press again to save');
                }
            });
        }
    }

    // Frames stacked into one tall PNG, at 1:1 rather than LED scale so the
    // result is exact pixel data rather than a picture of a picture.
    function saveStrip(frames, count) {
        if (!count || !lastFrame) { return toast('nothing recorded', true); }

        var w = lastFrame.width;
        var h = lastFrame.height;
        var sheet = document.createElement('canvas');
        sheet.width = w;
        sheet.height = h * count;

        var ctx = sheet.getContext('2d');
        var image = ctx.createImageData(w, h * count);

        for (var f = 0; f < count; ++f) {
            var binary = frames[f];
            for (var i = 0; i < w * h; ++i) {
                var src = i * 3;
                var dst = (f * w * h + i) * 4;
                image.data[dst] = binary.charCodeAt(src);
                image.data[dst + 1] = binary.charCodeAt(src + 1);
                image.data[dst + 2] = binary.charCodeAt(src + 2);
                image.data[dst + 3] = 255;
            }
        }
        ctx.putImageData(image, 0, 0);

        sheet.toBlob(function (blob) {
            var url = URL.createObjectURL(blob);
            var link = el('a');
            link.href = url;
            link.download = 'notrix-' + stamp() + '-' + count + 'frames.png';
            link.click();
            setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
        });
    }

    function tickLive() {
        // Skipped while a request is still in flight, so a slow device cannot
        // accumulate a backlog of frame requests it will never catch up on.
        if (liveBusy || activePanel !== 'panel-display') { return; }
        liveBusy = true;

        send('GET', '/api/v1/display/frame')
            .then(function (payload) {
                drawFrame(payload);
                $('live-status').textContent =
                    'live — ' + payload.width + '×' + payload.height;
            })
            .catch(function () {
                $('live-status').textContent = 'not available';
            })
            .then(function () { liveBusy = false; });
    }

    // A tap and a hold are different actions on this hardware, so the button
    // has to measure how long it was held rather than just fire on click.
    function wireControls() {
        var pressedAt = 0;

        Array.prototype.forEach.call(document.querySelectorAll('[data-control]'), function (button) {
            var control = button.getAttribute('data-control');

            button.addEventListener('pointerdown', function () { pressedAt = Date.now(); });

            button.addEventListener('click', function () {
                var held = pressedAt ? Date.now() - pressedAt : 0;
                pressedAt = 0;

                var body = { control: control };
                if (control !== 'left' && control !== 'right') {
                    body.holdMillis = held;
                }

                send('POST', '/api/v1/input', body)
                    .then(function () {
                        // Redraw immediately rather than waiting for the next
                        // tick, so the button feels connected to the panel.
                        tickLive();
                    })
                    .catch(fail);
            });
        });
    }

    // --- liveness -----------------------------------------------------------

    function markConnection(online) {
        var pill = $('connection');
        pill.textContent = online ? 'online' : 'offline';
        pill.className = 'pill ' + (online ? 'online' : 'offline');
    }

    function poll() {
        send('GET', '/api/v1/health')
            .then(function () {
                markConnection(true);
                return refreshActive();
            })
            .catch(function () { markConnection(false); });
    }

    // --- start --------------------------------------------------------------

    function start() {
        fillOffsets();
        bindControls();
        wireTabs();
        wireNotify();
        wireMqtt();
        wireReboot();
        wireControls();
        wireCapture();
        wireSwatches();

        ['twentyFourHour', 'theme'].forEach(function (id) {
            var input = $(id);
            if (input) { input.addEventListener('change', refreshMeridiem); }
        });
        showPanel('panel-display');

        Promise.all([loadSettings(), loadDevice()])
            .then(function () {
                describePassword();
                previewTopic();
                markConnection(true);
            })
            .catch(function (error) {
                markConnection(false);
                fail(error);
            });

        setInterval(poll, 4000);
        liveTimer = setInterval(tickLive, LIVE_INTERVAL_MS);
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', start);
    } else {
        start();
    }
}());
