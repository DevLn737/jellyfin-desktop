const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

function fixture(kind = 'Audio', confirm = () => Promise.resolve()) {
    const timers = new Map(), calls = [];
    let timerId = 0;
    const signal = { connect() {}, disconnect() {} };
    const native = {
        playing: signal, positionUpdate: signal, finished: signal,
        updateDuration: signal, error: signal, paused: signal,
        playbackReady: signal, seekStarted: signal,
        load(url, _options, _metadata, _audio, _subtitle, accepted) { calls.push(['load', url]); accepted(true); },
        setVolume(value) { calls.push(['volume', value]); },
        pause() { calls.push(['pause']); }, stop() { calls.push(['stop']); },
        setSubtitleDelay() {}
    };
    const settings = { volume: 1, get() { return this.volume; }, set(_, value) { this.volume = value; } };
    const context = { console: { debug() {}, error() {} },
        setTimeout(fn) { timers.set(++timerId, fn); return timerId; }, clearTimeout(id) { timers.delete(id); },
        window: { _mpvPlaybackLifecycle: class {
            cancel() {} dispose() {} pause() {}
            load(url) { calls.push(['load', url]); return Promise.resolve(); }
        }, api: { player: native, power: { setScreensaverEnabled() {} } } }
    };
    const classList = { add() {}, remove() {} };
    let mediaElement;
    context.document = {
        querySelector() { return mediaElement; },
        createElement() { return { classList, style: {} }; },
        body: { classList, insertBefore(element) { mediaElement = element; } }
    };
    context.window.setTimeout = context.setTimeout;
    context.window.clearTimeout = context.clearTimeout;
    vm.createContext(context);
    vm.runInContext(fs.readFileSync(`native/mpv${kind}Player.js`, 'utf8'), context);
    const player = new context.window[`_mpv${kind}Player`]({
        events: { trigger(_, name) { calls.push(['event', name]); } },
        appSettings: settings, appHost: {}, loading: { show() {}, hide() {} }, confirm,
        dashboard: { default: { setBackdropTransparency() {} } }
    });
    const drain = async () => {
        for (let pass = 0; pass < 30; pass++) {
            for (const [id, fn] of [...timers]) { timers.delete(id); fn(); }
            await Promise.resolve();
        }
    };
    return { player, settings, calls, timers, drain, context };
}

(async () => {
    for (const kind of ['Audio', 'Video']) {
        const f = fixture(kind);
        f.player.setVolume(0);
        assert.equal(f.settings.volume, 0, `${kind}: zero volume must persist`);
        assert.equal(f.player.getSavedVolume(), 0, `${kind}: zero volume must reload`);
        f.settings.volume = undefined;
        assert.equal(f.player.getSavedVolume(), 1, `${kind}: absent volume keeps default`);
    }
    for (const replace of [false, true]) {
        let finishConfirm;
        const f = fixture('Video', () => new Promise(resolve => { finishConfirm = resolve; }));
        const errorPending = f.player.onError('synthetic decoder failure');
        assert.equal(typeof finishConfirm, 'function', 'error handling must await retry decision');
        if (replace) await f.player.play({ url: 'replacement', mediaSource: { MediaStreams: [] } });
        finishConfirm();
        await errorPending;
        assert.equal(f.calls.filter(call => call[0] === 'event' && call[1] === 'error').length, replace ? 0 : 1,
            'retry decision emits an error only for the playback generation that opened it');
        if (replace) assert.equal(f.player.currentSrc(), 'replacement');
    }
    {
        const f = fixture();
        f.player._currentSrc = 'old';
        let firstSettled = false, secondSettled = false;
        f.player.stop().then(() => { firstSettled = true; });
        f.player.stop().then(() => { secondSettled = true; });
        await f.drain();
        assert.ok(firstSettled && secondSettled, 'both overlapping stops must settle');
        assert.equal(f.calls.filter(call => call[0] === 'pause').length, 1);
        assert.equal(f.calls.filter(call => call[1] === 'stopped').length, 1);
    }
    {
        const f = fixture();
        f.player._currentSrc = 'old';
        const stopped = f.player.stop();
        await f.player.play({ url: 'new' });
        const replacementStart = f.calls.length;
        await f.drain(); await stopped;
        assert.equal(f.player.currentSrc(), 'new', 'old fade must not clear replacement');
        assert.deepEqual(f.calls.slice(replacementStart), [], 'old fade must not adjust or pause replacement');
        assert.equal(f.timers.size, 0);
    }
    {
        const f = fixture();
        f.player._currentSrc = 'old';
        const stopped = f.player.stop();
        f.player.destroy();
        const afterDestroy = f.calls.length;
        await f.drain(); await stopped;
        assert.deepEqual(f.calls.slice(afterDestroy), [], 'destroy cancels fade continuation');
        assert.equal(f.timers.size, 0);
    }
    {
        const f = fixture('Video');
        vm.runInContext(fs.readFileSync('native/mpvPlaybackLifecycle.js', 'utf8'), f.context);
        f.player._lifecycle = new f.context.window._mpvPlaybackLifecycle(f.player);
        const options = url => ({ url, mediaSource: { MediaStreams: [] } });
        const playing = f.player.play(options('cancelled'));
        const pendingReady = f.player._lifecycle._request.promise;
        assert.equal(f.player.currentSrc(), 'cancelled', 'video load must dispatch before play returns');
        assert.deepEqual(f.calls.filter(call => call[0] === 'load'), [['load', 'cancelled']]);
        f.player.stop();
        await playing;
        await assert.rejects(pendingReady, /Native playback was stopped\./);
        await Promise.resolve();
        assert.equal(f.player.currentSrc(), null, 'immediate stop must not be followed by delayed source restoration');
        assert.equal(f.player._lifecycle._request, null);
        const older = f.player.play(options('older'));
        const olderReady = f.player._lifecycle._request.promise;
        let webStream, webFailures = 0;
        older.then(() => { webStream = 'older'; }, () => { ++webFailures; f.player.stop(); });
        const newer = f.player.play(options('newer'));
        newer.then(() => { webStream = 'newer'; }, () => { ++webFailures; f.player.stop(); });
        await older; await newer;
        await assert.rejects(olderReady, /Native playback was replaced\./);
        assert.equal(webStream, 'newer', 'same-turn Web dispatch completions retain the latest stream');
        assert.equal(webFailures, 0, 'internal cancellation must never enter Web public play failure handlers');
        assert.equal(f.player.currentSrc(), 'newer');
        assert.equal(f.player._lifecycle._request.kind, 'load');
        const newerReady = f.player._lifecycle._request.promise;
        f.player.stop();
        await assert.rejects(newerReady, /Native playback was stopped\./);
        assert.deepEqual(f.calls.filter(call => call[0] === 'load'), [['load', 'cancelled'], ['load', 'older'], ['load', 'newer']]);
        assert.equal(f.timers.size, 0, 'stops clear native readiness deadlines');
    }
    for (const kind of ['Audio', 'Video']) {
        const f = fixture(kind);
        f.player._currentSrc = 'old';
        let replacement;
        f.player.events.trigger = (_, event, data) => {
            if (event !== 'stopped') return;
            assert.equal(data[0].src, 'old', `${kind}: stop notification retains old source`);
            assert.equal(f.player.currentSrc(), null, `${kind}: clear old state before notifying subscribers`);
            replacement = f.player.play({ url: 'replacement', mediaSource: { MediaStreams: [] } });
        };
        const stopped = f.player.stop(true);
        await f.drain(); await stopped; await replacement;
        assert.equal(f.player.currentSrc(), 'replacement', `${kind}: stopped subscriber may start next item`);
        assert.equal(f.player._currentPlayOptions.url, 'replacement');
        assert.equal(f.calls.filter(call => call[0] === 'stop').length, kind === 'Video' ? 1 : 0,
            `${kind}: old stop(true) must not destroy replacement media`);
    }
    console.log('Player cancellation and volume persistence checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
