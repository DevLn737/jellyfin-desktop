const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

(async () => {
    let notify;
    let prompted = false;
    let opened = false;
    const context = {
        navigator: { userAgent: 'JellyfinMediaPlayer 1.12.1-discord.1' },
        window: {
            apiPromise: Promise.resolve({ system: {
                updateInfoEmitted: { connect(callback) { notify = callback; } },
                checkForUpdates() {},
                openExternalUrl() { opened = true; }
            } })
        },
        setTimeout(callback) { callback(); }
    };
    vm.createContext(context);
    vm.runInContext(fs.readFileSync('native/jmpUpdatePlugin.js', 'utf8'), context);
    new context.window._jmpUpdatePlugin({ confirm() { prompted = true; return Promise.resolve(); } });
    await Promise.resolve();
    assert.equal(typeof notify, 'function');
    await notify('https://github.com/jellyfin/jellyfin-media-player/releases/tag/v1.12.0');
    assert.equal(prompted, false, 'a fork must not offer an upstream replacement');
    assert.equal(opened, false);
    context.navigator.userAgent = 'JellyfinMediaPlayer 1.11.0';
    await notify('https://github.com/jellyfin/jellyfin-media-player/releases/tag/v1.12.0');
    assert.equal(prompted, true, 'upstream builds retain their existing update behavior');
    assert.equal(opened, true);
    console.log('Discord fork updater tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
