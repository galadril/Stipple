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
        wireReboot();
        showPanel('panel-display');

        Promise.all([loadSettings(), loadDevice()])
            .then(function () { markConnection(true); })
            .catch(function (error) {
                markConnection(false);
                fail(error);
            });

        setInterval(poll, 4000);
    }

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', start);
    } else {
        start();
    }
}());
