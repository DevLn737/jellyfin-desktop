// Exercises immutable custom Web core/wrappers against the actual NativeShell
// adapters. It has no HTTP server, account, installed profile or native process.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const crypto = require('node:crypto');
const vm = require('node:vm');
const { stripTypeScriptTypes } = require('node:module');

const fixturePath = 'tests/syncplay/web-reference';
const pins = JSON.parse(fs.readFileSync(`${fixturePath}/source-pins.json`, 'utf8'));
assert.equal(pins.commit, 'b2a97d40c6898726b030b0233a0c9dc89734d3e3');
for (const [name, entry] of Object.entries(pins.files)) {
  assert.equal(crypto.createHash('sha256').update(fs.readFileSync(`${fixturePath}/${name}`)).digest('hex'),
    entry.sha256, `${name}: immutable Web fixture changed`);
}

const flush = async () => { for (let i = 0; i < 40; i++) await Promise.resolve(); };
function createEvents() {
  const targets = new WeakMap();
  return {
    on(target, type, handler) {
      let types = targets.get(target); if (!types) targets.set(target, types = new Map());
      let handlers = types.get(type); if (!handlers) types.set(type, handlers = new Set());
      handlers.add(handler);
    },
    off(target, type, handler) { targets.get(target)?.get(type)?.delete(handler); },
    trigger(target, type, args = []) {
      for (const handler of Array.from(targets.get(target)?.get(type) || [])) handler({ type }, ...args);
    }
  };
}
function fixture(name, offsetTicks = 0) {
  const events = createEvents(), readyRequests = [], bufferingRequests = [], rates = [], loads = [], seeks = [], toasts = [];
  const timers = new Map(); let nextTimer = 0, now = 1791676801000, nativePosition = 0, nativePaused = true;
  const setTimer = (fn, delay = 0) => { const id = ++nextTimer; timers.set(id, { fn, when: now + delay }); return id; };
  const clearTimer = id => timers.delete(id);
  const signal = () => {
    const handlers = new Set();
    return { connect(fn) { handlers.add(fn); }, disconnect(fn) { handlers.delete(fn); },
      emit(...args) { for (const fn of Array.from(handlers)) fn(...args); }, get size() { return handlers.size; } };
  };
  const native = Object.fromEntries(['playing', 'paused', 'positionUpdate', 'finished', 'updateDuration',
    'error', 'buffering', 'playbackReady', 'seekStarted'].map(key => [key, signal()]));
  Object.assign(native, {
    load(url, options, metadata, audio, subtitle, callback) {
      loads.push({ url, options, metadata, audio, subtitle }); callback(true);
    },
    seekToForReady(position, id, callback) { seeks.push({ position, id }); callback(true); },
    pause() { queueMicrotask(() => { nativePaused = true; native.paused.emit(); }); },
    play() { queueMicrotask(() => { nativePaused = false; native.playing.emit(); }); },
    stop() {}, setVolume() {}, setMuted() {}, setSubtitleDelay() {}, setAudioDelay() {},
    getPosition(callback) { queueMicrotask(() => callback(nativePosition)); },
    setPlaybackRate(value, callback) { rates.push(value); queueMicrotask(() => callback(true)); }
  });
  const playbackManager = {
    syncPlayEnabled: false,
    pause(player) { player.pause(); }, unpause(player) { player.unpause(); },
    seek(ticks, player) { player.currentTime(ticks / 10000); },
    setPlaybackRate(value) { return player.setPlaybackRate(value); },
    getPlaybackRate() { return player.getPlaybackRate(); }
  };
  const context = { console: { ...console, debug() {}, log() {} }, URL, Promise, queueMicrotask, setTimeout: setTimer, clearTimeout: clearTimer,
    Date: class extends Date { constructor(...args) { super(...(args.length ? args : [now])); } static now() { return now; } },
    window: { api: { player: native, power: { setScreensaverEnabled() {} } },
      jmpInfo: { settings: { video: { default_playback_speed: 1.75 } } },
      setTimeout: setTimer, clearTimeout: clearTimer, addEventListener() {}, removeEventListener() {} },
    Events: events, playbackManager, toast(message) { toasts.push(message); },
    globalize: { translate(value) { return value; } },
    getSetting() {}, toBoolean(value, fallback) { return value ?? fallback; },
    toFloat(value, fallback) { return value ?? fallback; }
  };
  context.Helper = { TicksPerMillisecond: 10000, WaitForEventDefaultTimeout: 30000, WaitForPlayerEventTimeout: 500,
    waitForEventOnce(emitter, type, timeout) {
      return new Promise((resolve, reject) => {
        const handler = (_, ...args) => { events.off(emitter, type, handler); clearTimer(timer); resolve(args); };
        events.on(emitter, type, handler);
        const timer = timeout ? setTimer(() => { events.off(emitter, type, handler); reject(new Error('event timeout')); }, timeout) : 0;
      });
    }
  };
  vm.createContext(context);
  function loadWeb(file, exportedName) {
    let source = fs.readFileSync(`${fixturePath}/${file}`, 'utf8');
    if (file.endsWith('.ts')) source = stripTypeScriptTypes(source);
    source = source.replace(/^import[\s\S]*?;\s*/gm, '').replace(/^export default .*;\s*$/gm, '').replace(/^export /gm, '');
    vm.runInContext(source + (exportedName ? `\nthis.${exportedName} = ${exportedName};` : ''), context, { filename: file });
  }
  loadWeb('PlaybackRate.ts'); loadWeb('PlayerPosition.ts'); loadWeb('GenericPlayer.js', 'GenericPlayer');
  context.SyncPlay = { Players: { GenericPlayer: context.GenericPlayer } };
  loadWeb('QueueManager.js', 'QueueManager'); loadWeb('NoActivePlayer.js', 'NoActivePlayer');
  loadWeb('HtmlVideoPlayer.js', 'HtmlVideoPlayer'); loadWeb('PlaybackCore.js', 'PlaybackCore'); loadWeb('QueueCore.js', 'QueueCore');
  vm.runInContext(fs.readFileSync('native/mpvPlaybackLifecycle.js', 'utf8'), context);
  vm.runInContext(fs.readFileSync(`native/${name}.js`, 'utf8') + `\nthis.Player = ${name};`, context);
  const player = new context.Player({ events, playbackManager,
    appSettings: { get(key) { return key === 'volume' ? 0.8 : undefined; } },
    appHost: {}, appRouter: {}, loading: { show() {}, hide() {} }, globalize: context.globalize,
    dashboard: { default: { setBackdropTransparency() {} } }, toast: context.toast });
  if (name === 'mpvVideoPlayer') player.createMediaElement = () => {
    if (!player._hasConnection) {
      player._hasConnection = true;
      native.playing.connect(player.onPlaying);
      native.paused.connect(player.onPause);
      native.positionUpdate.connect(player.onTimeUpdate);
    }
    return Promise.resolve();
  };
  const apiClient = { serverInfo() { return { Id: 'server' }; }, requestSyncPlayReady(data) { readyRequests.push(data); },
    requestSyncPlayBuffering(data) { bufferingRequests.push(data); } };
  const core = new context.PlaybackCore(), queue = new context.QueueCore();
  const manager = { getPlaybackCore() { return core; }, getQueueCore() { return queue; },
    getPlayerWrapper() { return this.wrapper; }, getApiClient() { return apiClient; },
    getLastPlaybackCommand() { return null; }, isRemote() { return false; },
    isPlaybackActive() { return !!this.wrapper?.isPlaybackActive(); }, isFollowingGroupPlayback() { return true; },
    isSyncPlayEnabled() { return playbackManager.syncPlayEnabled; }, haltGroupPlayback() {}, clearSyncIcon() {}, showSyncIcon() {},
    timeSyncCore: { localDateToRemote(date) { return date; }, remoteDateToLocal(date) { return date; } },
    getTimeSyncCore() { return this.timeSyncCore; },
    getController() { return { setPlaybackRate: value => manager.rateApi(value) }; },
    rateApi() { return Promise.reject({ response: { status: 409 } }); }
  };
  core.init(manager); queue.init(manager);
  queue.lastPlayQueueUpdate = { PlayingItemIndex: 0, StartPositionTicks: 300000000, LastUpdate: new context.Date(), PlaybackRate: 1.25,
    Playlist: [{ ItemId: 'item', PlaylistItemId: 'queue-occurrence' }] };
  queue.playlist = [{ Id: 'item', PlaylistItemId: 'queue-occurrence', ServerId: 'server' }];
  const streamInfo = { url: 'https://media.invalid/stream', item: { Id: 'item' }, mediaSource: { Id: 'source', MediaStreams: [] },
    playerStartPositionTicks: 300000000, transcodingOffsetTicks: offsetTicks, fullscreen: false, playMethod: 'Transcode' };
  playbackManager.play = async options => {
    const owned = { ...streamInfo, playerStartPositionTicks: options.startPositionTicks ?? streamInfo.playerStartPositionTicks };
    await player.play(owned);
    events.trigger(player, 'playbackstart', [{ PlaylistItemId: 'queue-occurrence' }]);
  };
  manager.wrapper = new context.HtmlVideoPlayer(player, manager);
  function finish(id, position, paused = true) {
    nativePosition = position; nativePaused = paused;
    if (paused) native.paused.emit(); else native.playing.emit();
    native.positionUpdate.emit(position);
    native.playbackReady.emit(id, position, paused);
  }
  function close() { core.resetPlaybackState(); manager.wrapper.unbindFromPlayer(); player._lifecycle.dispose(); timers.clear(); }
  return { player, context, native, playbackManager, manager, core, queue, streamInfo, loads, seeks, rates,
    readyRequests, bufferingRequests, toasts, finish, close,
    async advance(ms) { now += ms; for (const [id, timer] of Array.from(timers)) if (timer.when <= now) { timers.delete(id); timer.fn(); } await flush(); } };
}

async function groupPlayback(name, offsetTicks) {
  const f = fixture(name, offsetTicks);
  try {
    f.manager.wrapper.bindToPlayer();
    const personalRate = f.player.getPlaybackRate();
    f.core.setBasePlaybackRate(1.25, false);
    f.queue.startPlayback(f.manager.getApiClient()); await flush();
    assert.equal(f.loads.length, 1);
    const load = f.loads[0];
    assert.equal(load.options.autoplay, false, 'cold native group load is actually paused');
    assert.equal(load.options.startMilliseconds, 30000 - offsetTicks / 10000);
    assert.equal(f.readyRequests.length, 0, 'queue acknowledgement is not native Ready');
    f.native.playing.emit(); await flush();
    assert.equal(f.readyRequests.length, 0, 'playing before native restart is not Ready');
    f.finish(load.options.nativeRequestId + 100, load.options.startMilliseconds); await flush();
    assert.equal(f.readyRequests.length, 0, 'stale native generations cannot satisfy Ready');
    f.finish(load.options.nativeRequestId, load.options.startMilliseconds); await flush();
    assert.ok(f.readyRequests.length > 0, 'real paused native restart releases Ready barrier');
    for (const ready of f.readyRequests) {
      assert.equal(ready.PlaylistItemId, 'queue-occurrence', 'Ready uses queue occurrence, never media ItemId');
      assert.equal(ready.PositionTicks, 300000000, 'Ready reports absolute logical media ticks');
      assert.equal(ready.IsPlaying, false);
    }
    assert.equal(f.player.currentTime(), 30000 - offsetTicks / 10000, 'ordinary Web player position remains raw');
    assert.equal((f.player.currentTime() * 10000) + f.streamInfo.transcodingOffsetTicks, 300000000,
      'ordinary manager adds untouched stream offset exactly once');
    assert.equal(await f.manager.wrapper.currentTimeAsync(), 30000, 'owned SyncPlay wrapper samples logical milliseconds');
    assert.equal(f.streamInfo.transcodingOffsetTicks, offsetTicks, 'native adapter never mutates shared streamInfo');
    assert.equal(f.player.getPlaybackRate(), 1.25);

    const beforeSeekReady = f.readyRequests.length;
    await f.core.applyCommand({ Command: 'Seek', PlaybackRate: 1.5, PositionTicks: 400000000,
      PlaylistItemId: 'queue-occurrence', When: new f.context.Date() }); await flush();
    assert.equal(f.seeks.length, 1);
    const seek = f.seeks[0];
    assert.equal(seek.position, 40000 - offsetTicks / 10000);
    assert.equal(f.readyRequests.length, beforeSeekReady, 'seek command acceptance cannot release readiness');
    f.finish(seek.id, seek.position - 1000, false); await flush();
    assert.equal(f.readyRequests.length, beforeSeekReady, 'misaligned old restart cannot satisfy current seek');
    f.finish(seek.id, seek.position, false); await flush();
    assert.ok(f.readyRequests.length > beforeSeekReady);
    assert.equal(f.readyRequests.at(-1).PositionTicks, 400000000);
    assert.equal(f.readyRequests.at(-1).IsPlaying, false, 'Web pause is acknowledged before Ready');
    assert.equal(f.player.getPlaybackRate(), 1.5);
    f.player.setMute(true, false);
    f.native.playing.emit(); await flush();
    assert.equal(f.player.isMuted(), true, 'resume cannot newly unmute the native player');
    assert.equal(f.player.getPlaybackRate(), 1.5, 'resume preserves group base speed');

    const requestsBefore409 = f.rates.length;
    await f.playbackManager.setPlaybackRate(2); await flush();
    assert.equal(f.rates.length, requestsBefore409, 'HTTP409 cannot fall back to a local rate setter');
    assert.equal(f.player.getPlaybackRate(), 1.5);
    assert.equal(f.toasts.at(-1), 'MessageSyncPlayPlaybackRateNotSupported');

    f.core.enableSyncCorrection = true;
    f.core.syncEnabled = true;
    f.core.lastSyncTime = new f.context.Date(f.context.Date.now() - 2000);
    f.core.lastCommand = { Command: 'Unpause', PlaybackRate: 1.5, PositionTicks: 400000000,
      PlaylistItemId: 'queue-occurrence', When: new f.context.Date() };
    f.core.syncPlaybackTime({ currentTime: new f.context.Date(), currentPosition: 39900 }); await flush();
    assert.equal(f.player.getPlaybackRate(), 1.6, 'actual Web SpeedToSync changes only effective native speed');
    assert.equal(f.playbackManager.getPlaybackRate(), 1.5, 'group controls retain the authoritative base speed');
    assert.equal(f.manager.wrapper.savedPlaybackRate, personalRate, 'correction preserves saved personal speed');

    const original = f.player._lifecycle._wrapperOriginal.currentTime;
    f.core.resetPlaybackState();
    f.context.Events.trigger(f.manager, 'enabled', [false]);
    f.manager.wrapper.unbindFromPlayer(); await flush();
    assert.equal(f.manager.wrapper.currentTime, original, 'leave removes owned position adaptation');
    assert.equal(f.player.getPlaybackRate(), personalRate, 'leave restores personal native speed');
    await f.advance(10000);
    assert.equal(f.player.getPlaybackRate(), personalRate, 'cancelled group timers cannot overwrite personal speed');
  } finally { f.close(); }
}

async function lifecycleEdges() {
  const f = fixture('mpvAudioPlayer');
  try {
    f.player.setPlaybackRate(1.75); await flush();
    const first = f.player.play(f.streamInfo); await flush();
    const firstId = f.loads.at(-1).options.nativeRequestId;
    const second = f.player.play(f.streamInfo); await flush();
    await assert.rejects(first, /replaced/);
    const secondId = f.loads.at(-1).options.nativeRequestId;
    f.finish(firstId, 30000, false); await flush();
    let resolved = false; second.then(() => { resolved = true; }); await flush();
    assert.equal(resolved, false, 'old file cannot complete newer load');
    f.finish(secondId, 30000, false); await second;
    assert.equal(f.player.getPlaybackRate(), 1.75, 'audio new file preserves personal speed');
    f.player._lifecycle.dispose();
    assert.equal(f.native.playbackReady.size, 0);
    const third = f.player.play(f.streamInfo); await flush();
    f.finish(f.loads.at(-1).options.nativeRequestId, 30000, true); await third;
    assert.equal(f.native.playbackReady.size, 1, 'reused native owner registers one readiness listener');

    const setRate = f.native.setPlaybackRate;
    let rateAck;
    f.native.setPlaybackRate = (_, callback) => { rateAck = callback; };
    f.player.setPlaybackRate(1.5);
    let sampled = false;
    const rejectedSample = f.player.currentTimeAsync().then(value => { sampled = true; return value; });
    await flush();
    assert.equal(sampled, false, 'Ready-facing position awaits actual native rate acknowledgement');
    rateAck(false);
    await assert.rejects(rejectedSample, /rejected the playback rate/);
    assert.equal(f.player.getPlaybackRate(), 1.75, 'native rejection restores the confirmed effective rate');
    f.native.setPlaybackRate = setRate;

    const recovered = f.player.play({ ...f.streamInfo, playerStartPositionTicks: 0 }); await flush();
    f.finish(f.loads.at(-1).options.nativeRequestId, 90000, true); await recovered;
    assert.equal(await f.player.currentTimeAsync(), 90000, 'nonzero initial PTS is valid for a zero-start Live TV load');
    const pending = f.player.play(f.streamInfo); await flush();
    await f.advance(30000);
    await assert.rejects(pending, /30 seconds/);
    assert.match(f.toasts.at(-1), /30 seconds/);

    const timestamps = f.context.window._mpvPlaybackLifecycle.preserveTimestamps;
    assert.equal(timestamps({ playMethod: 'DirectPlay' }, 'https://media.invalid/index.m3u8'), false);
    assert.equal(timestamps({ playMethod: 'Transcode', mediaSource: { TranscodingSubProtocol: 'hls' } }, 'https://media.invalid/x'), true);
    assert.equal(timestamps({ playMethod: 'Transcode' }, 'https://media.invalid/index.m3u8?x=1'), true);
    assert.equal(timestamps({ playMethod: 'Transcode' }, 'https://media.invalid/stream?CopyTimeStamps=true'), true);
    assert.equal(timestamps({ playMethod: 'Transcode' }, 'https://media.invalid/stream.mkv'), false);
  } finally { f.close(); }
}

async function missingOffsetHook() {
  const f = fixture('mpvAudioPlayer', 200000000);
  try {
    f.playbackManager.syncPlayEnabled = true;
    f.playbackManager._localSetPlaybackRate = function () {};
    await assert.rejects(f.player.play(f.streamInfo), /cannot report native transcode/);
    assert.equal(f.loads.length, 0, 'missing private hook never claims logical transcode readiness');
    assert.equal(f.readyRequests.length, 0);
    f.playbackManager.syncPlayEnabled = false;
    const zero = f.player.play({ ...f.streamInfo, transcodingOffsetTicks: 0 }); await flush();
    f.finish(f.loads.at(-1).options.nativeRequestId, 30000, true); await zero;
    assert.equal(await f.player.currentTimeAsync(), 30000, 'ordinary zero-offset compatibility needs no Web private hook');
  } finally { f.close(); }
}

(async () => {
  for (const name of ['mpvVideoPlayer', 'mpvAudioPlayer']) {
    await groupPlayback(name, 0); // Direct Play/HLS absolute timestamps.
    await groupPlayback(name, 200000000); // Progressive transcode starts at logical 20s, native 0s.
  }
  await lifecycleEdges();
  await missingOffsetHook();
  console.log('pinned Web/native cold Ready, paused seek, logical offsets, rates, 409, cleanup and stale-generation tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
