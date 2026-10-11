// Read-only reproduction against immutable Web classes; import dependencies are
// minimal mocks. No Desktop adapter, application, network or profile is involved.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const fixtureRoot = path.resolve(__dirname, '../../tests/syncplay/web-reference');
const pins = JSON.parse(fs.readFileSync(path.join(fixtureRoot, 'source-pins.json'), 'utf8'));
const originalClearQueue = () => 'original queue cleared';
const playbackManager = { syncPlayEnabled: false, clearQueue: originalClearQueue };
for (const name of ['playPause', 'unpause', 'pause', 'seek', 'sendCommand', 'setPlaybackRate',
  'getPlaybackRate', 'play', 'setCurrentPlaylistItem', 'removeFromPlaylist', 'movePlaylistItem',
  'queue', 'queueNext', 'nextTrack', 'previousTrack', 'setRepeatMode', 'setQueueShuffleMode',
  'toggleQueueShuffleMode']) playbackManager[name] = () => {};
playbackManager._playQueueManager = {};
const context = vm.createContext({ playbackManager, Events: { trigger() {} },
  QueueManager: class QueueManager { constructor(manager) { this.manager = manager; } },
  toast() {}, globalize: { translate(value) { return value; } } });

function loadPinnedClass(file, className) {
  const bytes = fs.readFileSync(path.join(fixtureRoot, file));
  const hash = crypto.createHash('sha256').update(bytes).digest('hex');
  assert.equal(hash, pins.files[file].sha256, `${file}: source identity differs from handoff`);
  // Strip only imports/default export to substitute module import mocks; leave
  // both complete class implementations, including bind/unbind, unchanged.
  const source = bytes.toString('utf8').replace(/^import[^\n]*;\s*/gm, '')
    .replace(/^export default[^\n]*;\s*$/gm, '');
  vm.runInContext(source + `\nthis.${className} = ${className};`, context, { filename: file });
}
loadPinnedClass('GenericPlayer.js', 'GenericPlayer');
context.SyncPlay = { Players: { GenericPlayer: context.GenericPlayer } };
loadPinnedClass('NoActivePlayer.js', 'NoActivePlayer');
const manager = { getPlaybackCore: () => ({}), getQueueCore: () => ({}) };
const wrapper = new context.NoActivePlayer({}, manager);
assert.equal(playbackManager.clearQueue, originalClearQueue);
wrapper.bindToPlayer();
assert.equal(wrapper.bound, true);
assert.equal(playbackManager.syncPlayEnabled, true);
assert.equal(playbackManager._localClearQueue, originalClearQueue);
assert.equal(playbackManager.clearQueue, wrapper.clearQueueRequest);
wrapper.unbindFromPlayer();
assert.equal(wrapper.bound, false);
assert.equal(playbackManager.syncPlayEnabled, false);
assert.equal(playbackManager.clearQueue, undefined);
assert.throws(() => playbackManager.clearQueue(), TypeError);
console.log(JSON.stringify({ commit: pins.commit, beforeBind: 'function',
  duringBind: 'group override; original saved on playbackManager',
  afterUnbind: typeof playbackManager.clearQueue, callingAfterLeave: 'TypeError',
  reproduced: true }));
