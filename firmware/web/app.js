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
            // Not a data-setting control: the password half is write-only, so
            // the pair cannot round-trip through the generic binding.
            showAccess(settings);
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

    // Zones as POSIX rules, not names.
    //
    // There is no timezone database on the device - no /usr/share/zoneinfo -
    // and shipping one would cost megabytes on an 8 MiB partition and go stale
    // the moment a government moved a date. A rule is one line and describes
    // the changeover rather than its consequences.
    //
    // A short list on purpose. Somewhere in this list is right for almost
    // everybody, and a menu of four hundred entries on a page for a clock is a
    // worse answer than a menu of twenty.
    var ZONES = [
        ['', 'Fixed offset (below)'],
        ['UTC0', 'UTC'],
        ['GMT0BST,M3.5.0/1,M10.5.0', 'United Kingdom, Ireland, Portugal'],
        ['CET-1CEST,M3.5.0,M10.5.0/3', 'Central Europe (Amsterdam, Berlin, Paris)'],
        ['EET-2EEST,M3.5.0/3,M10.5.0/4', 'Eastern Europe (Athens, Helsinki)'],
        ['MSK-3', 'Moscow'],
        ['EST5EDT,M3.2.0,M11.1.0', 'US Eastern'],
        ['CST6CDT,M3.2.0,M11.1.0', 'US Central'],
        ['MST7MDT,M3.2.0,M11.1.0', 'US Mountain'],
        ['MST7', 'Arizona (no daylight saving)'],
        ['PST8PDT,M3.2.0,M11.1.0', 'US Pacific'],
        ['AST4ADT,M3.2.0,M11.1.0', 'Atlantic Canada'],
        ['<-03>3', 'Brazil (Sao Paulo)'],
        ['<-05>5', 'Colombia, Peru'],
        ['IST-5:30', 'India'],
        ['<+04>-4', 'Gulf (Dubai)'],
        ['CST-8', 'China, Singapore, Hong Kong'],
        ['JST-9', 'Japan'],
        ['KST-9', 'Korea'],
        ['AEST-10AEDT,M10.1.0,M4.1.0/3', 'Sydney, Melbourne'],
        ['AEST-10', 'Brisbane (no daylight saving)'],
        ['NZST-12NZDT,M9.5.0,M4.1.0/3', 'New Zealand'],
        ['SAST-2', 'South Africa']
    ];

    function fillZones() {
        var select = $('timezone');
        if (!select) { return; }
        ZONES.forEach(function (zone) {
            select.appendChild(new Option(zone[1], zone[0]));
        });
        select.addEventListener('change', refreshTimezone);
    }

    // The offset only matters when no rule is chosen, so it says so rather than
    // sitting there looking equally important.
    function refreshTimezone() {
        var select = $('timezone');
        var offset = $('offset-field');
        var help = $('timezone-help');
        if (!select || !offset) { return; }

        var chosen = select.value !== '';
        offset.style.opacity = chosen ? '0.45' : '';
        var control = $('utcOffsetSeconds');
        if (control) { control.disabled = chosen; }

        if (help) {
            help.textContent = chosen
                ? 'Follows daylight saving on its own.'
                : 'No timezone chosen, so the fixed offset below is used - which will be an hour out for half the year anywhere that changes its clocks.';
        }
    }


    // --- overnight dimming ---------------------------------------------------
    //
    // The two time fields are not data-setting bound, because an <input
    // type="time"> speaks "HH:MM" and the device stores minutes after
    // midnight. Storing the string instead would put parsing on the firmware,
    // which is the wrong side of the wire for it.

    function minutesFromTime(text) {
        var parts = /^(\d{1,2}):(\d{2})/.exec(text || '');
        if (!parts) { return null; }
        var hours = parseInt(parts[1], 10);
        var minutes = parseInt(parts[2], 10);
        if (hours > 23 || minutes > 59) { return null; }
        return hours * 60 + minutes;
    }

    function timeFromMinutes(total) {
        var value = Math.max(0, Math.min(1439, total | 0));
        function two(n) { return (n < 10 ? '0' : '') + n; }
        return two(Math.floor(value / 60)) + ':' + two(value % 60);
    }

    function refreshNight() {
        var enabled = $('night-enabled');
        var fields = $('night-fields');
        if (!enabled || !fields) { return; }

        // Dimmed rather than hidden: a schedule that vanishes when switched
        // off gives no clue what switching it on would do.
        var on = enabled.checked;
        fields.style.opacity = on ? '' : '0.45';
        ['night-start', 'night-end', 'night-brightness'].forEach(function (id) {
            var control = $(id);
            if (control) { control.disabled = !on; }
        });
    }

    function wireNight() {
        var start = $('night-start');
        var end = $('night-end');
        var enabled = $('night-enabled');

        function sendTime(control, key) {
            return function () {
                var minutes = minutesFromTime(control.value);
                if (minutes === null) {
                    // Put back what the device has rather than sending
                    // nonsense: the field is already showing something the
                    // browser could not make sense of.
                    control.value = timeFromMinutes(
                        pathGet(settings, 'display.night.' + key) || 0);
                    return toast('That is not a time', true);
                }
                var patch = { display: { night: {} } };
                patch.display.night[key] = minutes;
                send('PATCH', '/api/v1/settings', patch)
                    .then(function (updated) { settings = updated; toast('Saved'); })
                    .catch(fail);
            };
        }

        if (start) { start.addEventListener('change', sendTime(start, 'startMinutes')); }
        if (end) { end.addEventListener('change', sendTime(end, 'endMinutes')); }
        if (enabled) { enabled.addEventListener('change', refreshNight); }
    }

    function writeNight() {
        var night = pathGet(settings, 'display.night');
        if (!night) { return; }
        if ($('night-start')) { $('night-start').value = timeFromMinutes(night.startMinutes); }
        if ($('night-end')) { $('night-end').value = timeFromMinutes(night.endMinutes); }
        refreshNight();
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

    var firstRunSeen = false;

    function loadDevice() {
        return send('GET', '/api/v1/device').then(function (device) {
            // A state, not a wizard: nothing is blocked behind it, and it
            // stops appearing the moment anything is saved rather than when
            // a button is pressed.
            firstRunSeen = !!device.firstRun;
            var welcome = $('first-run');
            if (welcome) { welcome.hidden = !firstRunSeen; }

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
                if (device.network.ssid) {
                    addFact(facts, 'Network', device.network.ssid);
                }
                if (device.network.rssiDbm !== undefined) {
                    addFact(facts, 'Signal', device.network.rssiDbm + ' dBm');
                }
                addFact(facts, 'Lease', leaseFor(device.network));
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


    // --- status tiles -------------------------------------------------------

    // Everything here is polled state, so every tile has to be able to say it
    // does not know. A plausible zero where a reading is missing is the exact
    // failure this project keeps finding on the device itself - a flat line
    // that means "no microphone", a 0% that means "nothing answered" - and it
    // reads the same way in a browser.
    function tile(label, value, note, meterPermille, meterClass) {
        var node = el('div', 'tile');
        node.appendChild(el('span', 'label', label));
        if (value === null || value === undefined) {
            node.className = 'tile unknown';
            node.appendChild(el('span', 'value', 'unknown'));
        } else {
            node.appendChild(el('span', 'value', String(value)));
        }
        if (note) { node.appendChild(el('span', 'note', note)); }
        if (meterPermille !== undefined && meterPermille !== null) {
            var meter = el('span', 'meter');
            var fill = el('i', meterClass || null);
            var width = Math.max(0, Math.min(1000, meterPermille));
            fill.style.width = (width / 10) + '%';
            meter.appendChild(fill);
            node.appendChild(meter);
        }
        return node;
    }

    function formatUptime(millis) {
        var seconds = Math.floor(millis / 1000);
        var days = Math.floor(seconds / 86400);
        var hours = Math.floor((seconds % 86400) / 3600);
        var minutes = Math.floor((seconds % 3600) / 60);
        if (days > 0) { return days + 'd ' + hours + 'h'; }
        if (hours > 0) { return hours + 'h ' + minutes + 'm'; }
        return minutes + 'm ' + (seconds % 60) + 's';
    }

    function renderStatus(device, diagnostics) {
        var host = $('status-tiles');
        if (!host) { return; }
        host.textContent = '';

        var can = (device && device.capabilities) || {};

        // Battery. "known" and the capability are separate facts: a device can
        // have a battery and not have heard from it yet.
        if (can.battery) {
            var battery = device.battery || {};
            if (battery.known) {
                // Both, now there is room. The voltage is what says whether to
                // believe the percentage, and the charge flag is what explains
                // it moving - neither is redundant.
                var volts = battery.millivolts
                    ? (battery.millivolts / 1000).toFixed(2) + ' V' : '';
                var note = [battery.charging ? 'charging' : '', volts]
                    .filter(Boolean).join(' · ');
                host.appendChild(tile('Battery', battery.percent + '%', note,
                    battery.percent * 10,
                    battery.percent <= 20 && !battery.charging ? 'low' : 'ok'));
            } else {
                host.appendChild(tile('Battery', null, 'no reading yet'));
            }
        }

        // Microphone. Present-but-silent and absent are different, and the
        // panel already learned that the hard way.
        if (can.microphone) {
            var mic = device.microphone || {};
            if (mic.known) {
                host.appendChild(tile('Mic', mic.amplitude, 'level',
                    Math.min(1000, Math.round(mic.amplitude / 32.767))));
            } else {
                host.appendChild(tile('Mic', null, 'not streaming'));
            }
        }

        var network = device && device.network;
        if (network && network.connected) {
            // The address leads: it is the one reading here somebody might
            // want to read out loud. The note carries the network name and
            // the signal, whichever of them the device can say - rssiDbm is
            // absent rather than zero when it cannot be measured.
            var detail = [];
            if (network.ssid) { detail.push(network.ssid); }
            if (network.rssiDbm !== undefined) { detail.push(network.rssiDbm + ' dBm'); }
            host.appendChild(tile('Wi-Fi', network.ipv4 || 'connected',
                detail.join(' · ')));
        } else if (network) {
            host.appendChild(tile('Wi-Fi', null, 'not connected'));
        }

        if (diagnostics) {
            host.appendChild(tile('Uptime', formatUptime(diagnostics.uptimeMillis || 0)));

            var render = diagnostics.render;
            if (render) {
                // Frames actually drawn, against frames the interval offered.
                // A healthy static clock skips far more than it renders, so a
                // high skip count is the good outcome rather than the alarming
                // one - which is why it is shown as "drawn" and not "dropped".
                var offered = (render.rendered || 0) + (render.skipped || 0);
                var share = offered > 0
                    ? Math.round((render.rendered * 1000) / offered) : 0;
                host.appendChild(tile('Frames', render.rendered,
                    render.overruns ? render.overruns + ' over budget'
                                    : render.lastRenderMillis + ' ms last',
                    share, render.overruns ? 'low' : null));
            }

            if (diagnostics.carousel) {
                host.appendChild(tile('Showing',
                    diagnostics.carousel.active || 'nothing',
                    diagnostics.carousel.paused ? 'paused' : ''));
            }

            var notes = diagnostics.notifications;
            if (notes && (notes.active || notes.pending || notes.dropped)) {
                host.appendChild(tile('Notifications', notes.active ? 'showing' : notes.pending,
                    notes.dropped ? notes.dropped + ' dropped' : 'pending'));
            }
        }
    }

    function addFact(list, term, value) {
        list.appendChild(el('dt', null, term));
        list.appendChild(el('dd', null, value === undefined ? 'unknown' : String(value)));
    }

    // --- apps ---------------------------------------------------------------

    // Which app row is being dragged. Held outside the handlers because
    // dragover fires on the row being passed over, not the one being moved.
    var dragging = null;

    // Which app's settings are open, so rebuilding the list can put them back.
    var expandedApp = null;

    function configPanelFor(id) {
        return document.querySelector('.app-config[data-app="' + id + '"]');
    }

    // Panels are moved, never cloned.
    //
    // They live in a hidden stash so bindControls() binds them once at load,
    // like every other setting. Cloning would produce two controls bound to
    // one setting, which is two answers to the same question and a race about
    // which one is right.
    function stashConfigPanels() {
        var stash = $('app-config-stash');
        if (!stash) { return; }
        Array.prototype.forEach.call(
            document.querySelectorAll('.app-config'), function (panel) {
                if (panel.parentNode !== stash) { stash.appendChild(panel); }
            });
    }

    function moveApp(id, index) {
        return send('PATCH', '/api/v1/apps/' + encodeURIComponent(id), { position: index })
            .then(function () { return loadApps(); })
            .catch(function (error) {
                // Reload either way: the list on screen no longer matches the
                // device, and guessing which of the two is right is how a UI
                // ends up lying about what order the apps are in.
                loadApps();
                fail(error);
            });
    }

    function loadApps() {
        return send('GET', '/api/v1/apps').then(function (result) {
            var list = $('app-list');
            // Rescued before the list is emptied, or clearing it would delete
            // the bound controls along with the rows.
            stashConfigPanels();
            list.textContent = '';

            var apps = result.apps || [];

            apps.forEach(function (app, position) {
                var row = el('li');
                row.draggable = true;
                row.dataset.appId = app.id;

                // A handle, so the row can still be dragged on a device where
                // the whole row is also a tap target.
                var grip = el('span', 'grip', '☰');
                grip.title = 'Drag to reorder';

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

                // Only where there is something to configure. A gear that opens
                // an empty box is worse than no gear: it says the app has
                // settings and then declines to name any.
                var panel = configPanelFor(app.id);
                var gear = null;
                if (panel) {
                    gear = el('button', 'btn btn-gear', '⚙');
                    gear.type = 'button';
                    gear.title = 'Settings for ' + app.name;
                    gear.addEventListener('click', function () {
                        expandedApp = expandedApp === app.id ? null : app.id;
                        loadApps();
                    });
                }

                // Buttons as well as dragging. Dragging does not exist on a
                // touch screen without a pile of pointer-event code, and the
                // phone is where somebody is most likely to be standing in
                // front of the clock wanting to change it.
                var up = el('button', 'btn btn-move', '▲');
                up.type = 'button';
                up.title = 'Move up';
                up.disabled = position === 0;
                up.addEventListener('click', function () { moveApp(app.id, position - 1); });

                var down = el('button', 'btn btn-move', '▼');
                down.type = 'button';
                down.title = 'Move down';
                down.disabled = position === apps.length - 1;
                down.addEventListener('click', function () { moveApp(app.id, position + 1); });

                row.addEventListener('dragstart', function (event) {
                    dragging = { id: app.id, from: position };
                    row.classList.add('dragging');
                    event.dataTransfer.effectAllowed = 'move';
                    // Firefox refuses to start a drag without payload.
                    event.dataTransfer.setData('text/plain', app.id);
                });

                row.addEventListener('dragend', function () {
                    row.classList.remove('dragging');
                    dragging = null;
                    Array.prototype.forEach.call(
                        list.children, function (child) { child.classList.remove('over'); });
                });

                row.addEventListener('dragover', function (event) {
                    if (!dragging || dragging.id === app.id) { return; }
                    event.preventDefault();
                    event.dataTransfer.dropEffect = 'move';
                    row.classList.add('over');
                });

                row.addEventListener('dragleave', function () { row.classList.remove('over'); });

                row.addEventListener('drop', function (event) {
                    event.preventDefault();
                    row.classList.remove('over');
                    if (!dragging || dragging.id === app.id) { return; }
                    moveApp(dragging.id, position);
                });

                row.appendChild(grip);
                row.appendChild(toggle);
                row.appendChild(body);
                row.appendChild(up);
                row.appendChild(down);
                if (gear) { row.appendChild(gear); }
                row.appendChild(show);
                list.appendChild(row);

                // The settings sit in their own list item under the app, not
                // inside the row: the row is a flex line of controls, and a
                // block of fields dropped into it lays out like a car crash.
                if (panel && expandedApp === app.id) {
                    row.classList.add('expanded');
                    if (gear) { gear.classList.add('on'); }
                    var holder = el('li', 'app-config-row');
                    panel.hidden = false;
                    holder.appendChild(panel);
                    list.appendChild(holder);
                }
            });

            if (!apps.length) {
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


    // --- network -------------------------------------------------------------

    // Signal as bars rather than a number. -67 dBm means nothing to most
    // people; four bars out of five does. The number stays in the note for
    // anybody it does mean something to.
    function signalBars(dbm) {
        if (dbm === undefined || dbm === null) { return ''; }
        var bars = dbm >= -55 ? 4 : dbm >= -67 ? 3 : dbm >= -78 ? 2 : 1;
        var out = '';
        for (var i = 0; i < 4; ++i) { out += i < bars ? '█' : '░'; }
        return out;
    }

    // How long is left on the address, in words a person reads rather than
    // a number of seconds. The useful distinction is hours-and-days, not
    // precision.
    function leaseFor(state) {
        if (!state.leaseManaged) {
            // Deliberately not silence. A device running on an address
            // nothing renews looks exactly like a healthy one until the
            // address is taken back, and then nobody can reach it to find
            // out why.
            return 'not managed - running on an inherited address';
        }
        if (state.leaseSeconds === -1) { return state.leaseState + ', granted forever'; }
        var left = state.leaseSeconds;
        var text;
        if (left >= 86400) { text = Math.floor(left / 86400) + 'd ' + Math.floor((left % 86400) / 3600) + 'h'; }
        else if (left >= 3600) { text = Math.floor(left / 3600) + 'h ' + Math.floor((left % 3600) / 60) + 'm'; }
        else if (left >= 60) { text = Math.floor(left / 60) + 'm'; }
        else { text = left + 's'; }
        return state.leaseState + ', ' + text + ' left';
    }

    var joinPolling = null;
    var OTHER_NETWORK = 'other:' + new Array(28).join('-');

    function showJoin(join) {
        var note = $('join-note');
        if (!note) { return; }
        if (!join) { note.textContent = ''; return; }

        if (join.stage === 'succeeded') {
            note.textContent = 'Connected to ' + join.ssid + '.';
        } else if (join.stage === 'failed') {
            // The device writes this sentence for the person who typed the
            // password, so it is shown as-is rather than mapped to a code.
            note.textContent = 'Could not join ' + join.ssid + ': ' + join.detail;
        } else {
            note.textContent = 'Joining ' + join.ssid + ' - ' + join.detail + '...';
        }
    }

    function watchJoin() {
        if (joinPolling) { return; }
        joinPolling = setInterval(function () {
            // This deliberately keeps polling through the failures. Joining
            // takes the radio away, so the requests in the middle of it are
            // *expected* to fail - giving up on the first one would report a
            // failure for every successful join.
            send('GET', '/api/v1/network').then(function (state) {
                showJoin(state.join);
                if (state.join && (state.join.stage === 'succeeded' ||
                                   state.join.stage === 'failed')) {
                    clearInterval(joinPolling);
                    joinPolling = null;
                    loadNetwork();
                }
            }).catch(function () { /* the radio is busy; ask again */ });
        }, 2000);
    }

    function loadNetwork() {
        return send('GET', '/api/v1/network').then(function (state) {
            var facts = $('network-facts');
            if (facts) {
                facts.textContent = '';
                addFact(facts, 'Status', state.connected ? 'connected' : 'not connected');
                if (state.ssid) { addFact(facts, 'Network', state.ssid); }
                if (state.ipv4) { addFact(facts, 'Address', state.ipv4); }
                if (state.rssiDbm !== undefined) {
                    addFact(facts, 'Signal', signalBars(state.rssiDbm) + '  ' +
                                             state.rssiDbm + ' dBm');
                }
                addFact(facts, 'Lease', leaseFor(state));
            }

            var scan = $('network-scan');
            if (scan) { scan.disabled = !state.canScan; }

            // One entry per name, keeping the strongest.
            //
            // A real scan is full of duplicates - the same network on 2.4 and
            // 5 GHz, and again from every access point in the house. The
            // device reports what the radio saw, which is correct; a person
            // choosing a network wants the name once. Eleven collapsed to six
            // on the first real scan.
            var byName = {};
            (state.networks || []).forEach(function (network) {
                var seen = byName[network.ssid];
                if (!seen || network.signalDbm > seen.signalDbm) {
                    // Keep "current" if any copy had it: being connected is a
                    // property of the network, not of one radio in it.
                    var current = network.current || (seen && seen.current);
                    byName[network.ssid] = network;
                    byName[network.ssid].current = !!current;
                }
            });

            var networks = Object.keys(byName).map(function (name) { return byName[name]; });
            // Strongest first. The one somebody wants is almost always near
            // the top.
            networks.sort(function (a, b) { return b.signalDbm - a.signalDbm; });

            var pick = $('join-pick');
            if (pick) {
                // The current choice survives a refresh, so a list reloading
                // underneath somebody mid-type does not lose it.
                var chosen = pick.value;
                pick.textContent = '';

                var blank = el('option', null,
                               networks.length ? 'Choose a network...' : 'No networks known');
                blank.value = '';
                pick.appendChild(blank);

                networks.forEach(function (network) {
                    var label = network.ssid + '  ' + signalBars(network.signalDbm);
                    if (network.current) { label += '  (connected)'; }
                    if (!network.secured) { label += '  (open)'; }
                    var option = el('option', null, label);
                    option.value = network.ssid;
                    option.dataset.secured = network.secured ? '1' : '';
                    pick.appendChild(option);
                });

                var other = el('option', null, 'Other - type a name...');
                other.value = OTHER_NETWORK;
                pick.appendChild(other);

                pick.value = chosen;
                if (pick.selectedIndex < 0) { pick.value = ''; }
            }

            // Said plainly. A remembered list presented as a current one
            // would have somebody wondering why the network they can see on
            // their phone is missing from the clock.
            var note2 = $('network-note');
            if (note2) {
                if (!state.canScan && !networks.length) {
                    note2.textContent = 'Cannot scan right now.';
                } else if (state.networksAreLive === false && networks.length) {
                    note2.textContent = 'From before the hotspot started - one radio ' +
                                        'cannot host and scan at once.';
                } else {
                    note2.textContent = '';
                }
            }

            showJoin(state.join);
        });
    }

    function showAccess(settings) {
        var state = $('access-state');
        var user = $('access-user');
        if (!state) { return; }

        var web = (settings && settings.web) || {};
        if (web.username) {
            state.textContent = 'On, as "' + web.username + '".';
            if (user && document.activeElement !== user) { user.value = web.username; }
        } else {
            // Said plainly. Two blank fields is not an answer to "is this
            // device protected".
            state.textContent = 'Off - anyone on this network can change this device.';
        }
    }

    function describeStaged(state) {
        var note = $('restore-state');
        if (!note) { return; }
        if (!state) {
            note.textContent = 'This build cannot stage a recovery image.';
            return;
        }
        if (state.stagedBytes > 0) {
            note.textContent = 'An image is staged at ' + state.path + ' (' +
                               Math.round(state.stagedBytes / 1024) + ' KB). ' +
                               'Holding reset during power-up installs it.';
        } else {
            note.textContent = 'Nothing staged at ' + state.path + '.';
        }
    }

    function loadRestoreState() {
        return send('GET', '/api/v1/system/restore-image')
            .then(describeStaged)
            .catch(function () { describeStaged(null); });
    }

    function wireRestoreImage() {
        var button = $('restore-upload');
        if (!button) { return; }

        button.addEventListener('click', function () {
            var picker = $('restore-file');
            var note = $('restore-state');
            var file = picker && picker.files && picker.files[0];
            if (!file) {
                if (note) { note.textContent = 'Choose an image first.'; }
                return;
            }

            button.disabled = true;
            if (note) {
                note.textContent = 'Uploading ' + Math.round(file.size / 1024) +
                                   ' KB and checking it...';
            }

            // Sent as raw bytes, not base64 in JSON. A three-megabyte image
            // would become four megabytes of text, on a device with about
            // seventeen free.
            file.arrayBuffer().then(function (bytes) {
                return fetch('/api/v1/system/restore-image', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/octet-stream' },
                    body: bytes,
                });
            }).then(function (response) {
                return response.json().then(function (body) {
                    if (!response.ok) {
                        throw new Error((body.error && body.error.message) ||
                                        ('HTTP ' + response.status));
                    }
                    return body;
                });
            }).then(function (body) {
                if (note) {
                    note.textContent = 'Staged ' + Math.round(body.bytes / 1024) +
                                       ' KB at ' + body.path + '. Nothing was ' +
                                       'flashed. Holding reset during power-up ' +
                                       'installs it.';
                }
                if (picker) { picker.value = ''; }
            }).catch(function (err) {
                // Shown as-is. The device writes these for a person: "the
                // file is damaged", "built for a different device".
                if (note) { note.textContent = 'Refused: ' + (err.message || 'failed'); }
            }).then(function () { button.disabled = false; });
        });
    }

    function wireAccess() {
        var save = $('access-save');
        var clear = $('access-clear');
        var state = $('access-state');
        if (!save || !clear) { return; }

        save.addEventListener('click', function () {
            var user = ($('access-user') || {}).value || '';
            var pass = ($('access-pass') || {}).value || '';
            if (!user.trim() || !pass) {
                if (state) {
                    state.textContent = 'Both a username and a password are needed.';
                }
                return;
            }

            save.disabled = true;
            send('PATCH', '/api/v1/settings', { web: { username: user, password: pass } })
                .then(function () {
                    // Not kept in the field. The device never gives it back,
                    // so a browser is the only place it would linger - and
                    // the next request needs it, which the browser will now
                    // ask for.
                    var field = $('access-pass');
                    if (field) { field.value = ''; }
                    if (state) {
                        state.textContent = 'On. Your browser will ask for it on the ' +
                                            'next request.';
                    }
                })
                .catch(function (err) {
                    if (state) { state.textContent = (err && err.message) || 'Failed.'; }
                })
                .then(function () { save.disabled = false; });
        });

        clear.addEventListener('click', function () {
            if (!confirm('Turn off the password? Anyone on this network will be able ' +
                         'to change this device.')) {
                return;
            }
            clear.disabled = true;
            // Username first in the same patch: clearing only the password
            // would leave a username set, which the device refuses as
            // half-configured - correctly, and unhelpfully if the UI asked
            // for it.
            send('PATCH', '/api/v1/settings', { web: { username: '', password: '' } })
                .then(function () { return loadSettings(); })
                .catch(fail)
                .then(function () { clear.disabled = false; });
        });
    }

    function wireNetwork() {
        var scan = $('network-scan');
        if (!scan) { return; }

        scan.addEventListener('click', function () {
            scan.disabled = true;
            scan.textContent = 'Scanning...';
            send('POST', '/api/v1/network/scan')
                .then(function () {
                    // The radio takes seconds, and the device answered 202
                    // rather than waiting - so this waits instead, once,
                    // rather than polling something that finishes in one go.
                    return new Promise(function (resolve) { setTimeout(resolve, 3000); });
                })
                .then(loadNetwork)
                .catch(fail)
                .then(function () {
                    scan.disabled = false;
                    scan.textContent = 'Scan';
                });
        });

        var go = $('join-go');
        if (!go) { return; }

        var pick = $('join-pick');
        if (pick) {
            pick.addEventListener('change', function () {
                var other = pick.value === OTHER_NETWORK;
                var row = $('join-other-row');
                if (row) { row.hidden = !other; }

                var pass = $('join-password');
                if (pass) {
                    pass.value = '';
                    // An open network has no password, and a field asking for
                    // one that does not exist is a field somebody types into
                    // and then wonders why it failed. Left enabled for
                    // "Other", because a typed name says nothing about
                    // whether it is secured.
                    var option = pick.options[pick.selectedIndex];
                    pass.disabled = !other && !!option && option.value !== '' &&
                                    !option.dataset.secured;
                }
                if (other) {
                    var field = $('join-ssid');
                    if (field) { field.focus(); }
                }
            });
        }

        go.addEventListener('click', function () {
            var chosen = pick ? pick.value : '';
            var ssid = (chosen && chosen !== OTHER_NETWORK)
                ? chosen
                : (($('join-ssid') || {}).value || '');
            var password = ($('join-password') || {}).value || '';
            var note = $('join-note');

            if (!ssid.trim()) {
                if (note) { note.textContent = 'A network name is needed.'; }
                return;
            }

            // Said before anything happens, because the next thing that
            // happens is the device leaving the network this page arrived
            // over. A page that just went quiet would read as a crash.
            if (note) {
                note.textContent = 'Joining ' + ssid + '. This page will lose contact ' +
                                   'with the device for up to a minute.';
            }
            go.disabled = true;

            send('POST', '/api/v1/network/join', { ssid: ssid, password: password })
                .then(function () {
                    // The password is not kept in the field afterwards. It is
                    // stored on the device and the API never gives it back,
                    // so leaving it visible in a browser is the only place it
                    // would linger.
                    var pass = $('join-password');
                    if (pass) { pass.value = ''; }
                    watchJoin();
                })
                .catch(function (err) {
                    if (note) { note.textContent = (err && err.message) || 'Could not start.'; }
                })
                .then(function () { go.disabled = false; });
        });
    }

    // --- backup, restore, reset ---------------------------------------------

    function wireMaintenance() {
        var backup = $('backup');
        if (backup) {
            backup.addEventListener('click', function () {
                // Fetched fresh rather than serialising the copy this page is
                // holding: what gets saved should be what the device says it
                // has, not what a browser tab thinks it set an hour ago.
                send('GET', '/api/v1/settings')
                    .then(function (current) {
                        var name = (current.deviceName || 'notrix') + '-' + stamp() + '.json';
                        var blob = new Blob([JSON.stringify(current, null, 2)],
                                            { type: 'application/json' });
                        var url = URL.createObjectURL(blob);
                        var link = el('a');
                        link.href = url;
                        link.download = name;
                        link.click();
                        setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
                        toast('Backup saved');
                    })
                    .catch(fail);
            });
        }

        var restore = $('restore');
        var file = $('restore-file');
        if (restore && file) {
            restore.addEventListener('click', function () { file.click(); });

            file.addEventListener('change', function () {
                var chosen = file.files && file.files[0];
                if (!chosen) { return; }
                var reader = new FileReader();

                reader.onload = function () {
                    var parsed;
                    try {
                        parsed = JSON.parse(reader.result);
                    } catch (error) {
                        // Said plainly rather than sent to the device to be
                        // refused: the file never was settings, and the device
                        // has nothing useful to add.
                        file.value = '';
                        return toast('That file is not a backup', true);
                    }
                    if (!parsed || typeof parsed !== 'object') {
                        file.value = '';
                        return toast('That file is not a backup', true);
                    }

                    // Sent whole. The device validates every field and refuses
                    // the request if any of them is wrong, which is a better
                    // guarantee than this page picking through it - and it
                    // means a backup from a newer firmware fails loudly rather
                    // than half-applying.
                    send('PATCH', '/api/v1/settings', parsed)
                        .then(function () {
                            file.value = '';
                            return Promise.all([loadSettings(), loadApps()]);
                        })
                        .then(function () {
                            refreshTimezone();
                            writeNight();
                            describePassword();
                            previewTopic();
                            toast('Restored - the MQTT password needs typing in again');
                        })
                        .catch(function (error) {
                            file.value = '';
                            fail(error);
                        });
                };

                reader.onerror = function () {
                    file.value = '';
                    toast('Could not read that file', true);
                };
                reader.readAsText(chosen);
            });
        }

        function resetWith(includeApps, question) {
            return function () {
                if (!window.confirm(question)) { return; }
                send('POST', '/api/v1/system/reset', { apps: includeApps })
                    .then(function () {
                        return Promise.all([loadSettings(), loadApps()]);
                    })
                    .then(function () {
                        refreshTimezone();
                        writeNight();
                        describePassword();
                        previewTopic();
                        toast(includeApps ? 'Settings and apps reset' : 'Settings reset');
                    })
                    .catch(fail);
            };
        }

        var reset = $('reset');
        if (reset) {
            reset.addEventListener('click', resetWith(false,
                'Put every setting back to its default? Apps and icons are kept.'));
        }

        var resetAll = $('reset-all');
        if (resetAll) {
            resetAll.addEventListener('click', resetWith(true,
                'Reset every setting AND remove installed apps and icons? The built-in apps stay.'));
        }
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
        // Refreshed when the tab is open rather than on its own timer. A scan
        // list goes stale slowly, and polling one costs the device a socket
        // round trip for a page nobody is looking at.
        if (activePanel === 'panel-system') {
            loadRestoreState();
            return loadNetwork().catch(function () {});
        }
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
    // Sized so the canvas renders about 1:1 at its CSS ceiling of 936px
    // (52 * 18). Drawing a 519px canvas and letting the browser stretch it to
    // 936 gave uneven dots - a 9px LED scaled by 1.8 lands on half pixels, and
    // image-rendering: pixelated then rounds different columns differently.
    // The two numbers here and the max-width in app.css have to agree.
    var LED_SIZE = 16;
    var LED_GAP = 2;
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

        // Black first, so it is always index 0.
        //
        // The GIF header already declares 0 as the background, and the gaps
        // between LEDs are drawn in it - so seeding it here means the gap
        // colour is guaranteed to have an index rather than depending on the
        // panel happening to contain black. It almost always does; "almost"
        // is not a good enough reason to skip four lines.
        map[0] = 0;
        palette.push([0, 0, 0]);

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

    function encodeGif(frames, width, height, delayCentis, cell, gap) {
        // Scaled up, with the same LED grid the live view draws.
        //
        // The panel is 52x16, and a GIF of a 52x16 image is 52x16 - which is
        // what this wrote until somebody tried to look at one. A viewer
        // showing it at any size at all smears it, because scaling is the
        // viewer's choice and most of them choose smoothly. Baking the
        // upscale in is the only way to control how it looks, and drawing the
        // gaps makes it a picture of a matrix rather than a blurry rectangle.
        cell = cell || 1;
        gap = gap === undefined ? 0 : gap;
        var pitch = cell + gap;
        var outWidth = width * pitch - gap;
        var outHeight = height * pitch - gap;

        var pixelCount = width * height;
        var built = buildPalette(frames, pixelCount);
        var stream = new ByteStream();
        var i;

        stream.text('GIF89a');
        stream.short(outWidth);
        stream.short(outHeight);
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
            stream.short(outWidth); stream.short(outHeight);
            stream.byte(0);              // no local table, not interlaced

            // One byte per output pixel rather than a scaled copy of the RGB.
            // At a cell of 11 that is 120 KB a frame instead of 360 KB, and a
            // long recording is the case that matters.
            var indices = new Uint8Array(outWidth * outHeight);
            for (var sy = 0; sy < height; ++sy) {
                for (var sx = 0; sx < width; ++sx) {
                    var at = (sy * width + sx) * 3;
                    var index = paletteIndex(built,
                                             binary.charCodeAt(at),
                                             binary.charCodeAt(at + 1),
                                             binary.charCodeAt(at + 2));
                    var top = sy * pitch;
                    var left = sx * pitch;
                    for (var dy = 0; dy < cell; ++dy) {
                        var row = (top + dy) * outWidth + left;
                        for (var dx = 0; dx < cell; ++dx) {
                            indices[row + dx] = index;
                        }
                    }
                }
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
        // 11 and 1 gives 623 x 191 from a 52 x 16 panel: large enough to look
        // at without a viewer scaling it, small enough that a minute of
        // recording is still a file somebody can send.
        var result = encodeGif(frames, lastFrame.width, lastFrame.height, delay, 11, 1);

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
            .then(function () {
                // Two requests rather than one, because /device and
                // /diagnostics answer different questions and neither should
                // grow the other's fields just to save a round trip on a LAN.
                return Promise.all([
                    send('GET', '/api/v1/device'),
                    send('GET', '/api/v1/diagnostics')
                ]);
            })
            .then(function (answers) {
                renderStatus(answers[0], answers[1]);
            })
            .catch(function () { markConnection(false); });
    }

    // --- start --------------------------------------------------------------

    function start() {
        fillOffsets();
        fillZones();
        bindControls();
        wireNight();
        wireTabs();
        wireNotify();
        wireMqtt();
        wireReboot();
        wireMaintenance();
        wireNetwork();
        wireAccess();
        wireRestoreImage();
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
                // A device nobody has set up opens on the step that matters
                // rather than on a live view of a clock showing the wrong
                // time (ADR 0018). Not a modal and not a wizard - the same
                // page, in a different order.
                if (firstRunSeen) {
                    showPanel('panel-system');
                }
                describePassword();
                previewTopic();
                refreshTimezone();
                markConnection(true);
                writeNight();
                // Fill the tiles immediately rather than leaving the page
                // blank until the first poll four seconds later.
                poll();
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
