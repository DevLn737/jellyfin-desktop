// Local-only runtime check driven by test-native-shell.ps1. No server account
// or user media is used; the app runs with a fresh portable profile and a
// generated silent WAV and music HLS through mpv's null audio output. A hidden
// QQuickWindow has no render pass; video rendering is checked by tests/render.
import http from 'node:http';
import fs from 'node:fs';
import assert from 'node:assert/strict';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';

const [reportPath, expectedRoot] = process.argv.slice(2);
if (!reportPath || !expectedRoot) throw new Error('Report path and isolated app root are required');
const expectedDevice = 'Тест — 日本語 ✓';
const deadline = Date.now() + 45000;
const events = [];
const report = { expectedDevice, expectedRoot,
    scope: 'hidden NativeShell/WebChannel and audio playback; video uses the offscreen renderer harness',
    phases: {} };
const fixtureRoot = path.join(expectedRoot, 'data', 'hls-silent-30');
const fixtureNames = new Set(['index.m3u8', 'segment-000.ts', 'segment-001.ts', 'segment-002.ts', 'segment-003.ts']);
const strictCsp = "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'";
const connectionRequests = [];
let socket;
const server = http.createServer((_request, response) => {
    const requested = new URL(_request.url, 'http://127.0.0.1').pathname;
    if (requested === '/System/Info/Public') {
        connectionRequests.push({ route: 'publicInfo', destination: _request.headers['sec-fetch-dest'] || '' });
        response.setHeader('Access-Control-Allow-Origin', '*');
        response.setHeader('Content-Security-Policy', strictCsp);
        response.setHeader('Content-Type', 'application/json; charset=utf-8');
        response.end(JSON.stringify({ Id: 'synthetic-native-shell-server' }));
        return;
    }
    const fixtureName = requested.startsWith('/fixture-hls/') ? requested.slice('/fixture-hls/'.length) : null;
    if (fixtureNames.has(fixtureName)) {
        response.setHeader('Content-Type', fixtureName.endsWith('.m3u8') ? 'application/vnd.apple.mpegurl' : 'video/mp2t');
        response.end(fs.readFileSync(path.join(fixtureRoot, fixtureName)));
        return;
    }
    if (requested === '/') {
        connectionRequests.push({ route: 'rootHtml', destination: _request.headers['sec-fetch-dest'] || '' });
    }
    // Real reverse proxies may allow CORS for the API but not for web HTML.
    // Direct navigation must work without fetching/re-writing this document.
    response.setHeader('Content-Security-Policy', strictCsp);
    response.setHeader('Content-Type', 'text/html; charset=utf-8');
    response.end('<!doctype html><html><head><meta charset="utf-8"><title>NativeShell CSP smoke</title></head><body>Local NativeShell smoke</body></html>');
});
const wait = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
try {
    for (const [directory, names] of [[fixtureRoot, fixtureNames]]) {
        const fixtureInfo = JSON.parse(fs.readFileSync(path.join(directory, 'fixture-info.json'), 'utf8'));
        for (const [name, sha256] of Object.entries(fixtureInfo.sha256)) {
            assert.ok(names.has(name), 'Fixture contains an unexpected filename');
            assert.equal(createHash('sha256').update(fs.readFileSync(path.join(directory, name))).digest('hex'), sha256);
        }
    }
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
    report.syntheticUrl = `http://127.0.0.1:${server.address().port}/`;
    let target;
    while (Date.now() < deadline) {
        try {
            const targets = await (await fetch('http://127.0.0.1:9222/json/list', {
                signal: AbortSignal.timeout(1000)
            })).json();
            target = targets.find(value => value.type === 'page' && value.url.startsWith('file:///') &&
                decodeURIComponent(new URL(value.url).pathname).toLowerCase()
                    .includes(expectedRoot.replaceAll('\\', '/').toLowerCase()));
            if (target) break;
        } catch {}
        await wait(100);
    }
    assert.ok(target, 'Isolated application did not expose its bundled first page within 45 seconds');
    socket = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('DevTools WebSocket connection timed out')), 3000);
        socket.addEventListener('open', () => { clearTimeout(timer); resolve(); }, { once: true });
        socket.addEventListener('error', () => { clearTimeout(timer); reject(new Error('DevTools WebSocket failed')); }, { once: true });
    });
    const pending = new Map();
    let sequence = 0;
    socket.addEventListener('message', ({ data }) => {
        const value = JSON.parse(String(data));
        if (value.id && pending.has(value.id)) {
            pending.get(value.id)(value);
            pending.delete(value.id);
        } else if (value.method) events.push(value);
    });
    const command = (method, params = {}, timeoutMs = 6000) => new Promise((resolve, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => {
            pending.delete(id);
            reject(new Error(`${method} timed out`));
        }, timeoutMs);
        pending.set(id, value => {
            clearTimeout(timer);
            if (value.error) reject(new Error(`${method}: ${JSON.stringify(value.error)}`));
            else resolve(value.result);
        });
        socket.send(JSON.stringify({ id, method, params }));
    });
    await command('Runtime.enable');
    await command('Log.enable');
    await command('Page.enable');
    await command('Network.enable');
    const expression = `(async () => {
        const result = { url: location.href, nativeShell: typeof window.NativeShell,
            deviceName: window.jmpInfo?.deviceName, channel: typeof window.QWebChannel,
            transport: !!window.qt?.webChannelTransport };
        const api = await Promise.race([window.apiPromise,
            new Promise((resolve, reject) => setTimeout(() => reject(Error('Native API timeout')), 2000))]);
        await Promise.race([window.initCompleted,
            new Promise((resolve, reject) => setTimeout(() => reject(Error('Native initialization timeout')), 2000))]);
        result.api = api ? Object.keys(api) : [];
        result.diagnosticsCapturing = api?.diagnostics?.capturing;
        const setting = (...args) => new Promise(resolve => api.settings.value(...args, resolve));
        result.savedServer = await setting('main', 'userWebClient');
        result.savedServerSnapshot = window.jmpInfo.settings.main.userWebClient;
        result.legacyMigration = {
            hardwareDecoding: await setting('video', 'hardwareDecoding'),
            autodetectCertBundle: await setting('main', 'autodetectCertBundle'),
            unknownUnicode: await setting('migrationSmoke', 'unknownUnicode'),
            nested: await setting('migrationSmoke', 'nested')
        };
        // Check opaque-page dialogs under strict CSP: the former video-only
        // selector missed these menus. Modal input and animations remain intact.
        const fixtureSheet = new CSSStyleSheet();
        fixtureSheet.replaceSync('.dialogBackdropOpened { opacity: .5; will-change: opacity; } .ownedModalDialog { opacity: .73; animation-name: ownedModalEntry; animation-duration: .14s; } @keyframes ownedModalEntry { from { transform: scale(.5); } to { transform: scale(1); } }');
        document.adoptedStyleSheets = [...document.adoptedStyleSheets, fixtureSheet];
        const backdrop = document.createElement('div');
        backdrop.className = 'dialogBackdrop dialogBackdropOpened';
        const dialog = document.createElement('div');
        dialog.className = 'ownedModalDialog dialog';
        document.body.append(backdrop, dialog);
        const backdropStyle = getComputedStyle(backdrop), dialogStyle = getComputedStyle(dialog);
        result.softwareDialogStyles = {
            enabled: window.jmpInfo.softwareWebEngine,
            opaqueDocument: !document.documentElement.classList.contains('transparentDocument'),
            backdropOpacity: Number(backdropStyle.opacity),
            backdropPointerEvents: backdropStyle.pointerEvents,
            backdropWillChange: backdropStyle.willChange,
            dialogOpacity: Number(dialogStyle.opacity),
            dialogAnimation: dialogStyle.animationName,
            dialogDuration: dialogStyle.animationDuration
        };
        backdrop.remove(); dialog.remove();
        document.adoptedStyleSheets = document.adoptedStyleSheets.filter(sheet => sheet !== fixtureSheet);
        result.plugins = [];
        for (const name of window.NativeShell?.getPlugins() || []) {
            const plugin = await Promise.race([window[name](),
                new Promise((resolve, reject) => setTimeout(() => reject(Error(name + ' timeout')), 2000))]);
            result.plugins.push({ name, loaded: typeof plugin === 'function' });
        }
        return result;
    })()`;
    async function verify(phase, expectedUrl) {
        let lastError;
        for (let attempt = 0; attempt < 12; attempt++) {
            try {
                const evaluation = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true });
                if (evaluation.exceptionDetails) throw new Error(JSON.stringify(evaluation.exceptionDetails));
                const value = evaluation.result.value;
                assert.equal(value.nativeShell, 'object');
                assert.equal(value.channel, 'function');
                assert.equal(value.transport, true);
                assert.equal(value.deviceName, expectedDevice);
                if (expectedUrl) {
                    assert.equal(value.url, expectedUrl);
                    assert.equal(new URL(value.savedServer).href, expectedUrl,
                        'Native saved server must match the connected document');
                    assert.equal(new URL(value.savedServerSnapshot).href, expectedUrl,
                        'JS settings must refresh after file-to-HTTP navigation and reload');
                }
                const requiredApi = ['diagnostics', 'input', 'player', 'power', 'settings', 'system', 'taskbar'];
                // Display can be absent on a headless Windows runner.
                assert.ok(requiredApi.every(name => value.api.includes(name)), JSON.stringify(value.api));
                assert.equal(value.diagnosticsCapturing, false);
                assert.deepEqual(value.legacyMigration, { hardwareDecoding: 'copy', autodetectCertBundle: true,
                    unknownUnicode: '保留', nested: { value: 17 } });
                assert.deepEqual(value.plugins.map(plugin => plugin.name),
                    ['mpvVideoPlayer', 'mpvAudioPlayer', 'jmpInputPlugin', 'jmpDiagnosticsPlugin', 'jmpUpdatePlugin']);
                assert.ok(value.plugins.every(plugin => plugin.loaded));
                assert.deepEqual(value.softwareDialogStyles, {
                    enabled: true, opaqueDocument: true,
                    backdropOpacity: 0, backdropPointerEvents: 'auto', backdropWillChange: 'auto',
                    dialogOpacity: .73, dialogAnimation: 'ownedModalEntry', dialogDuration: '0.14s'
                }, 'Software dialog styling must preserve modal input and animations under strict CSP');
                report.phases[phase] = value;
                return;
            } catch (error) { lastError = error; await wait(100); }
        }
        throw lastError;
    }
    async function waitForLoad(cursor, description) {
        for (let attempt = 0; attempt < 50; attempt++) {
            if (events.slice(cursor).some(event => event.method === 'Page.loadEventFired')) return;
            await wait(100);
        }
        throw new Error(`${description} did not complete loading within 5 seconds`);
    }
    async function navigate(method, params) {
        const cursor = events.length;
        await command(method, params);
        await waitForLoad(cursor, method);
    }
    await verify('bundledFirstLaunch');
    const connectionCursor = events.length;
    // Do not await tryConnect across navigation: successful navigation destroys
    // the chooser's JS execution context. The load event proves its completion.
    const chooser = await command('Runtime.evaluate', {
        expression: `(() => {
            if (location.protocol !== 'file:' || typeof tryConnect !== 'function') {
                throw Error('Actual bundled server chooser is unavailable');
            }
            void tryConnect(${JSON.stringify(report.syntheticUrl)});
            return { invoked: true, originScheme: location.protocol };
        })()`, returnByValue: true
    });
    assert.ok(!chooser.exceptionDetails, JSON.stringify(chooser.exceptionDetails));
    assert.deepEqual(chooser.result.value, { invoked: true, originScheme: 'file:' });
    await waitForLoad(connectionCursor, 'Actual server chooser connection');
    await verify('strictCsp', report.syntheticUrl);
    const discoveryRequests = connectionRequests.filter(request => request.route === 'publicInfo');
    const htmlRequests = connectionRequests.filter(request => request.route === 'rootHtml');
    assert.equal(discoveryRequests.length, 1, 'Chooser must validate the synthetic server once');
    assert.equal(htmlRequests.length, 1, 'Chooser must request web HTML once by direct navigation');
    assert.equal(htmlRequests[0].destination, 'document', 'Web HTML must not be fetched from the file origin');
    const rootNetworkRequests = events.slice(connectionCursor).filter(event =>
        event.method === 'Network.requestWillBeSent' && event.params.request.url === report.syntheticUrl);
    assert.equal(rootNetworkRequests.length, 1, 'Unexpected HTML fetch before or instead of navigation');
    assert.equal(rootNetworkRequests[0].params.type, 'Document', 'Chooser must navigate to an actual remote document');
    assert.equal(new URL(report.phases.strictCsp.savedServer).href, report.syntheticUrl,
        'The saved server must survive the chooser document being replaced');
    report.phases.chooserConnection = { publicInfoRequests: 1, rootHtmlRequests: 1,
        rootHtmlFetchRequests: 0, directDocumentNavigation: true, savedServerMatches: true,
        savedServerSnapshotMatches: true,
        publicInfoAllowsCors: true, rootHtmlAllowsCors: false, strictCsp: true };
    await navigate('Page.reload', { ignoreCache: true });
    await verify('reload', report.syntheticUrl);
    // CSP errors indicate dynamic qrc/file script loading accidentally returned.
    const cspFailures = events.filter(event => event.method === 'Log.entryAdded' &&
        event.params.entry.source === 'security' && event.params.entry.level === 'error');
    assert.equal(cspFailures.length, 0, JSON.stringify(cspFailures));

    const sampleRate = 16000;
    const dataLength = sampleRate * 2 * 12;
    const wav = Buffer.alloc(44 + dataLength);
    wav.write('RIFF', 0); wav.writeUInt32LE(36 + dataLength, 4);
    wav.write('WAVEfmt ', 8); wav.writeUInt32LE(16, 16);
    wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22);
    wav.writeUInt32LE(sampleRate, 24); wav.writeUInt32LE(sampleRate * 2, 28);
    wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34);
    wav.write('data', 36); wav.writeUInt32LE(dataLength, 40);
    const fixturePath = path.join(expectedRoot, 'data', 'readiness-fixture.wav');
    fs.writeFileSync(fixturePath, wav);
    const mediaUrl = pathToFileURL(fixturePath).href;
    const hlsUrl = new URL('fixture-hls/index.m3u8', report.syntheticUrl).href;
    const directTsUrl = pathToFileURL(path.join(fixtureRoot, 'segment-000.ts')).href;
    const readinessExpression = `(async () => {
        const player = (await window.apiPromise).player;
        const samples = [], seeks = [];
        const pending = new Map();
        const ready = (id, positionMs, paused) => {
            const sample = { id, positionMs, paused };
            samples.push(sample);
            const waiter = pending.get(id);
            if (waiter) { pending.delete(id); clearTimeout(waiter.timer); waiter.resolve(sample); }
        };
        const seeking = id => seeks.push(id);
        player.playbackReady.connect(ready);
        player.seekStarted.connect(seeking);
        const waitFor = id => new Promise((resolve, reject) => {
            const timer = setTimeout(() => { pending.delete(id); reject(Error('Native ready timeout: ' + id)); }, 4000);
            pending.set(id, { resolve, reject, timer });
        });
        const call = (method, ...args) => new Promise(resolve => player[method](...args, resolve));
        const load = (id, startMilliseconds) => call('load', ${JSON.stringify(mediaUrl)},
            { startMilliseconds, autoplay: false, nativeRequestId: id },
            { type: 'music', metadata: {}, media: {} }, '#1', '');
        try {
            const coldPromise = waitFor(101);
            if (await load(101, 0) !== true) throw Error('Native load rejected');
            const cold = await coldPromise;
            const seekPromise = waitFor(102);
            if (await call('seekToForReady', 2750, 102) !== true) throw Error('Native seek rejected');
            const seek = await seekPromise;
            const positionMs = await call('getPosition');
            await load(103, 1000);
            const latestPromise = waitFor(104);
            if (await load(104, 2000) !== true) throw Error('Latest native load rejected');
            const latest = await latestPromise;
            const hlsPromise = waitFor(105);
            if (await call('load', ${JSON.stringify(hlsUrl)},
                { startMilliseconds: 30000, preserveTimestamps: true, autoplay: false, nativeRequestId: 105 },
                { type: 'music', metadata: {}, media: {} }, '#1', '') !== true) throw Error('Native HLS load rejected');
            const hls = await hlsPromise;
            const hlsDurationMs = await call('getDuration');
            const hlsSeekPromise = waitFor(106);
            if (await call('seekToForReady', 32000, 106) !== true) throw Error('Native HLS seek rejected');
            const hlsSeek = await hlsSeekPromise;
            const directPromise = waitFor(107);
            if (await call('load', ${JSON.stringify(directTsUrl)},
                { startMilliseconds: 0, autoplay: false, nativeRequestId: 107 },
                { type: 'music', metadata: {}, media: {} }, '#1', '') !== true) throw Error('Native direct TS load rejected');
            const directTs = await directPromise;
            await call('stop');
            const countAfterStop = samples.length;
            await new Promise(resolve => setTimeout(resolve, 200));
            return { cold, seek, latest, hls, hlsSeek, directTs,
                hlsDurationMs, positionMs, seeks,
                stoppedWithoutLateReady: samples.length === countAfterStop };
        } catch (error) {
            return { failure: String(error.stack || error), samples, seeks,
                positionMs: await call('getPosition') };
        } finally {
            player.playbackReady.disconnect(ready);
            player.seekStarted.disconnect(seeking);
            for (const waiter of pending.values()) clearTimeout(waiter.timer);
            pending.clear();
            await call('stop');
        }
    })()`;
    const readiness = await command('Runtime.evaluate', {
        expression: readinessExpression, awaitPromise: true, returnByValue: true
    }, 30000);
    assert.ok(!readiness.exceptionDetails, JSON.stringify(readiness.exceptionDetails));
    const native = readiness.result.value;
    report.phases.nativePausedReadiness = native;
    assert.ok(!native.failure, JSON.stringify(native));
    assert.equal(native.cold.id, 101);
    assert.equal(native.cold.paused, true);
    assert.ok(Number.isFinite(native.cold.positionMs) && native.cold.positionMs <= 20);
    assert.equal(native.seek.id, 102);
    assert.equal(native.seek.paused, true);
    assert.ok(Math.abs(native.seek.positionMs - 2750) <= 20, JSON.stringify(native));
    assert.ok(Math.abs(native.positionMs - 2750) <= 20, JSON.stringify(native));
    assert.ok(native.seeks.includes(102), JSON.stringify(native));
    assert.equal(native.latest.id, 104);
    assert.equal(native.latest.paused, true);
    assert.ok(Math.abs(native.latest.positionMs - 2000) <= 20, JSON.stringify(native));
    assert.equal(native.hls.paused, true);
    assert.ok(Math.abs(native.hls.positionMs - 30000) <= 50, JSON.stringify(native));
    assert.equal(native.hlsSeek.paused, true);
    assert.ok(Math.abs(native.hlsSeek.positionMs - 32000) <= 50, JSON.stringify(native));
    assert.ok(Math.abs(native.hlsDurationMs - 36000) <= 100, JSON.stringify(native));
    assert.equal(native.directTs.paused, true);
    assert.ok(native.directTs.positionMs >= 0 && native.directTs.positionMs <= 50, JSON.stringify(native));
    assert.equal(native.stoppedWithoutLateReady, true);
    report.phases.nativePausedReadiness = native;
    const diagnostics = await command('Runtime.evaluate', {
        expression: `(async () => {
            const diagnostics = (await window.apiPromise).diagnostics;
            const changes = [];
            const changed = active => changes.push(active);
            diagnostics.captureChanged.connect(changed);
            const call = (method, ...args) => new Promise(resolve => diagnostics[method](...args, resolve));
            const waitForState = async active => {
                // WebChannel batches NOTIFY properties independently of the
                // invokable reply. Let the UI see start before sending stop.
                for (let attempt = 0; attempt < 40; attempt++) {
                    if (diagnostics.capturing === active && changes.at(-1) === active) return;
                    await new Promise(resolve => setTimeout(resolve, 25));
                }
                throw Error('Diagnostic state notification timed out: ' + active);
            };
            try {
                const initiallyOff = diagnostics.capturing === false;
                const ping = await call('ping');
                await call('startCapture');
                await waitForState(true);
                await call('startCapture');
                await call('stopCapture');
                await waitForState(false);
                await call('clearCapture');
                return { initiallyOff, ping, changes, finallyOff: diagnostics.capturing === false };
            } finally {
                diagnostics.captureChanged.disconnect(changed);
                await call('clearCapture');
            }
        })()`, awaitPromise: true, returnByValue: true
    });
    assert.ok(!diagnostics.exceptionDetails, JSON.stringify(diagnostics.exceptionDetails));
    report.phases.diagnosticsControls = diagnostics.result.value;
    assert.deepEqual(report.phases.diagnosticsControls,
        { initiallyOff: true, ping: true, changes: [true, false], finallyOff: true });
    report.passed = true;
    console.log('NativeShell chooser/API-only CORS/direct navigation/startup/CSP/reload/Unicode, mpv paused audio readiness and music HLS/TS timestamps passed');
} catch (error) {
    report.passed = false;
    report.failure = String(error.stack || error);
    console.error(report.failure);
    process.exitCode = 1;
} finally {
    report.events = events;
    fs.writeFileSync(reportPath, JSON.stringify(report, null, 2));
    socket?.close();
    server.close();
}
