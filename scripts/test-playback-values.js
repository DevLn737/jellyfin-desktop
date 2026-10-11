const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

async function testPlayer(file, className) {
  const calls = [];
  const warnings = [];
  const toasts = [];
  const playbackManager = { syncPlayEnabled: false };
  let now = 10000;
  const context = {
    Date: class extends Date { static now() { return now; } },
    console: { debug() {}, error() {}, log() {}, warn(message) { warnings.push(message); } },
    window: {
      jmpInfo: { settings: { video: { default_playback_speed: 1 } } },
      api: {
        player: {
          setPlaybackRate(value, callback) { calls.push({ value, callback }); },
          seekTo(value) { calls.push({ seek: value }); },
          getPosition(callback) { callback(123456); }
        },
        power: {}
      }
    }
  };
  vm.createContext(context);
  vm.runInContext(`${fs.readFileSync(file, 'utf8')}\nthis.Player = ${className};`, context);
  const player = new context.Player({
    events: { trigger() {} }, loading: { show() {}, hide() {} }, appRouter: {},
    globalize: {}, appHost: {}, appSettings: { get() {} }, confirm() {}, toast(message) { toasts.push(message); },
    dashboard: { default: { setBackdropTransparency() {} } }, playbackManager
  });

  for (const value of [0, -1, 0.199, 5.001, NaN, Infinity, -Infinity, null,
    undefined, true, false, '', ' ', '1x', {}, []]) {
    const before = calls.length;
    const previous = player.getPlaybackRate();
    assert.equal(player.setPlaybackRate(value), false, `${className}: reject ${String(value)}`);
    assert.equal(calls.length, before);
    assert.equal(player.getPlaybackRate(), previous);
  }
  assert.equal(toasts.length, 1, 'continuous invalid corrections produce one throttled visible message');

  for (const value of [0.2, 0.225, 0.25, '0.75', 1, '1.25', 1.7514, 5]) {
    assert.equal(player.setPlaybackRate(value), true);
    const request = calls.at(-1);
    assert.equal(request.value, Math.round(Number(value) * 1000));
    assert.equal(player.getPlaybackRate(), request.value / 1000);
    request.callback(true);
  }

  const choices = Array.from(player.getSupportedPlaybackRates(), (entry) => entry.id);
  assert.equal(choices[0], 0.25);
  assert.equal(choices.at(-1), 5);
  assert.ok(choices.every((rate) => Number.isFinite(rate) && rate >= 0.25 && rate <= 5));

  player.setPlaybackRate(1);
  calls.at(-1).callback(true);
  player.setPlaybackRate(1.25);
  const stale = calls.at(-1);
  player.setPlaybackRate(1.5);
  calls.at(-1).callback(true);
  stale.callback(false);
  assert.equal(player.getPlaybackRate(), 1.5, 'stale rejection must not undo a newer correction');
  assert.equal(toasts.length, 1, 'stale rejection must not show a warning');
  now += 5000;
  player.setPlaybackRate(2);
  calls.at(-1).callback(false);
  assert.equal(player.getPlaybackRate(), 1.5, 'rejected request must not claim the new rate');
  assert.equal(warnings.length, 1);
  assert.equal(toasts.length, 2, 'latest rejected request shows a visible warning after cooldown');
  assert.ok(toasts.every(message => message === 'Requested playback rate is not supported by the native player.'));
  player.setPlaybackRate(2.5);
  const earlierFailure = calls.at(-1);
  player.setPlaybackRate(3);
  calls.at(-1).callback(false);
  earlierFailure.callback(false);
  assert.equal(player.getPlaybackRate(), 1.5, 'two rejected pending requests restore the last confirmed rate');
  assert.equal(toasts.length, 2, 'repeated rejected corrections stay throttled');
  player.setPlaybackRate(2.5);
  const earlierSuccess = calls.at(-1);
  player.setPlaybackRate(3);
  calls.at(-1).callback(false);
  earlierSuccess.callback(true);
  assert.equal(player.getPlaybackRate(), 2.5, 'a delayed earlier success resolves the last confirmed native rate');

  playbackManager.syncPlayEnabled = true;
  assert.equal(player.supports('PlaybackRate'), false, 'stock SyncPlay cannot advertise local rate changes');
  assert.deepEqual(Array.from(player.getSupportedPlaybackRates(), entry => entry.id), [1]);
  now += 5000;
  const beforeUnsupportedGroup = calls.length;
  for (const value of [0.2, 0.25, 1.25, 5]) {
    assert.equal(player.setPlaybackRate(value), false, 'stock group rejects local rates and speed corrections');
  }
  assert.equal(calls.length, beforeUnsupportedGroup, 'unsupported group rate must never reach native');
  assert.equal(player.getPlaybackRate(), 2.5, 'rejection does not claim a native rate change');
  assert.equal(toasts.at(-1), 'This SyncPlay group does not support changing playback rate.');
  assert.equal(player.setPlaybackRate(1), true, 'stock group can restore synchronized 1x');
  calls.at(-1).callback(true);
  playbackManager._localSetPlaybackRate = function () {};
  assert.equal(player.supports('PlaybackRate'), true, 'custom group-rate wrapper supports server-coordinated rate');
  assert.equal(player.getSupportedPlaybackRates().length, choices.length);
  for (const value of [0.2, 0.225, 1.25, 5]) {
    assert.equal(player.setPlaybackRate(value), true, 'custom wrapper permits local SyncPlay correction');
    calls.at(-1).callback(true);
  }
  delete playbackManager._localSetPlaybackRate;
  playbackManager.syncPlayEnabled = false;
  assert.equal(player.supports('PlaybackRate'), true, 'leaving a stock group restores standalone support');

  for (const value of [-1, NaN, Infinity, Number.MAX_SAFE_INTEGER + 1, true, '', ' ', 'bad']) {
    const before = calls.length;
    player.currentTime(value);
    assert.equal(calls.length, before, 'invalid seek must not reach WebChannel');
  }
  player.currentTime('1234.6');
  assert.equal(calls.at(-1).seek, 1235);
  player.currentTime(0);
  assert.equal(calls.at(-1).seek, 0);
  assert.equal(await player.currentTimeAsync(), 123456, 'async position is in milliseconds');

  if (className === 'mpvVideoPlayer') {
    for (const value of [null, 'bad', NaN, Infinity, 0.2, 6]) {
      player._playRate = undefined;
      context.window.jmpInfo.settings.video.default_playback_speed = value;
      assert.equal(player.getPlaybackRate(), 1, 'invalid saved base rate falls back to 1x');
    }
    player._playRate = undefined;
    context.window.jmpInfo.settings.video.default_playback_speed = '1.25';
    assert.equal(player.getPlaybackRate(), 1.25);
  }
}

(async () => {
  await testPlayer('native/mpvVideoPlayer.js', 'mpvVideoPlayer');
  await testPlayer('native/mpvAudioPlayer.js', 'mpvAudioPlayer');
  console.log('video/audio playback rate, rejection, seek and millisecond bridge tests passed');
})().catch((error) => { console.error(error); process.exitCode = 1; });
