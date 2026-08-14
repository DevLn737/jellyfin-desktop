const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const source = fs.readFileSync('native/mpvVideoPlayer.js', 'utf8');
const context = {
  URL,
  console: { debug() {}, error() {}, log() {} },
  window: { api: { player: {}, power: {} } }
};

vm.createContext(context);
vm.runInContext(`${source}\nthis.MpvVideoPlayer = mpvVideoPlayer;`, context);

const player = new context.MpvVideoPlayer({
  events: { trigger() {} },
  loading: { show() {}, hide() {} },
  appRouter: {},
  globalize: {},
  appHost: {},
  appSettings: { get() {} },
  confirm() {},
  dashboard: { default: { setBackdropTransparency() {} } }
});

player._currentPlayOptions = {
  url: 'https://example.test/jellyfin/Videos/item-1/stream.mkv?Static=true&MediaSourceId=source-1&Tag=etag&ApiKey=secret',
  item: { Id: 'item-1' },
  mediaSource: {
    Id: 'source-1',
    MediaStreams: [
      { Type: 'Audio', Index: 1, Codec: 'AC3', IsExternal: true },
      { Type: 'Audio', Index: 3, Codec: 'flac', IsExternal: false },
      {
        Type: 'Subtitle',
        Index: 0,
        IsExternal: true,
        DeliveryMethod: 'External',
        DeliveryUrl: 'https://example.test/jellyfin/Videos/item-1/Subtitles/0/Stream.ass?ApiKey=secret'
      }
    ]
  }
};

player._audioTrackIndexToSetOnPlaying = 1;
const externalParam = player.getAudioParam();
assert.ok(externalParam.startsWith('#,https://example.test/jellyfin/Audio/item-1/stream.ac3?'));

const externalUrl = new URL(externalParam.slice(2));
assert.equal(externalUrl.searchParams.get('Static'), null);
assert.equal(externalUrl.searchParams.get('Tag'), null);
assert.equal(externalUrl.searchParams.get('ApiKey'), 'secret');
assert.equal(externalUrl.searchParams.get('MediaSourceId'), 'source-1');
assert.equal(externalUrl.searchParams.get('AudioStreamIndex'), '1');
assert.equal(externalUrl.searchParams.get('AudioCodec'), 'ac3');
assert.equal(externalUrl.searchParams.get('TranscodingProtocol'), 'http');
assert.equal(externalUrl.searchParams.get('EnableRedirection'), 'false');

player._audioTrackIndexToSetOnPlaying = 3;
assert.equal(player.getAudioParam(), '#1');

player._currentPlayOptions.mediaSource.MediaStreams[0].DeliveryUrl = 'https://cdn.example.test/russian.ac3';
player._audioTrackIndexToSetOnPlaying = 1;
assert.equal(player.getAudioParam(), '#,https://cdn.example.test/russian.ac3');

player._currentPlayOptions.mediaSource.MediaStreams[0].DeliveryUrl = null;
player._currentPlayOptions.url = 'not-a-url';
assert.equal(player.getAudioParam(), '#1');

player._subtitleTrackIndexToSetOnPlaying = 0;
assert.equal(
  player.getSubtitleParam(),
  '#,https://example.test/jellyfin/Videos/item-1/Subtitles/0/Stream.ass?ApiKey=secret'
);

console.log('external audio and subtitle mapping tests passed');
