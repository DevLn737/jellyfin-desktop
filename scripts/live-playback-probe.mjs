// Only test-windows-live-playback.ps1 may launch this driver. It addresses a
// fresh owned portable app, uses a synthetic local fixture and never signs in.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createHash } from 'node:crypto';

const wait = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex');

function isOwnedPage(value, root) {
    try {
        if (value.type !== 'page') return false;
        const url = new URL(value.url);
        if (url.protocol !== 'file:' || url.hostname) return false;
        const relative = path.relative(path.resolve(root).toLowerCase(),
            path.resolve(fileURLToPath(url)).toLowerCase());
        return path.basename(fileURLToPath(url)).toLowerCase() === 'find-webclient.html' &&
            !!relative && !path.isAbsolute(relative) && relative !== '..' &&
            !relative.startsWith('..' + path.sep);
    } catch { return false; }
}

function ownedSocket(value, port) {
    try {
        const url = new URL(value);
        return url.protocol === 'ws:' && url.hostname === '127.0.0.1' &&
            url.port === String(port) && !url.username && !url.password;
    } catch { return false; }
}

function validatePosition(value) {
    return typeof value === 'number' && Number.isFinite(value) && value >= 29900 && value <= 37000;
}

async function initializeNative(getWindow = () => window, appearanceMs = 3000, totalMs = 6000) {
    const appearedBy = Date.now() + appearanceMs;
    const completedBy = Date.now() + totalMs;
    // The bundled CDP target can appear before profile-injected NativeShell.
    // Racing an undefined property would immediately resolve with undefined.
    while (!getWindow()?.apiPromise || !getWindow()?.initCompleted) {
        if (Date.now() >= appearedBy) throw Error('native-promises-unavailable');
        await new Promise(resolve => setTimeout(resolve, 50));
    }
    async function bounded(promise) {
        let timer;
        try {
            if (Date.now() >= completedBy) throw Error('native-initialization-timeout');
            return await Promise.race([promise, new Promise((_, reject) => {
                timer = setTimeout(() => reject(Error('native-initialization-timeout')),
                    Math.min(3000, Math.max(1, completedBy - Date.now())));
            })]);
        } finally { clearTimeout(timer); }
    }
    const nativeWindow = getWindow();
    const api = await bounded(nativeWindow.apiPromise);
    await bounded(nativeWindow.initCompleted);
    return { nativeShell: !!nativeWindow.NativeShell, channel: !!nativeWindow.qt?.webChannelTransport,
        player: !!api?.player, diagnosticsOff: api?.diagnostics?.capturing === false };
}

function initializationRetryCode(error) {
    const message = error.protocolError?.message || error.evaluationDescription || error.message || '';
    if (/execution context|cannot find context|inspected target navigated|promise was collected/i.test(message)) return 1;
    if (/native-promises-unavailable/.test(message)) return 2;
    if (/native-initialization-timeout/.test(message) || error.message === 'cdp-timeout') return 3;
    if (error.message === 'owned-chooser-not-stable') return 4;
    if (error.message === 'native-context-not-ready') return 5;
    return 0;
}

async function initializeOwnedPage(command, evaluate, root, contextForFrame, recordRetry,
    totalMilliseconds = 12000) {
    const deadline = Date.now() + totalMilliseconds;
    const remaining = () => Math.max(1, deadline - Date.now());
    await command('Page.enable', {}, Math.min(5000, remaining()));
    await command('Runtime.enable', {}, Math.min(5000, remaining()));
    let attempts = 0;
    while (Date.now() < deadline && attempts < 30) {
        ++attempts;
        try {
            const tree = await command('Page.getFrameTree', {}, Math.min(3000, remaining()));
            const frame = tree.frameTree?.frame;
            if (!frame || !isOwnedPage({ type: 'page', url: frame.url }, root)) throw Error('owned-chooser-not-stable');
            const contextId = contextForFrame(frame.id);
            if (!contextId) throw Error('native-context-not-ready');
            // Initialization only reads the injected bridge. A destroyed early
            // context can be retried; load/stop and other side effects cannot.
            const value = await evaluate(`(${initializeNative.toString()})()`,
                Math.min(7000, remaining()), contextId);
            assert.deepEqual(value, { nativeShell: true, channel: true, player: true, diagnosticsOff: true });
            return { value, attempts };
        } catch (error) {
            const code = initializationRetryCode(error);
            if (!code) throw error;
            recordRetry(code);
        }
        await wait(Math.min(100, remaining()));
    }
    throw Error('owned-native-initialization-deadline');
}

if (process.argv[2] === '--self-test') {
    const root = path.resolve('build', 'owned live probe');
    const own = { type: 'page', url: pathToFileURL(path.join(root, 'find-webclient.html')).href };
    assert.ok(isOwnedPage(own, root));
    assert.ok(!isOwnedPage({ ...own, type: 'iframe' }, root));
    assert.ok(!isOwnedPage({ ...own, url: 'https://example.invalid/' }, root));
    assert.ok(!isOwnedPage({ ...own, url: pathToFileURL(path.join(root + '-other', 'find-webclient.html')).href }, root));
    assert.ok(!isOwnedPage({ ...own, url: pathToFileURL(path.join(root, '..', 'find-webclient.html')).href }, root));
    assert.ok(!isOwnedPage({ ...own, url: pathToFileURL(path.join(root, 'other.html')).href }, root));
    assert.ok(ownedSocket('ws://127.0.0.1:9224/devtools/page/synthetic', 9224));
    assert.ok(!ownedSocket('ws://localhost:9224/devtools/page/synthetic', 9224));
    assert.ok(!ownedSocket('ws://127.0.0.1:9223/devtools/page/synthetic', 9224));
    assert.ok(!ownedSocket('ws://secret@127.0.0.1:9224/devtools/page/synthetic', 9224));
    assert.ok(validatePosition(32000));
    assert.ok(!validatePosition(NaN) && !validatePosition(Infinity) && !validatePosition('32000') && !validatePosition(6000));
    const delayedWindow = { NativeShell: {}, qt: { webChannelTransport: {} } };
    setTimeout(() => {
        delayedWindow.apiPromise = Promise.resolve({ player: {}, diagnostics: { capturing: false } });
        delayedWindow.initCompleted = Promise.resolve();
    }, 10);
    assert.deepEqual(await initializeNative(() => delayedWindow, 200, 500),
        { nativeShell: true, channel: true, player: true, diagnosticsOff: true });
    await assert.rejects(initializeNative(() => ({}), 20, 100), /native-promises-unavailable/);
    const rawContextError = Object.assign(Error('cdp-error'), {
        protocolError: { code: -32000, message: 'Execution context was destroyed.' }
    });
    let initializationCalls = 0;
    const retries = [], deadlines = [], methods = [];
    const initialized = await initializeOwnedPage(async method => {
        methods.push(method);
        return { frameTree: { frame: { id: 'synthetic', url: own.url } } };
    }, async (_expression, timeoutMs, contextId) => {
        deadlines.push(timeoutMs);assert.equal(contextId, 7);
        if (++initializationCalls === 1) throw rawContextError;
        return { nativeShell: true, channel: true, player: true, diagnosticsOff: true };
    }, root, () => 7, code => retries.push(code));
    assert.equal(initialized.attempts, 2);
    assert.deepEqual(retries, [1]);
    assert.deepEqual(methods.slice(0, 2), ['Page.enable', 'Runtime.enable']);
    assert.deepEqual(deadlines, [7000, 7000]);
    assert.equal(initializationRetryCode({ protocolError: { code: -32602, message: 'Invalid parameters' } }), 0);
    await assert.rejects(initializeOwnedPage(async () => {
        throw Object.assign(Error('cdp-error'), { protocolError: { code: -32602, message: 'Invalid parameters' } });
    }, async () => {}, root, () => 7, () => {}, 100), /cdp-error/);
    console.log('Live playback ownership, original-PTS and bounded CDP initialization guards passed; no app launched.');
} else {
    const [rootArgument, reportPathArgument, portArgument] = process.argv.slice(2);
    if (!rootArgument || !reportPathArgument || !/^\d+$/.test(portArgument || '')) throw Error('Owned driver arguments are required');
    const root = path.resolve(rootArgument), reportPath = path.resolve(reportPathArgument);
    const port = Number(portArgument);
    if (port < 1024 || port > 65535 || fs.existsSync(reportPath)) throw Error('Invalid port or reused driver report');
    const fixtureRoot = path.join(root, 'data', 'hls-video-30');
    const fixtureNames = new Set(['index.m3u8', 'segment-000.ts', 'segment-001.ts', 'segment-002.ts']);
    const report = {
        schemaVersion: 1,
        scope: 'owned-full-desktop-live-video-and-animated-transparent-web-composition',
        qualificationEligible: false,
        forcedGC: false,
        serverConnection: false,
        authentication: false,
        userMedia: false,
        syncPlay: false,
        initialized: false,
        completed: false,
        passed: false,
        cycles: [],
        stopLatencyMs: null,
        closeDuringActiveVideoPrepared: false
    };
    let socket, nextId = 0, failureCode = 'fixture-validation';
    const pending = new Map(), contexts = new Map();
    report.initializationRetryCodes = [];
    const started = performance.now();
    function save() {
        report.elapsedSeconds = (performance.now() - started) / 1000;
        const temporary = reportPath + '.tmp';
        fs.writeFileSync(temporary, JSON.stringify(report, null, 2) + '\n');
        fs.renameSync(temporary, reportPath);
    }
    try {
        const fixtureBytes = fs.readFileSync(path.join(fixtureRoot, 'fixture-info.json'));
        const fixture = JSON.parse(fixtureBytes.toString('utf8'));
        assert.deepEqual(Object.keys(fixture.sha256).sort(), [...fixtureNames].sort());
        for (const [name, expected] of Object.entries(fixture.sha256)) {
            assert.equal(sha256(fs.readFileSync(path.join(fixtureRoot, name))), expected);
        }
        report.fixtureManifestSha256 = sha256(fixtureBytes);
        report.fixtureBytes = fixture.generatedMediaBytes;
        save();
        failureCode = 'owned-bundled-page';
        let target;
        const targetDeadline = Date.now() + 12000;
        while (Date.now() < targetDeadline) {
            try {
                const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`, {
                    signal: AbortSignal.timeout(500)
                })).json();
                const owned = targets.filter(value => isOwnedPage(value, root));
                if (owned.length === 1) { target = owned[0]; break; }
            } catch {}
            await wait(100);
        }
        assert.ok(target && ownedSocket(target.webSocketDebuggerUrl, port));
        socket = new WebSocket(target.webSocketDebuggerUrl);
        await new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(Error('socket-timeout')), 3000);
            socket.addEventListener('open', () => { clearTimeout(timer); resolve(); }, { once: true });
            socket.addEventListener('error', () => { clearTimeout(timer); reject(Error('socket-failed')); }, { once: true });
        });
        socket.addEventListener('message', ({ data }) => {
            const value = JSON.parse(String(data));
            if (value.method === 'Runtime.executionContextCreated') {
                const context = value.params.context;
                if (context.auxData?.isDefault) contexts.set(context.id, context.auxData.frameId);
            } else if (value.method === 'Runtime.executionContextDestroyed') {
                contexts.delete(value.params.executionContextId);
            } else if (value.method === 'Runtime.executionContextsCleared') {
                contexts.clear();
            }
            const reply = pending.get(value.id);
            if (reply) { pending.delete(value.id); reply(value); }
        });
        const command = (method, params, timeoutMs = 5000) => new Promise((resolve, reject) => {
            const id = ++nextId;
            const timer = setTimeout(() => { pending.delete(id); reject(Error('cdp-timeout')); }, timeoutMs);
            pending.set(id, value => {
                clearTimeout(timer);
                if (value.error) {
                    console.error('Raw owned CDP error:', JSON.stringify(value.error));
                    reject(Object.assign(Error('cdp-error'), { protocolError: value.error }));
                } else resolve(value.result);
            });
            socket.send(JSON.stringify({ id, method, params }));
        });
        const evaluate = async (expression, timeoutMs = 5000, contextId) => {
            const params = { expression, awaitPromise: true, returnByValue: true };
            if (contextId) params.contextId = contextId;
            const value = await command('Runtime.evaluate', params, timeoutMs);
            if (value.exceptionDetails) {
                console.error('Raw owned evaluation exception:', JSON.stringify(value.exceptionDetails));
                throw Object.assign(Error('local-evaluation-failed'), {
                    evaluationDescription: value.exceptionDetails.exception?.description || value.exceptionDetails.text
                });
            }
            return value.result.value;
        };
        failureCode = 'native-initialization';
        const initialized = await initializeOwnedPage(command, evaluate, root,
            frameId => [...contexts].reverse().find(([, contextFrame]) => contextFrame === frameId)?.[0],
            code => { report.initializationRetryCodes.push(code); save(); });
        const initialization = initialized.value;
        report.initializationAttempts = initialized.attempts;
        report.initialized = true;
        report.nativeInitialization = initialization;
        failureCode = 'transparent-overlay';
        assert.equal(await evaluate(`(async()=>{
            const api=await window.apiPromise;
            await new Promise(resolve=>api.settings.setValue('main','fullscreen',true,resolve));
            await new Promise(resolve=>setTimeout(resolve,250));
            await new Promise(resolve=>api.settings.setValue('main','fullscreen',false,resolve));
            document.documentElement.style.background='transparent';
            document.body.style.background='transparent';document.body.style.margin='0';
            document.body.innerHTML='<div style="position:fixed;left:10px;top:10px;width:30px;height:30px;background:blue"></div>';
            const style=document.createElement('style');
            style.textContent='@keyframes probe {0%{transform:translateX(0)}100%{transform:translateX(120px)}} .probe{position:fixed;bottom:20px;left:20px;width:300px;height:60px;border-radius:10px;background:rgba(0,0,0,.35);backdrop-filter:blur(12px);box-shadow:0 0 20px rgba(0,0,0,.5);animation:probe 1s infinite alternate linear;color:white}';
            document.head.appendChild(style);const overlay=document.createElement('div');
            overlay.className='probe';overlay.textContent='Synthetic video overlay';document.body.appendChild(overlay);
            return getComputedStyle(overlay).backdropFilter==='blur(12px)'&&getComputedStyle(overlay).animationName==='probe';
        })()`), true);
        report.animatedTransparentBlurOverlay = true;
        save();
        const url = pathToFileURL(path.join(fixtureRoot, 'index.m3u8')).href;
        async function load(cycle) {
            const result = await evaluate(`(async()=>{
                const player=(await window.apiPromise).player;const began=performance.now();
                let readyListener;let timer;
                const ready=new Promise((resolve,reject)=>{
                    readyListener=(id,positionMs,paused)=>{if(id===${100 + cycle}){clearTimeout(timer);resolve({positionMs,paused});}};
                    player.playbackReady.connect(readyListener);timer=setTimeout(()=>reject(Error('ready-timeout')),3000);
                });
                // The ready deadline may expire before a delayed load ack.
                // Observe rejection now; awaiting the original promise below
                // still fails and retains the owned run's partial report.
                void ready.catch(()=>{});
                try {
                    const accepted=await new Promise(resolve=>player.load(${JSON.stringify(url)},
                        {startMilliseconds:30000,preserveTimestamps:true,autoplay:true,nativeRequestId:${100 + cycle}},
                        {type:'video',metadata:{},media:{}},'#1','',resolve));
                    if(!accepted)throw Error('load-rejected');const value=await ready;
                    return {accepted,readyLatencyMs:performance.now()-began,readyPositionMs:value.positionMs,readyPaused:value.paused};
                } finally {clearTimeout(timer);player.playbackReady.disconnect(readyListener);}
            })()`);
            assert.equal(result.accepted, true);
            assert.equal(result.readyPaused, false);
            assert.ok(validatePosition(result.readyPositionMs));
            assert.ok(Number.isFinite(result.readyLatencyMs) && result.readyLatencyMs >= 0);
            return result;
        }
        async function position() {
            const value = await evaluate(`(async()=>{
                const player=(await window.apiPromise).player;const begin=performance.now();
                const positionMs=await new Promise(resolve=>player.getPosition(resolve));
                return {positionMs,queryLatencyMs:performance.now()-begin};
            })()`);
            assert.ok(validatePosition(value.positionMs));
            assert.ok(Number.isFinite(value.queryLatencyMs) && value.queryLatencyMs >= 0);
            return value;
        }
        for (let cycle = 0; cycle < 4; ++cycle) {
            failureCode = 'video-cycle';
            const value = { cycle, ...await load(cycle), positions: [] };
            report.cycles.push(value);
            for (let tick = 0; tick < 4; ++tick) {
                await wait(1000);
                value.positions.push(await position());
                save();
            }
            assert.ok(value.positions.at(-1).positionMs > value.positions[0].positionMs + 1000);
        }
        failureCode = 'native-stop';
        report.stopLatencyMs = await evaluate(`(async()=>{
            const player=(await window.apiPromise).player;const begin=performance.now();
            await new Promise(resolve=>player.stop(resolve));return performance.now()-begin;
        })()`);
        assert.ok(Number.isFinite(report.stopLatencyMs) && report.stopLatencyMs >= 0 && report.stopLatencyMs < 2000);
        failureCode = 'active-video-for-close';
        report.closeLoad = await load(4);
        await wait(1000);
        report.closePosition = await position();
        assert.ok(report.closePosition.positionMs > report.closeLoad.readyPositionMs + 100);
        report.closeDuringActiveVideoPrepared = true;
        report.completed = true;
        report.passed = true;
    } catch (error) {
        report.failureCode = failureCode;
        process.exitCode = 1;
        console.error(error); // Raw owned-only failure log remains ignored.
    } finally {
        socket?.close();
        save();
    }
}
