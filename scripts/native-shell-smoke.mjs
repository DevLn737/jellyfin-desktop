// Local-only runtime check driven by test-native-shell.ps1. No server account
// or media is used; the app runs with a fresh portable profile.
import http from 'node:http';
import fs from 'node:fs';
import assert from 'node:assert/strict';

const [reportPath, expectedRoot] = process.argv.slice(2);
if (!reportPath || !expectedRoot) throw new Error('Report path and isolated app root are required');
const expectedDevice = 'Тест — 日本語 ✓';
const deadline = Date.now() + 45000;
const events = [];
const report = { expectedDevice, expectedRoot, phases: {} };
let socket;
const server = http.createServer((_request, response) => {
    response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self'");
    response.setHeader('Content-Type', 'text/html; charset=utf-8');
    response.end('<!doctype html><html><head><meta charset="utf-8"><title>NativeShell CSP smoke</title></head><body>Local NativeShell smoke</body></html>');
});
const wait = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
try {
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
    const command = (method, params = {}) => new Promise((resolve, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => {
            pending.delete(id);
            reject(new Error(`${method} timed out`));
        }, 6000);
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
    const expression = `(async () => {
        const result = { url: location.href, nativeShell: typeof window.NativeShell,
            deviceName: window.jmpInfo?.deviceName, channel: typeof window.QWebChannel,
            transport: !!window.qt?.webChannelTransport };
        const api = await Promise.race([window.apiPromise,
            new Promise((resolve, reject) => setTimeout(() => reject(Error('Native API timeout')), 2000))]);
        result.api = api ? Object.keys(api) : [];
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
                if (expectedUrl) assert.equal(value.url, expectedUrl);
                const requiredApi = ['diagnostics', 'input', 'player', 'power', 'settings', 'system', 'taskbar'];
                // Display can be absent on a headless Windows runner.
                assert.ok(requiredApi.every(name => value.api.includes(name)), JSON.stringify(value.api));
                assert.deepEqual(value.plugins.map(plugin => plugin.name),
                    ['mpvVideoPlayer', 'mpvAudioPlayer', 'jmpInputPlugin', 'jmpDiagnosticsPlugin', 'jmpUpdatePlugin']);
                assert.ok(value.plugins.every(plugin => plugin.loaded));
                report.phases[phase] = value;
                return;
            } catch (error) { lastError = error; await wait(100); }
        }
        throw lastError;
    }
    async function navigate(method, params) {
        const cursor = events.length;
        await command(method, params);
        for (let attempt = 0; attempt < 50; attempt++) {
            if (events.slice(cursor).some(event => event.method === 'Page.loadEventFired')) return;
            await wait(100);
        }
        throw new Error(`${method} did not complete loading within 5 seconds`);
    }
    await verify('bundledFirstLaunch');
    await navigate('Page.navigate', { url: report.syntheticUrl });
    await verify('strictCsp', report.syntheticUrl);
    await navigate('Page.reload', { ignoreCache: true });
    await verify('reload', report.syntheticUrl);
    // CSP errors indicate dynamic qrc/file script loading accidentally returned.
    const cspFailures = events.filter(event => event.method === 'Log.entryAdded' &&
        event.params.entry.source === 'security' && event.params.entry.level === 'error');
    assert.equal(cspFailures.length, 0, JSON.stringify(cspFailures));
    report.passed = true;
    console.log('NativeShell first launch, strict CSP, reload and Unicode passed');
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
