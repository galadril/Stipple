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
                // Everything that needs a speaker says so in the same place
                // and the same way, rather than one control going quiet and
                // the rest pretending.
                $('volume').disabled = true;
                $('volume-help').textContent = 'This device has no speaker.';
                $('notify-sound').disabled = true;
                $('notify-sound-help').textContent = 'This device has no speaker.';
                $('clock-tick').disabled = true;
                $('clock-tick-help').textContent = 'This device has no speaker.';
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

    function tickLive() {
        // Skipped while a request is still in flight, so a slow device cannot
        // accumulate a backlog of frame requests it will never catch up on.
        if (liveBusy || activePanel !== 'panel-display') { return; }
        liveBusy = true;

        send('GET', '/api/v1/display/frame')
            .then(function (payload) {
                drawFrame(payload);
                $('live-status').textContent =
                    'live \u2014 ' + payload.width + '\u00d7' + payload.height;
            })
            .catch(function () {
                $('live-status').textContent = 'not available';
            })
            .then(function () { liveBusy = false; });
    }

    // A tap and a hold are different actions on this hardware, so the button has
    // to measure how long it was held rather than just fire on click.
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

    // --- colour picker ------------------------------------------------------
    //
    // A dialog rather than <input type="color">, for two reasons that matter on
    // this product. The native picker is an OS modal that covers the page, so
    // the live view - the only place you can actually see the colour land - is
    // hidden while you choose. And it offers a precision the hardware does not
    // have: 832 LEDs behind a diffuser, where neighbouring shades are the same
    // colour.
    //
    // So: a saturation/value field, a hue slider, presets that are known to
    // read well on the panel, and a hex box for when someone knows exactly what
    // they want. It is a panel anchored to the swatch, not a full-screen modal,
    // and it applies live so the panel updates as you drag.

    var PRESETS = [
        '#FFFFFF', '#C9C9C9', '#8A8A8A',
        '#FF3B30', '#FF8000', '#FFD400',
        '#4FC96F', '#00C8A0', '#00BEFF',
        '#5C7FBF', '#9B5CFF', '#FF5CA8'
    ];

    function clamp01(v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

    function hsvToRgb(h, s, v) {
        var i = Math.floor(h * 6);
        var f = h * 6 - i;
        var p = v * (1 - s);
        var q = v * (1 - f * s);
        var t = v * (1 - (1 - f) * s);
        var r, g, b;
        switch (i % 6) {
            case 0: r = v; g = t; b = p; break;
            case 1: r = q; g = v; b = p; break;
            case 2: r = p; g = v; b = t; break;
            case 3: r = p; g = q; b = v; break;
            case 4: r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
        return [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255)];
    }

    function rgbToHsv(r, g, b) {
        r /= 255; g /= 255; b /= 255;
        var max = Math.max(r, g, b);
        var min = Math.min(r, g, b);
        var d = max - min;
        var h = 0;
        if (d !== 0) {
            if (max === r) { h = ((g - b) / d + (g < b ? 6 : 0)) / 6; }
            else if (max === g) { h = ((b - r) / d + 2) / 6; }
            else { h = ((r - g) / d + 4) / 6; }
        }
        return [h, max === 0 ? 0 : d / max, max];
    }

    function toHex(rgbArray) {
        return '#' + rgbArray.map(function (c) {
            var t = c.toString(16).toUpperCase();
            return t.length < 2 ? '0' + t : t;
        }).join('');
    }

    function parseHex(text) {
        var m = /^#?([0-9a-fA-F]{6})$/.exec(String(text).trim());
        if (!m) { return null; }
        var n = parseInt(m[1], 16);
        return [(n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF];
    }

    var openPicker = null;

    function closePicker() {
        if (openPicker) {
            openPicker.root.remove();
            openPicker = null;
        }
    }

    document.addEventListener('pointerdown', function (event) {
        if (openPicker && !openPicker.root.contains(event.target) &&
            event.target !== openPicker.trigger) {
            closePicker();
        }
    });
    document.addEventListener('keydown', function (event) {
        if (event.key === 'Escape') { closePicker(); }
    });

    function buildPicker(input, trigger) {
        var rgbNow = parseHex(input.value) || [255, 255, 255];
        var hsv = rgbToHsv(rgbNow[0], rgbNow[1], rgbNow[2]);
        var hue = hsv[0], sat = hsv[1], val = hsv[2];

        var root = el('div', 'picker');

        var field = el('canvas', 'picker-field');
        field.width = 180;
        field.height = 120;
        root.appendChild(field);

        var hueInput = el('input', 'picker-hue');
        hueInput.type = 'range';
        hueInput.min = 0;
        hueInput.max = 360;
        root.appendChild(hueInput);

        var row = el('div', 'picker-row');
        var preview = el('span', 'picker-preview');
        var hexInput = el('input', 'picker-hex');
        hexInput.type = 'text';
        hexInput.maxLength = 7;
        hexInput.spellcheck = false;
        row.appendChild(preview);
        row.appendChild(hexInput);
        root.appendChild(row);

        var presets = el('div', 'picker-presets');
        PRESETS.forEach(function (hex) {
            var cell = el('button', 'picker-preset');
            cell.type = 'button';
            cell.style.background = hex;
            cell.title = hex;
            cell.addEventListener('click', function () {
                var parsed = parseHex(hex);
                var next = rgbToHsv(parsed[0], parsed[1], parsed[2]);
                hue = next[0]; sat = next[1]; val = next[2];
                redraw(true);
            });
            presets.appendChild(cell);
        });
        root.appendChild(presets);

        function paintField() {
            var ctx = field.getContext('2d');
            var base = hsvToRgb(hue, 1, 1);

            ctx.fillStyle = 'rgb(' + base[0] + ',' + base[1] + ',' + base[2] + ')';
            ctx.fillRect(0, 0, field.width, field.height);

            var white = ctx.createLinearGradient(0, 0, field.width, 0);
            white.addColorStop(0, 'rgba(255,255,255,1)');
            white.addColorStop(1, 'rgba(255,255,255,0)');
            ctx.fillStyle = white;
            ctx.fillRect(0, 0, field.width, field.height);

            var black = ctx.createLinearGradient(0, 0, 0, field.height);
            black.addColorStop(0, 'rgba(0,0,0,0)');
            black.addColorStop(1, 'rgba(0,0,0,1)');
            ctx.fillStyle = black;
            ctx.fillRect(0, 0, field.width, field.height);

            // The marker is drawn twice, dark under light, so it stays visible
            // against both ends of the field.
            var mx = sat * field.width;
            var my = (1 - val) * field.height;
            ctx.beginPath();
            ctx.arc(mx, my, 6, 0, Math.PI * 2);
            ctx.strokeStyle = 'rgba(0,0,0,0.6)';
            ctx.lineWidth = 3;
            ctx.stroke();
            ctx.beginPath();
            ctx.arc(mx, my, 6, 0, Math.PI * 2);
            ctx.strokeStyle = '#fff';
            ctx.lineWidth = 1.5;
            ctx.stroke();
        }

        // `apply` is false while dragging the hex box, so typing does not fight
        // the field being redrawn under the cursor.
        function redraw(apply) {
            var rgbArray = hsvToRgb(hue, sat, val);
            var hex = toHex(rgbArray);

            paintField();
            hueInput.value = Math.round(hue * 360);
            preview.style.background = hex;
            if (apply !== 'hex') { hexInput.value = hex; }
            if (trigger) { trigger.style.background = hex; }

            if (apply) {
                input.value = hex;
                applySetting(input);
            }
        }

        function pickFromEvent(event) {
            var box = field.getBoundingClientRect();
            sat = clamp01((event.clientX - box.left) / box.width);
            val = 1 - clamp01((event.clientY - box.top) / box.height);
            redraw(true);
        }

        field.addEventListener('pointerdown', function (event) {
            field.setPointerCapture(event.pointerId);
            pickFromEvent(event);
        });
        field.addEventListener('pointermove', function (event) {
            if (event.buttons === 1) { pickFromEvent(event); }
        });

        hueInput.addEventListener('input', function () {
            hue = Number(hueInput.value) / 360;
            redraw(true);
        });

        hexInput.addEventListener('input', function () {
            var parsed = parseHex(hexInput.value);
            if (!parsed) { return; }
            var next = rgbToHsv(parsed[0], parsed[1], parsed[2]);
            hue = next[0]; sat = next[1]; val = next[2];
            redraw('hex');
            input.value = toHex(parsed);
            applySetting(input);
        });

        redraw(false);
        return root;
    }

    function wireColorPickers() {
        Array.prototype.forEach.call(
            document.querySelectorAll('input[type="color"][data-setting]'),
            function (input) {
                input.classList.add('color-hidden');

                var trigger = el('button', 'color-trigger');
                trigger.type = 'button';
                trigger.style.background = input.value || '#FFFFFF';
                trigger.title = 'Choose a colour';
                trigger.setAttribute('aria-haspopup', 'dialog');

                trigger.addEventListener('click', function (event) {
                    event.stopPropagation();
                    var wasMine = openPicker && openPicker.trigger === trigger;
                    closePicker();
                    if (wasMine) { return; }

                    var root = buildPicker(input, trigger);
                    trigger.parentNode.insertBefore(root, trigger.nextSibling);
                    openPicker = { root: root, trigger: trigger };
                });

                // The stored value can change from elsewhere - a settings reload,
                // or another client - so the swatch follows the input.
                input.addEventListener('input', function () {
                    trigger.style.background = input.value;
                });

                input.parentNode.insertBefore(trigger, input.nextSibling);
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
                    // Written as an APNG-free animated strip: every captured
                    // frame stacked vertically in one PNG. No encoder, no
                    // dependency, and it opens anywhere.
                    saveRecording(recording.frames);
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

    // --- GIF encoding -------------------------------------------------------
    //
    // Written out rather than pulled in. The device serves its own assets from
    // flash with no internet behind it, so a CDN library is not an option, and
    // the embedded asset table takes text only. GIF89a with LZW is about a
    // hundred lines and has no dependencies, which is the right trade here.

    function ByteStream() {
        this.bytes = [];
    }
    ByteStream.prototype.byte = function (b) { this.bytes.push(b & 0xFF); };
    ByteStream.prototype.short = function (v) {
        this.bytes.push(v & 0xFF, (v >> 8) & 0xFF);
    };
    ByteStream.prototype.text = function (t) {
        for (var i = 0; i < t.length; ++i) { this.bytes.push(t.charCodeAt(i) & 0xFF); }
    };

    // A palette built from what is actually on screen.
    //
    // This panel shows a handful of distinct colours at a time, so an exact
    // palette almost always fits in 256 entries and the GIF is lossless. Only
    // when it does not does this fall back to quantising, and then it says so
    // rather than silently degrading.
    function buildPalette(frames, pixelCount) {
        var map = {};
        var palette = [];
        var exact = true;
        var f, i;

        for (f = 0; f < frames.length && exact; ++f) {
            var binary = frames[f];
            for (i = 0; i < pixelCount; ++i) {
                var r = binary.charCodeAt(i * 3);
                var g = binary.charCodeAt(i * 3 + 1);
                var b = binary.charCodeAt(i * 3 + 2);
                var key = (r << 16) | (g << 8) | b;
                if (map[key] === undefined) {
                    if (palette.length >= 256) { exact = false; break; }
                    map[key] = palette.length;
                    palette.push([r, g, b]);
                }
            }
        }

        if (exact) { return { exact: true, map: map, palette: palette }; }

        // 3-3-2 bits. Coarse, but this is the fallback, not the normal path.
        map = null;
        palette = [];
        for (var v = 0; v < 256; ++v) {
            palette.push([
                Math.round(((v >> 5) & 0x07) * 255 / 7),
                Math.round(((v >> 2) & 0x07) * 255 / 7),
                Math.round((v & 0x03) * 255 / 3)
            ]);
        }
        return { exact: false, map: null, palette: palette };
    }

    function paletteIndex(built, r, g, b) {
        if (built.exact) { return built.map[(r << 16) | (g << 8) | b]; }
        return ((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6);
    }

    // GIF-flavoured LZW: codes grow from minCodeSize+1 up to 12 bits, then the
    // dictionary is cleared and it starts over.
    function lzwEncode(indices, minCodeSize) {
        var clearCode = 1 << minCodeSize;
        var endCode = clearCode + 1;
        var codeSize = minCodeSize + 1;
        var next = endCode + 1;
        var dict = {};

        var out = [];
        var bits = 0;
        var bitCount = 0;

        function emit(code) {
            // LSB-first, which is what GIF specifies and the usual place to get
            // this wrong.
            bits |= code << bitCount;
            bitCount += codeSize;
            while (bitCount >= 8) {
                out.push(bits & 0xFF);
                bits >>= 8;
                bitCount -= 8;
            }
        }

        emit(clearCode);

        var prefix = indices[0];
        for (var i = 1; i < indices.length; ++i) {
            var k = indices[i];
            var key = prefix + ',' + k;
            if (dict[key] !== undefined) {
                prefix = dict[key];
                continue;
            }

            emit(prefix);
            dict[key] = next++;

            if (next > (1 << codeSize)) {
                if (codeSize < 12) {
                    ++codeSize;
                } else {
                    emit(clearCode);
                    dict = {};
                    next = endCode + 1;
                    codeSize = minCodeSize + 1;
                }
            }
            prefix = k;
        }

        emit(prefix);
        emit(endCode);
        if (bitCount > 0) { out.push(bits & 0xFF); }
        return out;
    }

    function encodeGif(frames, width, height, delayCentis) {
        var pixelCount = width * height;
        var built = buildPalette(frames, pixelCount);
        var stream = new ByteStream();
        var i;

        stream.text('GIF89a');
        stream.short(width);
        stream.short(height);
        stream.byte(0xF7);   // global table, 256 entries
        stream.byte(0);      // background index
        stream.byte(0);      // default aspect ratio

        for (i = 0; i < 256; ++i) {
            var entry = built.palette[i] || [0, 0, 0];
            stream.byte(entry[0]);
            stream.byte(entry[1]);
            stream.byte(entry[2]);
        }

        // Netscape extension: loop forever. Without it most viewers play once,
        // and a single pass of a clock recording is not much use.
        stream.byte(0x21); stream.byte(0xFF); stream.byte(11);
        stream.text('NETSCAPE2.0');
        stream.byte(3); stream.byte(1); stream.short(0);
        stream.byte(0);

        for (var f = 0; f < frames.length; ++f) {
            var binary = frames[f];

            stream.byte(0x21); stream.byte(0xF9); stream.byte(4);
            stream.byte(0);              // no disposal, no transparency
            stream.short(delayCentis);
            stream.byte(0);
            stream.byte(0);

            stream.byte(0x2C);
            stream.short(0); stream.short(0);
            stream.short(width); stream.short(height);
            stream.byte(0);              // no local table, not interlaced

            var indices = new Array(pixelCount);
            for (i = 0; i < pixelCount; ++i) {
                indices[i] = paletteIndex(built,
                                          binary.charCodeAt(i * 3),
                                          binary.charCodeAt(i * 3 + 1),
                                          binary.charCodeAt(i * 3 + 2));
            }

            stream.byte(8);
            var data = lzwEncode(indices, 8);

            // Sub-blocks of at most 255 bytes, each preceded by its length.
            for (var at = 0; at < data.length; at += 255) {
                var chunk = data.slice(at, at + 255);
                stream.byte(chunk.length);
                for (var c = 0; c < chunk.length; ++c) { stream.byte(chunk[c]); }
            }
            stream.byte(0);
        }

        stream.byte(0x3B);   // trailer
        return { bytes: new Uint8Array(stream.bytes), exact: built.exact };
    }

    function saveRecording(frames) {
        if (!frames.length || !lastFrame) { return toast('nothing recorded', true); }

        // Played back at the rate it was captured, so what you watch is what
        // the panel did rather than an arbitrary speed.
        var delay = Math.max(2, Math.round(LIVE_INTERVAL_MS / 10));
        var result = encodeGif(frames, lastFrame.width, lastFrame.height, delay);

        var blob = new Blob([result.bytes], { type: 'image/gif' });
        var url = URL.createObjectURL(blob);
        var link = el('a');
        link.href = url;
        link.download = 'notrix-' + stamp() + '.gif';
        link.click();
        setTimeout(function () { URL.revokeObjectURL(url); }, 1000);

        toast(frames.length + ' frames' + (result.exact ? '' : ' (colours reduced)'));
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
        wireColorPickers();

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
