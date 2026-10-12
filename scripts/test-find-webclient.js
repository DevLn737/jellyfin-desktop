const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../native/find-webclient.js'), 'utf8');
const flush = async () => { for (let i = 0; i < 12; i++) await Promise.resolve(); };
function deferred() {
    let resolve;
    const promise = new Promise(done => { resolve = done; });
    return { promise, resolve };
}

function harness({ savedServer = '', init = Promise.resolve(), acknowledge = true, fetcher } = {}) {
    const elements = new Map();
    const calls = [];
    const writes = [];
    const navigation = [];
    const order = [];
    const timers = new Map();
    let timerId = 0;
    let rewritten = 0;
    const element = id => {
        if (!elements.has(id)) elements.set(id, {
            disabled: false, value: '', style: {}, listeners: {},
            addEventListener(event, callback) { this.listeners[event] = callback; }
        });
        return elements.get(id);
    };
    const setTimeout = (callback, delay) => {
        const id = ++timerId;
        timers.set(id, { callback, delay });
        return id;
    };
    class AbortController {
        constructor() {
            this.signal = { aborted: false, listeners: [],
                addEventListener(type, callback) { if (type === 'abort') this.listeners.push(callback); } };
        }
        abort() {
            if (this.signal.aborted) return;
            this.signal.aborted = true;
            for (const callback of this.signal.listeners) callback();
        }
    }
    const AbortSignal = { timeout(delay) {
        const controller = new AbortController();
        setTimeout(() => controller.abort(), delay);
        return controller.signal;
    } };
    const response = (body = { Id: 'synthetic-server', Version: '12.2.0' }, ok = true) => ({
        ok, json: async () => body,
        // A CSP header must not cause HTML rewriting in the native chooser.
        headers: { get: name => name.toLowerCase() === 'content-security-policy' ? "script-src 'self'" : null }
    });
    const location = { get href() { return navigation.at(-1) || 'file:///synthetic/find-webclient.html'; },
        set href(value) { order.push('navigate'); navigation.push(String(value)); },
        assign(value) { this.href = value; }, replace(value) { this.href = value; } };
    const window = {
        initCompleted: init,
        jmpInfo: { settings: { main: { userWebClient: savedServer } } },
        api: { settings: { setValue(section, key, value, callback) {
            order.push('persist');
            const write = { section, key, value, callback };
            writes.push(write);
            if (acknowledge) { order.push('ack'); callback(); }
        } } },
        history: { pushState() {}, replaceState() {} }
    };
    Object.defineProperty(window, 'location', { get: () => location, set: value => { location.href = value; } });
    const context = {
        window, document: { getElementById: element,
            open() { rewritten++; }, write() { rewritten++; }, close() { rewritten++; } },
        URL, AbortController, AbortSignal, setTimeout, clearTimeout: id => timers.delete(id),
        console: { log() {}, error() {}, warn() {} },
        fetch: async (url, options) => {
            calls.push({ url: String(url), options });
            // This reproduces the real regression: the HTML navigation target
            // has no CORS header although the public API can be fetched.
            if (!String(url).endsWith('/System/Info/Public')) throw new TypeError('Synthetic HTML CORS failure');
            return fetcher ? fetcher(url, options, response) : response();
        }
    };
    vm.createContext(context);
    vm.runInContext(source, context, { filename: 'find-webclient.js' });
    return { context, calls, writes, navigation, order, timers, element, response,
        get rewritten() { return rewritten; },
        connect: value => context.tryConnect(value),
        acknowledge() { order.push('ack'); writes.at(-1).callback(); },
        expire() {
            const pending = [...timers.values()];
            timers.clear();
            for (const { callback } of pending) callback();
        }
    };
}

(async () => {
    const success = harness();
    assert.equal(await success.connect('https://example.test/'), true,
        'Valid public discovery must succeed even when the HTML target disallows CORS');
    assert.deepEqual(success.calls.map(call => call.url), ['https://example.test/System/Info/Public']);
    assert.deepEqual(success.navigation, ['https://example.test']);
    assert.deepEqual(success.writes.map(({ section, key, value }) => ({ section, key, value })),
        [{ section: 'main', key: 'userWebClient', value: 'https://example.test' }]);
    assert.deepEqual(success.order, ['persist', 'ack', 'navigate']);
    assert.equal(success.rewritten, 0, 'Server HTML/CSP must not be rewritten into a file origin');
    assert.equal(success.calls[0].options.credentials === 'include', false);
    assert.equal(success.calls[0].options.method || 'GET', 'GET');

    const subpath = harness();
    assert.equal(await subpath.connect(' example.test/media/// '), true);
    assert.deepEqual(subpath.calls.map(call => call.url), ['http://example.test/media/System/Info/Public']);
    assert.deepEqual(subpath.navigation, ['http://example.test/media']);

    const initialized = deferred();
    const delayed = harness({ init: initialized.promise, acknowledge: false });
    const pending = delayed.connect('https://example.test/base/');
    await flush();
    assert.equal(delayed.writes.length, 0, 'Native settings must await initCompleted');
    assert.equal(delayed.navigation.length, 0);
    initialized.resolve();
    await flush();
    assert.equal(delayed.writes.length, 1);
    assert.equal(delayed.navigation.length, 0, 'A pending WebChannel write must precede navigation');
    delayed.acknowledge();
    assert.equal(await pending, true);
    assert.deepEqual(delayed.order, ['persist', 'ack', 'navigate']);

    for (const fetcher of [
        (_url, _options, response) => response({}, false),
        (_url, _options, response) => response({}),
        (_url, _options, response) => response({ Id: '' }),
        (_url, _options, response) => response({ Id: '   ' }),
        (_url, _options, response) => response({ Id: 123 }),
        () => ({ ok: true, json: async () => { throw new SyntaxError('Synthetic invalid JSON'); } }),
        () => { throw new TypeError('Synthetic network failure'); }
    ]) {
        const failed = harness({ fetcher });
        assert.equal(await failed.connect('https://example.test'), false);
        assert.equal(failed.element('connect-button').disabled, false, 'Failed discovery must allow retry');
        assert.equal(failed.writes.length, 0);
        assert.equal(failed.navigation.length, 0);
    }

    for (const input of ['', '   ', 'file:///server', 'javascript:alert(1)', 'data:text/plain,server',
        'ftp://example.test', 'https://user:password@example.test', 'https://example.test?ApiKey=synthetic',
        'https://example.test#synthetic']) {
        const invalid = harness();
        assert.equal(await invalid.connect(input), false, 'Only valid HTTP(S) server URLs may connect');
        assert.equal(invalid.calls.length, 0, 'Invalid input must not issue a request');
        assert.equal(invalid.writes.length, 0);
        assert.equal(invalid.navigation.length, 0);
        assert.equal(invalid.element('connect-button').disabled, false);
    }

    const timeout = harness({ fetcher: (_url, options) => new Promise((_resolve, reject) => {
        options.signal.addEventListener('abort', () => reject(new Error('Synthetic abort')));
    }) });
    const timed = timeout.connect('https://example.test');
    await flush();
    assert.equal(timeout.element('connect-button').disabled, true);
    assert.ok([...timeout.timers.values()].some(({ delay }) => delay > 0 && delay <= 30000),
        'Discovery must have a bounded abort timer');
    timeout.expire();
    assert.equal(await timed, false);
    assert.equal(timeout.element('connect-button').disabled, false);
    assert.equal(timeout.navigation.length, 0);

    const persistenceFailure = harness();
    persistenceFailure.context.window.api.settings.setValue = () => { throw new Error('Synthetic bridge failure'); };
    assert.equal(await persistenceFailure.connect('https://example.test'), false);
    assert.equal(persistenceFailure.navigation.length, 0, 'A failed settings write must not navigate');
    assert.equal(persistenceFailure.element('connect-button').disabled, false);

    let attempts = 0;
    const retry = harness({ fetcher: (_url, _options, response) => response({ Id: 'synthetic-server' }, ++attempts > 1) });
    assert.equal(await retry.connect('https://example.test'), false);
    assert.equal(retry.element('connect-button').disabled, false);
    assert.equal(await retry.connect('https://example.test'), true);
    assert.equal(retry.calls.length, 2);
    assert.equal(retry.writes.length, 1);
    assert.deepEqual(retry.navigation, ['https://example.test']);

    const startup = harness({ savedServer: 'https://example.test/base' });
    await flush();
    assert.equal(startup.element('address').value, 'https://example.test/base');
    assert.deepEqual(startup.calls.map(call => call.url), ['https://example.test/base/System/Info/Public']);
    assert.deepEqual(startup.order, ['persist', 'ack', 'navigate']);
    console.log('Native server discovery, CORS-independent navigation, persistence, failure and retry tests passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
