// Local stress driver. The PowerShell owner measures the Desktop process tree.
// No server account, user media, forced GC or collection workaround is used.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';

const [root, output, portText, scenario, durationText, warmupText] = process.argv.slice(2);
const port = Number(portText), durationSeconds = Number(durationText), warmupSeconds = Number(warmupText);
assert.ok(root && output && ['navigation-svg', 'long-playback'].includes(scenario));
assert.ok(Number.isInteger(port) && port >= 1024 && port <= 65535);
assert.ok(Number.isFinite(durationSeconds) && durationSeconds > 0 && Number.isFinite(warmupSeconds) && warmupSeconds >= 0);
const status = { schemaVersion: 1, scenario, passed: false, initialized: false,
    operationCount: 0, fileChanges: 0, positionChecks: 0, navigationCount: 0,
    scope: scenario === 'navigation-svg' ? 'hidden-full-desktop-webengine-svg' : 'hidden-full-desktop-webengine-and-native-music',
    forcedGarbageCollection: false };
const writeStatus = () => fs.writeFileSync(output, JSON.stringify(status, null, 2) + '\n');
const wait = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="800" height="450" viewBox="0 0 800 450">' +
    '<rect width="800" height="450" fill="#111"/>' + Array.from({ length: 2400 }, (_, i) =>
        `<path d="M${i % 800} ${i % 450}q12 7 24 2t28 9" fill="none" stroke="hsl(${i % 360} 70% 55%)"/>`).join('') + '</svg>';
let svgRequests = 0, dataRequests = 0, socket, server;
const pending = new Map();
let sequence = 0;
function command(method, params = {}, timeout = 10000) {
    return new Promise((resolve, reject) => {
        const id = ++sequence;
        const timer = setTimeout(() => { pending.delete(id); reject(Error(method + ' timed out')); }, timeout);
        pending.set(id, { resolve, reject, timer });
        socket.send(JSON.stringify({ id, method, params }));
    });
}
async function evaluate(expression, timeout = 15000) {
    const result = await command('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true }, timeout);
    if (result.exceptionDetails) throw Error('Page evaluation failed');
    return result.result.value;
}
async function connect() {
    const deadline = performance.now() + 45000;
    let target;
    while (performance.now() < deadline) {
        try {
            const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`, { signal: AbortSignal.timeout(1000) })).json();
            target = targets.find(value => value.type === 'page' && value.url.startsWith('file:///') &&
                decodeURIComponent(new URL(value.url).pathname).toLowerCase().includes(root.replaceAll('\\', '/').toLowerCase()));
            if (target) break;
        } catch {}
        await wait(100);
    }
    assert.ok(target, 'Owned isolated app did not expose its bundled page');
    socket = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(Error('Owned DevTools connection timed out')), 5000);
        socket.addEventListener('open', () => { clearTimeout(timer); resolve(); }, { once: true });
        socket.addEventListener('error', () => { clearTimeout(timer); reject(Error('Owned DevTools connection failed')); }, { once: true });
    });
    socket.addEventListener('message', ({ data }) => {
        const message = JSON.parse(String(data)), waiter = pending.get(message.id);
        if (waiter) {
            pending.delete(message.id); clearTimeout(waiter.timer);
            if (message.error) waiter.reject(Error('DevTools command rejected'));
            else waiter.resolve(message.result);
        }
        // Do not retain raw protocol events, URLs, console messages or media.
    });
    socket.addEventListener('close', () => {
        for (const waiter of pending.values()) { clearTimeout(waiter.timer); waiter.reject(Error('Owned page closed')); }
        pending.clear();
    });
    await command('Page.enable'); await command('Runtime.enable');
}
async function navigate(url) {
    await command('Page.navigate', { url });
    for (let attempt = 0; attempt < 100; ++attempt) {
        try {
            if (await evaluate(`location.href === ${JSON.stringify(url)} && document.readyState === "complete" && typeof window.apiPromise !== "undefined"`)) {
                const playerReady = await evaluate('(async()=>!!(await window.apiPromise)?.player)()');
                const imageReady = scenario !== 'navigation-svg' || await evaluate('(()=>{const i=document.querySelector("#demo img");return !!i && i.complete && i.naturalWidth>0})()');
                if (playerReady && imageReady) return;
            }
        } catch {}
        await wait(50);
    }
    throw Error('Local navigation did not initialize NativeShell');
}
async function navigateChild(url) {
    await evaluate(`document.getElementById('navigation').src=${JSON.stringify(url)}`);
    const deadline = performance.now() + 5000;
    while (performance.now() < deadline) {
        if (await evaluate(`(()=>{const f=document.getElementById('navigation');return f?.contentWindow?.location.href===${JSON.stringify(url)} && f.contentDocument?.readyState==='complete'})()`, 1000)) return;
        await wait(50);
    }
    throw Error('Local child navigation did not complete');
}
async function checkReplacementImage() {
    // DOM replacement can coincide with this poll. Allow image decoding to
    // finish while still rejecting a stopped or broken SVG workload promptly.
    const deadline = performance.now() + 2000;
    while (performance.now() < deadline) {
        if (await evaluate('(()=>{const i=document.querySelector("#demo img");return !!i && i.complete && i.naturalWidth>0})()', 1000)) return;
        await wait(20);
    }
    throw Error('SVG workload did not load its replacement image');
}
try {
    const page = '<!doctype html><meta charset="utf-8"><div id="demo"></div>' +
        (scenario === 'navigation-svg' ? '<iframe id="navigation" src="/navigation?scene=initial"></iframe>' : '') + '<script src="/stress.js"></script>';
    server = http.createServer((request, response) => {
        const route = new URL(request.url, 'http://127.0.0.1').pathname;
        response.setHeader('Cache-Control', 'no-store');
        if (route === '/image.svg') { ++svgRequests; response.setHeader('Content-Type', 'image/svg+xml'); response.end(svg); }
        else if (route === '/data') { ++dataRequests; response.setHeader('Content-Type', 'text/html'); response.end('<div><img src="/image.svg" style="width:50%"></div>'); }
        else if (route === '/navigation') { response.setHeader('Content-Type', 'text/html'); response.end('<!doctype html><meta charset="utf-8"><p>Local child navigation</p>'); }
        else if (route === '/stress.js') {
            response.setHeader('Content-Type', 'application/javascript');
            response.end(scenario === 'navigation-svg' ? 'setInterval(()=>{const r=new XMLHttpRequest();r.onreadystatechange=()=>{if(r.readyState===4&&r.status===200)document.getElementById("demo").innerHTML=r.responseText};r.open("GET","/data");r.setRequestHeader("Cache-Control","no-cache");r.send()},100)' : '');
        } else {
            response.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; img-src 'self'; frame-src 'self'");
            response.setHeader('Content-Type', 'text/html; charset=utf-8'); response.end(page);
        }
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const base = `http://127.0.0.1:${server.address().port}/`;
    await connect();
    const version = await command('Browser.getVersion');
    status.chromiumProduct = version.product;
    await navigate(base);
    const wavPaths = [0, 1].map(index => path.join(root, 'data', `memory-music-${index}.wav`));
    if (scenario === 'long-playback') {
        for (const [index, wavPath] of wavPaths.entries()) {
            const rate = 16000, bytes = rate * 2 * (180 + index), wav = Buffer.alloc(44 + bytes);
            wav.write('RIFF'); wav.writeUInt32LE(36 + bytes, 4); wav.write('WAVEfmt ', 8);
            wav.writeUInt32LE(16, 16); wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22);
            wav.writeUInt32LE(rate, 24); wav.writeUInt32LE(rate * 2, 28);
            wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write('data', 36); wav.writeUInt32LE(bytes, 40);
            fs.writeFileSync(wavPath, wav);
        }
    }
    let requestId = 5000;
    async function loadMusic() {
        const wavPath = wavPaths[status.fileChanges % wavPaths.length];
        const value = await evaluate(`(async()=>{
            const p=(await window.apiPromise).player,id=${++requestId};
            const call=(name,...args)=>new Promise(resolve=>p[name](...args,resolve));
            const ready=new Promise((resolve,reject)=>{
                const done=(reply,position,paused)=>{if(reply===id){clearTimeout(timer);p.playbackReady.disconnect(done);resolve({position,paused})}};
                const timer=setTimeout(()=>{p.playbackReady.disconnect(done);reject(Error('Ready timeout'))},10000);
                p.playbackReady.connect(done);
            });
            const ack=await call('load',${JSON.stringify(pathToFileURL(wavPath).href)},
                {autoplay:true,startMilliseconds:0,nativeRequestId:id},{type:'music',metadata:{},media:{}},'#1','');
            if(ack!==true)throw Error('Load rejected'); const r=await ready;
            await call('setMuted',true); return r;
        })()`);
        assert.ok(Number.isFinite(value.position) && value.position >= 0 && value.paused === false);
        ++status.fileChanges;
    }
    if (scenario === 'long-playback') await loadMusic();
    status.initialized = true; writeStatus();
    // Both scenarios must initialize before the owner's shared measurement
    // clock starts. A missing owner cannot leave this driver running forever.
    const barrierDeadline = performance.now() + 90000;
    while (!fs.existsSync(path.join(root, 'memory-owner-start.json'))) {
        if (performance.now() >= barrierDeadline) throw Error('Memory owner did not release the start barrier');
        await wait(100);
    }
    // Keep the workload alive beyond the owner's final sample.
    const start = performance.now(), finish = start + (durationSeconds + warmupSeconds + 30) * 1000;
    let nextChange = start + 60000, nextProgress = start, lastPosition = -1;
    while (performance.now() < finish) {
        await wait(1000);
        if (performance.now() >= nextChange) {
            // Keep the main SVG/XHR V8 context alive for the entire run. Only
            // the script-free child document navigates, so reload cannot hide GC growth.
            if (scenario === 'navigation-svg') { await navigateChild(base + 'navigation?scene=' + status.navigationCount); ++status.navigationCount; }
            else { await loadMusic(); lastPosition = -1; }
            nextChange = performance.now() + 60000;
        }
        if (scenario === 'long-playback') {
            const position = await evaluate('(async()=>{const p=(await window.apiPromise).player;return new Promise(resolve=>p.getPosition(resolve))})()');
            assert.ok(Number.isFinite(position) && position >= 0 && position > lastPosition, 'Music position did not advance');
            lastPosition = position; ++status.positionChecks;
            await evaluate(`document.getElementById('demo').textContent=${JSON.stringify(String(Math.round(position)))}`);
        } else {
            await checkReplacementImage();
        }
        ++status.operationCount;
        if (performance.now() >= nextProgress) {
            status.elapsedSeconds = (performance.now() - start) / 1000;
            status.svgRequests = svgRequests; status.dataRequests = dataRequests; writeStatus();
            nextProgress = performance.now() + 5000;
        }
    }
    status.elapsedSeconds = (performance.now() - start) / 1000;
    status.svgRequests = svgRequests; status.dataRequests = dataRequests;
    if (scenario === 'navigation-svg') assert.ok(dataRequests >= (durationSeconds + warmupSeconds) * 7 && svgRequests >= dataRequests * 0.8,
        'SVG request cadence was throttled or assets were cached');
    else assert.ok(status.positionChecks >= (durationSeconds + warmupSeconds) * 0.8 && status.fileChanges >= Math.floor(durationSeconds / 65),
        'Native playback workload was interrupted');
    status.passed = true;
} catch (error) {
    // Reports expose only a bounded generic failure reason, never raw protocol.
    status.failure = String(error.message).slice(0, 180); process.exitCode = 1;
} finally {
    try { writeStatus(); } finally {
        socket?.close();
        for (const waiter of pending.values()) clearTimeout(waiter.timer);
        pending.clear(); server?.close();
        // The tested page keeps issuing XHR until the owner closes the app.
        // Do not keep this driver alive waiting for those owned HTTP sockets.
        server?.closeAllConnections(); server?.unref();
    }
}
