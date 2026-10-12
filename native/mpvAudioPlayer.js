let fadeTimeout;
function fade(instance, elem, startingVolume) {
    instance._isFadingOut = true;

    // Need to record the starting volume on each pass rather than querying elem.volume
    // This is due to iOS safari not allowing volume changes and always returning the system volume value
    const newVolume = Math.max(0, startingVolume - 15);
    console.debug('fading volume to ' + newVolume);
    api.player.setVolume(newVolume);

    if (newVolume <= 0) {
        instance._isFadingOut = false;
        return Promise.resolve();
    }

    return new Promise(function (resolve, reject) {
        cancelFadeTimeout();
        fadeTimeout = setTimeout(function () {
            fade(instance, null, newVolume).then(resolve, reject);
        }, 100);
    });
}

function cancelFadeTimeout() {
    const timeout = fadeTimeout;
    if (timeout) {
        clearTimeout(timeout);
        fadeTimeout = null;
    }
}

class mpvAudioPlayer {
    constructor({ events, appHost, appSettings, toast, playbackManager }) {
        const self = this;

        self.events = events;
        self.appHost = appHost;
        self.appSettings = appSettings;
        self._toast = toast;
        self._playbackManager = playbackManager;
        self._lifecycle = new window._mpvPlaybackLifecycle(self);

        self.name = 'MPV Audio Player';
        self.type = 'mediaplayer';
        self.id = 'mpvaudioplayer';
        self.syncPlayWrapAs = 'htmlaudioplayer';
        self.useServerPlaybackInfoForAudio = true;

        self._duration = undefined;
        self._currentTime = undefined;
        self._paused = false;
        self._volume = self.getSavedVolume() * 100;
        self._playRate = 1;
        self._hasConnection = false;

        self.play = (options) => {
            self._started = false;
            self._timeUpdated = false;
            self._currentTime = null;
            self._duration = undefined;

            const player = window.api.player;
            if (!self._hasConnection) {
                self._hasConnection = true;
                player.playing.connect(onPlaying);
                player.positionUpdate.connect(onTimeUpdate);
                player.finished.connect(onEnded);
                player.updateDuration.connect(onDuration);
                player.error.connect(onError);
                player.paused.connect(onPause);
            }

            return setCurrentSrc(options);
        };

        function setCurrentSrc(options) {
            {
                const val = options.url;
                self._currentSrc = val;
                console.debug('playing url: ' + val);

                self._currentPlayOptions = { ...options };

                return self._lifecycle.load(val, options,
                    {type: 'music', headers: {'User-Agent': 'JellyfinMediaPlayer'}, metadata: options.item, media: {}},
                    '#1',
                    '');
            }
        }

        self.onEndedInternal = () => {
            self._lifecycle.cancel();
            const stopInfo = {
                src: self._currentSrc
            };

            self.events.trigger(self, 'stopped', [stopInfo]);

            self._currentTime = null;
            self._currentSrc = null;
            self._currentPlayOptions = null;
        };

        self.stop = (destroyPlayer) => {
            self._lifecycle.cancel();
            cancelFadeTimeout();

            const src = self._currentSrc;

            if (src) {
                const originalVolume = self._volume;

                return fade(self, null, self._volume).then(function () {
                    self.pause();
                    self.setVolume(originalVolume, false);

                    self.onEndedInternal();

                    if (destroyPlayer) {
                        self.destroy();
                    }
                });
            }
            return Promise.resolve();
        };

        self.destroy = () => {
            window.api.player.stop();

            const player = window.api.player;
            self._hasConnection = false;
            player.playing.disconnect(onPlaying);
            player.positionUpdate.disconnect(onTimeUpdate);
            player.finished.disconnect(onEnded);
            self._duration = undefined;
            player.updateDuration.disconnect(onDuration);
            player.error.disconnect(onError);
            player.paused.disconnect(onPause);
            self._lifecycle.dispose();
        };

        function onDuration(duration) {
            self._duration = duration;
        }

        function onEnded() {
            self.onEndedInternal();
        }

        function onTimeUpdate(time) {
            // Don't trigger events after user stop
            if (!self._isFadingOut) {
                self._currentTime = time;
                self.events.trigger(self, 'timeupdate');
            }
        }

        self.onNativeReady = () => {
            if (!self._started) {
                self._started = true;
                const volume = self.getSavedVolume() * 100;
                self.setVolume(volume, volume != self._volume);
                // The SyncPlay core applies its authoritative base separately.
                self.setPlaybackRate(self.getPlaybackRate());
                self.setMute(false, false);
            }
        };

        function onPlaying() {
            self.onNativeReady();

            if (self._paused) {
                self._paused = false;
                self.events.trigger(self, 'unpause');
            }

            self._lifecycle.playing();
        }

        function onPause() {
            self._paused = true;
            self._lifecycle.paused();
            self.events.trigger(self, 'pause');
        }

        function onError(error) {
            self._lifecycle.cancel('Native playback failed.');
            console.error(`media error: ${error}`);
            toast(`media error: ${error}`);

            self.events.trigger(self, 'error', [
                {
                    type: 'mediadecodeerror'
                }
            ]);
        }
    }

    getSavedVolume() {
        return this.appSettings.get('volume') || 1;
    }

    currentSrc() {
        return this._currentSrc;
    }

    canPlayMediaType(mediaType) {
        return (mediaType || '').toLowerCase() === 'audio';
    }

    getDeviceProfile(item, options) {
        if (this.appHost.getDeviceProfile) {
            return this.appHost.getDeviceProfile(item, options);
        }

        return {};
    }

    currentTime(val) {
        if (val != null) {
            const position = typeof val === 'number' || (typeof val === 'string' && val.trim())
                ? Number(val) : NaN;
            if (Number.isFinite(position) && position >= 0 && position <= Number.MAX_SAFE_INTEGER) {
                this._lifecycle.seek(position);
            }
            return;
        }

        return this._currentTime;
    }

    currentTimeAsync() {
        return this._lifecycle.position();
    }

    duration() {
        if (this._duration) {
            return this._duration;
        }

        return null;
    }

    seekable() {
        return Boolean(this._duration);
    }

    getBufferedRanges() {
        return [];
    }

    pause() {
        this._lifecycle.pause();
        window.api.player.pause();
    }

    // This is a retry after error
    resume() {
        this._paused = false;
        window.api.player.play();
    }

    unpause() {
        window.api.player.play();
    }

    paused() {
        return this._paused;
    }

    setPlaybackRate(value) {
        const perMille = mpvAudioPlayer.normalizePlaybackRate(value);
        if (perMille === null) {
            this.notifyPlaybackRateFailure();
            return false;
        }
        if (perMille !== 1000 && !this.supportsSyncPlayPlaybackRate()) {
            this.notifyPlaybackRateFailure('This SyncPlay group does not support changing playback rate.');
            return false;
        }
        const previousRate = this._playRate;
        this._playRate = perMille / 1000;
        const generation = this._rateRequestGeneration = (this._rateRequestGeneration || 0) + 1;
        const settleRate = this._lifecycle.rateRequest();
        window.api.player.setPlaybackRate(perMille, (accepted) => {
            if (accepted === true && generation >= (this._acceptedRateGeneration || 0)) {
                this._acceptedRateGeneration = generation;
                this._acceptedPlayRate = perMille / 1000;
                if (this._rejectedRateGeneration === this._rateRequestGeneration) this._playRate = this._acceptedPlayRate;
            } else if (accepted === false && generation === this._rateRequestGeneration) {
                this._rejectedRateGeneration = generation;
                this._playRate = this._acceptedPlayRate ?? previousRate;
                console.warn('Native player rejected playback rate');
                this.notifyPlaybackRateFailure();
            }
            settleRate(accepted);
        });
        return true;
    }

    supportsSyncPlayPlaybackRate() {
        const manager = this._playbackManager;
        // The deployed group-rate wrapper saves the local setter under this
        // marker. Stock Web leaves its local setter exposed during SyncPlay.
        return !manager?.syncPlayEnabled || typeof manager._localSetPlaybackRate === 'function';
    }

    notifyPlaybackRateFailure(message = 'Requested playback rate is not supported by the native player.') {
        const now = Date.now();
        if (this._lastRateFailureAt !== undefined && now - this._lastRateFailureAt < 5000) return;
        this._lastRateFailureAt = now;
        try { this._toast?.(message); } catch (_) { }
    }

    getPlaybackRate() {
        return this._playRate;
    }

    static normalizePlaybackRate(value, minimum = 0.2) {
        if (typeof value !== 'number' && typeof value !== 'string') {
            return null;
        }
        if (typeof value === 'string' && !value.trim()) {
            return null;
        }
        const rate = Number(value);
        if (!Number.isFinite(rate) || rate < minimum || rate > 5) {
            return null;
        }
        return Math.round(rate * 1000);
    }

    getSupportedPlaybackRates() {
        if (!this.supportsSyncPlayPlaybackRate()) return [{ name: '1x', id: 1 }];
        return [0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5]
            .map((rate) => ({ name: rate + 'x', id: rate }));
    }

    saveVolume(value) {
        if (value) {
            this.appSettings.set('volume', value);
        }
    }

    setVolume(val, save = true) {
        this._volume = val;
        if (save) {
            this.saveVolume((val || 100) / 100);
            this.events.trigger(this, 'volumechange');
        }
        window.api.player.setVolume(val);
    }

    getVolume() {
        return this._volume;
    }

    volumeUp() {
        this.setVolume(Math.min(this.getVolume() + 2, 100));
    }

    volumeDown() {
        this.setVolume(Math.max(this.getVolume() - 2, 0));
    }

    setMute(mute, triggerEvent = true) {
        this._muted = mute;
        window.api.player.setMuted(mute);
        if (triggerEvent) {
            this.events.trigger(this, 'volumechange');
        }
    }

    isMuted() {
        return this._muted;
    }

    supports(feature) {
        if (feature === 'PlaybackRate' && !this.supportsSyncPlayPlaybackRate()) return false;
        if (!supportedFeatures) {
            supportedFeatures = getSupportedFeatures();
        }

        return supportedFeatures.indexOf(feature) !== -1;
    }
}

let supportedFeatures;

function getSupportedFeatures() {
    return ['PlaybackRate'];
}

window._mpvAudioPlayer = mpvAudioPlayer;
