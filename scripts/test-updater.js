const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

function release(version, extra = {}) {
    const tag = `v${version}`;
    return { tag_name: tag, draft: false, prerelease: version.includes('-'),
        html_url: `https://github.com/DevLn737/jellyfin-desktop/releases/tag/${encodeURIComponent(tag)}`, ...extra };
}

(async () => {
    let notify;
    const opened = [];
    const prompts = [];
    const context = {
        navigator: { userAgent: 'JellyfinMediaPlayer 2.0.0-rc.1 (windows-x86_64)' },
        window: { jmpInfo: { appVersion: '2.0.0-rc.1' }, apiPromise: Promise.resolve({ system: {
            updateInfoEmitted: { connect(callback) { notify = callback; } },
            checkForUpdates() {}, openExternalUrl(url) { opened.push(url); }
        } }) },
        setTimeout(callback) { callback(); }
    };
    vm.createContext(context);
    vm.runInContext(fs.readFileSync('native/jmpUpdatePlugin.js', 'utf8'), context);
    const select = (data, version) => context.jmpSelectUpdate(data, version);
    assert.equal(select(release('2.0.0'), '2.0.0'), null);
    assert.equal(select(release('1.12.4'), '2.0.0'), null);
    assert.equal(select(release('2.0.1-rc.1'), '2.0.0'), null);
    assert.equal(select(release('2.0.1', { draft: true }), '2.0.0'), null);
    assert.equal(select(release('2.0.1', { prerelease: true }), '2.0.0'), null);
    assert.equal(select(release('2.0.1', { html_url: 'https://github.com/jellyfin/jellyfin-desktop/releases/tag/v2.0.1' }), '2.0.0'), null);
    assert.equal(select(release('2.0.1', { html_url: 'https://github.com/DevLn737/jellyfin-desktop/releases/download/v2.0.1/setup.exe' }), '2.0.0'), null);
    assert.equal(select(release('2.0.1', { html_url: 'https://github.com/DevLn737/jellyfin-desktop/releases/tag/v2.0.1?token=secret' }), '2.0.0'), null);
    assert.equal(select('SSL_UNAVAILABLE', '2.0.0'), null);
    assert.equal(select('{malformed', '2.0.0'), null);
    assert.equal(select(release('02.0.1'), '2.0.0'), null);
    assert.equal(select(release('2.0.1-rc.01'), '2.0.0'), null);
    assert.equal(select([release('2.0.0-rc.2'), release('2.0.0-rc.10')], '2.0.0-rc.1').version.value, '2.0.0-rc.10');
    assert.equal(select([release('2.0.0-rc.10'), release('2.0.0')], '2.0.0-rc.1').version.value, '2.0.0');
    assert.equal(select([release('2.0.2'), release('2.0.10')], '2.0.1').version.value, '2.0.10');
    assert.equal(select(release('2.0.0+rebuilt'), '2.0.0'), null);
    const plugin = new context.window._jmpUpdatePlugin({ confirm(options) { prompts.push(options); return Promise.resolve(); } });
    await plugin.ready;
    await notify(JSON.stringify(release('2.0.0-rc.2')));
    await notify(JSON.stringify(release('2.0.0-rc.2')));
    assert.equal(prompts.length, 1, 'duplicate signals must not repeat the prompt');
    assert.equal(prompts[0].confirmText, 'View release');
    assert.deepEqual(opened, ['https://github.com/DevLn737/jellyfin-desktop/releases/tag/v2.0.0-rc.2']);
    const cancelled = new context.window._jmpUpdatePlugin({ confirm() { return Promise.reject(new Error('cancelled')); } });
    await cancelled.ready;
    await notify(JSON.stringify(release('2.0.0')));
    assert.equal(opened.length, 1, 'cancel must not open a release');
    console.log('Fork SemVer and release-page updater tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
