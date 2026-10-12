const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

function eventsFixture() {
  const targets = new Map();
  const listeners = (target, name) => targets.get(target)?.get(name) || new Set();
  return {
    on(target, name, handler) {
      if (!targets.has(target)) targets.set(target, new Map());
      if (!targets.get(target).has(name)) targets.get(target).set(name, new Set());
      targets.get(target).get(name).add(handler);
    },
    off(target, name, handler) { listeners(target, name).delete(handler); },
    emit(target, name, ...args) { for (const handler of [...listeners(target, name)]) handler({}, ...args); },
    count(target, name) { return listeners(target, name).size; }
  };
}

async function fixture(mode = 'subscribe', diagnosticsAvailable = true) {
  const events = eventsFixture();
  const records = [];
  const captureHandlers = new Set();
  const pageHandlers = new Map();
  const diagnostics = {
    capturing: false,
    captureChanged: { connect(fn) { captureHandlers.add(fn); }, disconnect(fn) { captureHandlers.delete(fn); } },
    recordSyncPlayEvent(event, metrics) { records.push(JSON.parse(JSON.stringify({ event, metrics }))); }
  };
  function client(kind = mode) {
    const handlers = new Map();
    const result = { subscribeCount: 0, unsubscribeCount: 0 };
    if (kind !== 'message') {
      result.subscribe = (types, handler) => {
        ++result.subscribeCount;
        if (kind === 'partial-failure' && result.subscribeCount === 2) throw new Error('Fixture failure');
        assert.equal(types.length, 1);
        handlers.set(types[0], handler);
        return () => { ++result.unsubscribeCount; handlers.delete(types[0]); };
      };
    }
    result.emit = (type, data) => kind === 'message'
      ? events.emit(result, 'message', { MessageType: type, Data: data })
      : handlers.get(type)?.({ Data: data });
    result.handler = type => handlers.get(type);
    result.listenerCount = () => kind === 'message' ? events.count(result, 'message') : handlers.size;
    return result;
  }
  let active = client();
  const connections = { currentApiClient() { return active; } };
  const context = {
    window: {
      apiPromise: Promise.resolve({ diagnostics: diagnosticsAvailable ? diagnostics : undefined }),
      addEventListener(name, handler) { pageHandlers.set(name, handler); },
      removeEventListener(name, handler) { if (pageHandlers.get(name) === handler) pageHandlers.delete(name); }
    }
  };
  vm.createContext(context);
  vm.runInContext(fs.readFileSync('native/jmpDiagnosticsPlugin.js', 'utf8'), context);
  const plugin = new context.window._jmpDiagnosticsPlugin({ events, ServerConnections: connections });
  await plugin.ready;
  return {
    plugin, records, events, connections, diagnostics, client,
    get active() { return active; },
    setActive(value) { active = value; },
    capture(value) {
      diagnostics.capturing = value;
      for (const handler of [...captureHandlers]) handler(value);
    },
    pageHide() { pageHandlers.get('pagehide')?.(); },
    captureListenerCount() { return captureHandlers.size; }
  };
}

async function testTransport(mode) {
  const f = await fixture(mode);
  assert.equal(f.active.listenerCount(), 0, 'capture off creates no message subscription');
  f.capture(true);
  assert.equal(f.active.listenerCount(), mode === 'subscribe' ? 2 : 1);
  const data = {
    Command: 'Seek', PositionTicks: 123456789, PlaybackRate: 1.5,
    When: '2026-10-11T12:34:56.1234567Z',
    GroupId: 'SECRET_GROUP', UserId: 'SECRET_USER', SessionId: 'SECRET_SESSION',
    Name: 'SECRET_NAME', Url: 'https://SECRET_HOST/stream?ApiKey=SECRET_TOKEN',
    Data: { PositionTicks: 99, Auth: 'SECRET_NESTED' }
  };
  const original = JSON.stringify(data);
  f.active.emit('SyncPlayCommand', data);
  assert.equal(JSON.stringify(data), original, 'observer must not mutate a shared SyncPlay command');
  assert.deepEqual(f.records.at(-1), {
    event: 'seek', metrics: { positionMs: 12345.6789, playbackRate: 1.5,
      whenUtcMs: Date.parse(data.When), status: 3 }
  });
  assert.ok(!JSON.stringify(f.records).includes('SECRET'), 'only numeric metrics leave the plugin');
  const contractFixture = JSON.parse(fs.readFileSync('contracts/fixtures/syncplay-source-derived.json', 'utf8'));
  assert.equal(contractFixture.provenance, 'source-derived-synthetic');
  assert.equal(contractFixture.capturedFromLiveServer, false);
  for (const entry of contractFixture.messages) {
    const count = f.records.length;
    f.active.emit(entry.payload.MessageType, entry.payload.Data);
    if (entry.expected === null) assert.equal(f.records.length, count, entry.name);
    else assert.deepEqual(f.records.at(-1), entry.expected, entry.name);
  }

  const when = new Date('2026-10-11T12:00:00Z');
  f.active.emit('SyncPlayCommand', { Command: 0, When: when, PlaybackRate: 0.225 });
  assert.deepEqual(f.records.at(-1), { event: 'resume',
    metrics: { playbackRate: 0.225, whenUtcMs: when.getTime(), status: 0 } });
  f.active.emit('SyncPlayCommand', { Command: 'Pause', PositionTicks: NaN,
    PlaybackRate: 'SECRET_TOKEN', When: 'SECRET_TOKEN', driftMs: 50 });
  assert.deepEqual(f.records.at(-1), { event: 'pause', metrics: { status: 1 } });
  f.active.emit('SyncPlayCommand', { Command: 'Stop', PositionTicks: Number.MAX_SAFE_INTEGER + 1,
    PlaybackRate: Infinity, When: new Date(NaN) });
  assert.deepEqual(f.records.at(-1), { event: 'command', metrics: { status: 2 } });
  const before = f.records.length;
  for (const data of [null, {}, { Command: 'SECRET_COMMAND' }, { Command: 1.5 }]) {
    f.active.emit('SyncPlayCommand', data);
  }
  f.active.emit('SECRET_MESSAGE', { Command: 'Pause' });
  assert.equal(f.records.length, before, 'unrecognized messages/enums are discarded');
  f.active.emit('SyncPlayGroupUpdate', { Type: 'GroupJoined', GroupId: 'SECRET_GROUP', Data: data });
  assert.deepEqual(f.records.at(-1), { event: 'join', metrics: {} });
  f.active.emit('SyncPlayGroupUpdate', { Type: 'GroupLeft', Data: { UserId: 'SECRET_USER' } });
  assert.deepEqual(f.records.at(-1), { event: 'leave', metrics: {} });
  f.active.emit('SyncPlayGroupUpdate', { Type: 'StateUpdate', Data: {
    State: 'Waiting', Reason: 'Buffer', GroupId: 'SECRET_GROUP', UserId: 'SECRET_USER' } });
  assert.deepEqual(f.records.at(-1), { event: 'buffering', metrics: { status: 1 } });
  f.active.emit('SyncPlayGroupUpdate', { Type: 'StateUpdate', Data: { State: 2, Reason: 9 } });
  assert.deepEqual(f.records.at(-1), { event: 'buffering', metrics: { status: 2 } });
  const bufferedCount = f.records.length;
  f.active.emit('SyncPlayGroupUpdate', { Type: 'StateUpdate', Data: { State: 'Waiting', Reason: 'Seek' } });
  f.active.emit('SyncPlayGroupUpdate', { Type: 'StateUpdate', Data: { State: 'Waiting', Reason: 8 } });
  assert.equal(f.records.length, bufferedCount, 'waiting caused by seek is not reported as buffering');

  const previousClient = f.active;
  const stale = mode === 'subscribe' ? previousClient.handler('SyncPlayCommand') : null;
  f.capture(false);
  assert.equal(previousClient.listenerCount(), 0);
  const stoppedCount = f.records.length;
  stale?.({ Data: { Command: 'Seek', PositionTicks: 1 } });
  previousClient.emit('SyncPlayCommand', { Command: 'Pause' });
  assert.equal(f.records.length, stoppedCount, 'queued callbacks after detach are ignored');
  f.capture(true);
  const secondClient = f.client(mode);
  f.setActive(secondClient);
  f.events.emit(f.connections, 'apiclientcreated', secondClient);
  await Promise.resolve();
  assert.equal(previousClient.listenerCount(), 0, 'server switch removes old listeners');
  assert.equal(secondClient.listenerCount(), mode === 'subscribe' ? 2 : 1);
  const switchedCount = f.records.length;
  stale?.({ Data: { Command: 'Seek', PositionTicks: 1 } });
  assert.equal(f.records.length, switchedCount);

  f.events.emit(f.connections, 'localusersignedout');
  assert.equal(secondClient.listenerCount(), 0, 'signout removes message listeners');
  f.capture(false);
  f.capture(true);
  assert.equal(secondClient.listenerCount(), 0, 'capture restart cannot attach a signed out client');
  f.events.emit(f.connections, 'localusersignedin');
  assert.equal(secondClient.listenerCount(), mode === 'subscribe' ? 2 : 1);
  f.pageHide();
  assert.equal(secondClient.listenerCount(), 0);
  assert.equal(f.captureListenerCount(), 0);
  for (const event of ['apiclientcreated', 'localusersignedin', 'localusersignedout']) {
    assert.equal(f.events.count(f.connections, event), 0, 'page teardown removes lifecycle listeners');
  }
  f.capture(false);
  f.capture(true);
  assert.equal(secondClient.listenerCount(), 0, 'disposed plugin remains detached');
}

(async () => {
  await testTransport('subscribe');
  await testTransport('message');
  const partial = await fixture('partial-failure');
  partial.capture(true);
  assert.equal(partial.active.listenerCount(), 0, 'failed second subscription rolls back the first');
  assert.equal(partial.active.unsubscribeCount, 1);
  partial.capture(false);
  partial.capture(true);
  assert.equal(partial.active.subscribeCount, 2, 'unknown adapter is not repeatedly registered');
  partial.plugin.dispose();
  const absent = await fixture('subscribe', false);
  absent.capture(true);
  assert.equal(absent.active.listenerCount(), 0, 'missing native diagnostics is supported');
  absent.plugin.dispose();
  console.log('Diagnostics plugin checks passed (subscription and legacy message transports).');
})().catch(error => { console.error(error); process.exitCode = 1; });
