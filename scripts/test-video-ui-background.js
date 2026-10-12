const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

function fixture(fullscreen = true) {
    const frames = [], calls = [];
    let finishRoute;
    const route = new Promise(resolve => { finishRoute = resolve; });
    const context = { window: {
        _mpvPlaybackLifecycle: class {},
        requestAnimationFrame: callback => frames.push(callback),
        api: { player: { videoUiReady: () => calls.push('ready') } }
    } };
    vm.createContext(context);
    vm.runInContext(fs.readFileSync('native/mpvVideoPlayer.js', 'utf8'), context);
    const player = new context.window._mpvVideoPlayer({
        events: {}, loading: { hide() {} }, appRouter: { showVideoOsd: () => route },
        dashboard: { default: { setBackdropTransparency: () => calls.push('transparent') } }
    });
    player._currentPlayOptions = { fullscreen };
    player._videoDialog = { isConnected: true, style: { setProperty() {} } };
    player.getSavedVolume = () => 1;
    player.setVolume = () => {};
    player.getPlaybackRate = () => 1;
    player.setPlaybackRate = () => {};
    const frame = () => frames.splice(0).forEach(callback => callback());
    return { player, calls, frames, frame, finishRoute };
}

(async () => {
    const f = fixture();
    f.player.onNativeReady();
    assert.equal(f.frames.length, 0, 'wait for actual OSD navigation');
    f.finishRoute();
    await Promise.resolve();
    f.frame();
    assert.deepEqual(f.calls, [], 'do not reset before the transparent page paints');
    f.frame();
    assert.deepEqual(f.calls, ['ready']);
    f.player.onNativeReady();
    f.frame(); f.frame();
    assert.deepEqual(f.calls, ['ready'], 'playing events do not repeatedly reset the background');

    for (const when of ['during-route', 'between-frames']) {
        for (const change of ['stop', 'replace-file', 'destroy-dialog']) {
            const f = fixture();
            f.player.onNativeReady();
            if (when === 'between-frames') {
                f.finishRoute(); await Promise.resolve(); f.frame();
            }
            if (change === 'stop') f.player._currentPlayOptions = null;
            if (change === 'replace-file') f.player._currentPlayOptions = { fullscreen: true };
            if (change === 'destroy-dialog') f.player._videoDialog.isConnected = false;
            f.finishRoute(); await Promise.resolve(); f.frame(); f.frame();
            assert.deepEqual(f.calls, [], `${change} ${when} must discard the stale refresh`);
        }
    }
    const mini = fixture(false);
    mini.player.onNativeReady();
    assert.deepEqual(mini.calls, ['transparent']);
    mini.frame(); mini.frame();
    assert.deepEqual(mini.calls, ['transparent', 'ready'], 'miniplayer also refreshes after paint');
    console.log('Video UI paint ordering, one-shot refresh, miniplayer and stale navigation checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
