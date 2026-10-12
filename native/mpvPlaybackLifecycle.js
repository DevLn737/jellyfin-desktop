// Native readiness is separate from accepting a load/seek command. This helper
// shares the existing PlayerComponent signals; it never opens another player.
class mpvPlaybackLifecycle {
    constructor(owner) {
        this.owner = owner;
        this.offsetMs = 0;
        this.ready = false;
        this.webStarted = false;
        this._serial = 0;
        this._signals = [];
        this._rateWait = Promise.resolve();
        this._eventsBound = true;
        this._onWebStart = () => {
            this.webStarted = true;
            this.adaptSyncPlayWrapper();
            Promise.resolve().then(() => this._notifyReady());
        };
        owner.events.on?.(owner, 'playbackstart', this._onWebStart);
        this._onPageHide = () => this.dispose();
        window.addEventListener?.('pagehide', this._onPageHide);
    }

    connect() {
        if (this._signals.length) return;
        if (!this._eventsBound) {
            this.owner.events.on?.(this.owner, 'playbackstart', this._onWebStart);
            window.addEventListener?.('pagehide', this._onPageHide);
            this._eventsBound = true;
        }
        const native = window.api.player;
        if (!native.playbackReady?.connect || !native.seekStarted?.connect) {
            throw new Error('Native player readiness bridge is unavailable.');
        }
        this._onReady = (id, positionMs, paused) => {
            const request = this._request;
            if (!Number.isFinite(positionMs) || positionMs < 0) return;
            if (!request) {
                if (Number(id) !== this._activeId || this._failure) return;
                this.ready = true;
                this.owner._currentTime = positionMs;
                this.owner._paused = paused === true;
                if (this.owner._paused) this.paused();
                this._notifyReady();
                return;
            }
            if (Number(id) !== request.id) return;
            // An old restart may already be queued when a new seek is issued.
            // Its fresh sample cannot satisfy the new position barrier.
            if (request.kind === 'seek' && request.expectedPaused && Math.abs(positionMs - request.targetMs) > 500) return;
            request.sample = { positionMs, paused: paused === true };
            this._completeReady();
        };
        this._onSeeking = id => {
            if (Number(id) === this._request?.id) this.owner.events.trigger(this.owner, 'waiting');
        };
        for (const [signal, handler] of [[native.playbackReady, this._onReady],
            [native.seekStarted, this._onSeeking], [native.buffering, () => {
                this.ready = false;
                this.owner.events.trigger(this.owner, 'waiting');
            }]]) {
            if (signal?.connect) {
                signal.connect(handler);
                this._signals.push([signal, handler]);
            }
        }
    }

    _begin(kind) {
        this.cancel('Native playback was replaced.');
        this.connect();
        this.ready = false;
        this._failure = null;
        const id = window._jmpNativeRequestId = (window._jmpNativeRequestId || 0) + 1;
        if (!Number.isSafeInteger(id)) throw new Error('Native request counter exhausted.');
        const request = { id, kind, acknowledged: false };
        request.promise = new Promise((resolve, reject) => Object.assign(request, { resolve, reject }));
        // A seek has no Promise consumer in the Web API. Keep its failure visible
        // without an unhandled rejection; samples still reject until recovered.
        request.promise.catch(() => {});
        request.timeout = window.setTimeout(() => this._fail(request,
            new Error('Native playback did not become ready within 30 seconds.')), 30000);
        this._request = request;
        return request;
    }

    load(url, options, metadata, audio, subtitle) {
        this.offsetMs = Number.isFinite(options.transcodingOffsetTicks) && options.transcodingOffsetTicks >= 0
            ? options.transcodingOffsetTicks / 10000 : 0;
        this.webStarted = false;
        this._rateWait = Promise.resolve();
        this._failure = null;
        this.adaptSyncPlayWrapper();
        if (this.offsetMs && this.owner._playbackManager?.syncPlayEnabled && !this._wrapper) {
            return Promise.reject(new Error('This Web client cannot report native transcode SyncPlay positions.'));
        }
        const request = this._begin('load');
        const startMs = (options.playerStartPositionTicks || 0) / 10000;
        request.targetMs = Math.max(0, startMs - this.offsetMs);
        window.api.player.load(url, {
            startMilliseconds: request.targetMs,
            autoplay: !this.owner._playbackManager?.syncPlayEnabled,
            preserveTimestamps: mpvPlaybackLifecycle.preserveTimestamps(options, url),
            nativeRequestId: request.id
        }, metadata, audio, subtitle, accepted => this._ack(request, accepted));
        return request.promise;
    }

    seek(logicalMs) {
        this._failure = null;
        const request = this._begin('seek');
        request.expectedPaused = this.owner._paused;
        request.targetMs = Math.max(0, Math.round(logicalMs - this.offsetMs));
        this.owner.events.trigger(this.owner, 'waiting');
        if (typeof window.api.player.seekToForReady !== 'function') {
            this._fail(request, new Error('Native seek readiness bridge is unavailable.'));
            return;
        }
        window.api.player.seekToForReady(request.targetMs, request.id,
            accepted => this._ack(request, accepted));
    }

    _ack(request, accepted) {
        if (request !== this._request) return;
        if (accepted !== true) {
            this._fail(request, new Error('Native player rejected the playback command.'));
            return;
        }
        request.acknowledged = true;
        this._completeReady();
    }

    _completeReady() {
        const request = this._request;
        if (!request?.acknowledged || !request.sample) return;
        window.clearTimeout(request.timeout);
        this._request = null;
        this._activeId = request.id;
        this._failure = null;
        this.ready = true;
        this.owner._currentTime = request.sample.positionMs;
        this.owner._paused = request.sample.paused;
        if (this.owner._paused) this.paused();
        if (request.kind === 'load') this.owner.onNativeReady?.();
        request.resolve();
        if (request.kind === 'seek') Promise.resolve().then(() => this._notifyReady());
    }

    playing() {
        // playing can arrive before FILE_LOADED/restart. It cannot prove Ready.
        if (!this._request && this.webStarted && !this._failure) this.ready = true;
        if (this.ready) this._notifyReady();
    }

    async _notifyReady() {
        const serial = this._serial;
        try {
            await this._afterLatestRate(() => {
                if (serial !== this._serial) return;
                if (this.ready && this.webStarted && !this._failure) {
                    this.adaptSyncPlayWrapper();
                    this.owner.events.trigger(this.owner, 'playing');
                }
            });
        } catch (_) { }
    }

    pause() {
        if (this.owner._paused || this._pauseWait) return;
        this._pauseWait = new Promise(resolve => { this._pauseResolve = resolve; });
    }

    paused() {
        this._pauseResolve?.();
        this._pauseResolve = null;
        this._pauseWait = null;
    }

    rateRequest() {
        let settle;
        const wait = new Promise((resolve, reject) => { settle = accepted => {
            if (accepted === true) resolve();
            else reject(new Error('Native player rejected the playback rate.'));
        }; });
        wait.catch(() => {});
        this._rateWait = this._bounded(wait, 'Native playback rate acknowledgement timed out.');
        this._rateWait.catch(() => {});
        return settle;
    }

    _afterLatestRate(action) {
        let active = true;
        const pending = (async () => {
            while (active) {
                const wait = this._rateWait;
                try { await wait; }
                catch (error) { if (wait === this._rateWait) throw error; }
                if (!active) return;
                // A newer rate can arrive while an older acknowledgement is
                // pending. Only the latest wait releases Ready or a sample.
                if (wait === this._rateWait) return action();
            }
        })();
        // Replacements share one deadline. A late acknowledgement must not run
        // the action after this barrier has already failed.
        return this._bounded(pending, 'Native playback rate acknowledgement timed out.')
            .finally(() => { active = false; });
    }

    async position() {
        const serial = this._serial;
        if (this._request) await this._request.promise;
        if (this._pauseWait) await this._bounded(this._pauseWait, 'Native pause acknowledgement timed out.');
        const sample = await this._afterLatestRate(() => {
            if (serial !== this._serial || this._failure) throw this._failure || new Error('Native position belongs to an old playback.');
            // Dispatch in the same turn as the latest-rate check. The sample
            // keeps its own timeout after the rate barrier has completed.
            return { promise: this._bounded(new Promise((resolve, reject) => window.api.player.getPosition(position => {
                if (serial !== this._serial || !Number.isFinite(position) || position < 0) {
                    reject(new Error('Native position is unavailable.'));
                } else resolve(position);
            })), 'Native position request timed out.') };
        });
        return sample.promise;
    }

    _bounded(promise, message) {
        return new Promise((resolve, reject) => {
            const serial = this._serial;
            const timer = window.setTimeout(() => {
                if (serial === this._serial && this._eventsBound) this.owner.notifyPlaybackRateFailure?.(message);
                reject(new Error(message));
            }, 5000);
            promise.then(value => { window.clearTimeout(timer); resolve(value); },
                error => { window.clearTimeout(timer); reject(error); });
        });
    }

    static preserveTimestamps(options, url) {
        if (options.playMethod !== 'Transcode') return false;
        if (options.mediaSource?.TranscodingSubProtocol?.toLowerCase() === 'hls' ||
            ['application/x-mpegurl', 'application/vnd.apple.mpegurl'].includes(options.mimeType?.toLowerCase())) return true;
        try {
            const parsed = new URL(url);
            return /\.m3u8$/i.test(parsed.pathname) || Array.from(parsed.searchParams)
                .some(([key, value]) => key.toLowerCase() === 'copytimestamps' && value.toLowerCase() === 'true');
        } catch (_) { return false; }
    }

    adaptSyncPlayWrapper() {
        const manager = this.owner._playbackManager;
        const sync = manager?._playQueueManager?.queueCore?.manager;
        const wrapper = sync?.getPlayerWrapper?.();
        if (!manager?.syncPlayEnabled || typeof manager._localSetPlaybackRate !== 'function' ||
            wrapper?.player !== this.owner || typeof wrapper.currentTime !== 'function' ||
            typeof wrapper.onTimeUpdate !== 'function' || typeof sync.getPlaybackCore !== 'function') return;
        if (this._wrapper === wrapper) return;
        this.restoreWrapper();
        const original = { currentTime: wrapper.currentTime, currentTimeAsync: wrapper.currentTimeAsync,
            onTimeUpdate: wrapper.onTimeUpdate };
        wrapper.currentTime = () => (this.owner.currentTime() || 0) + this.offsetMs;
        wrapper.currentTimeAsync = async () => (await this.owner.currentTimeAsync()) + this.offsetMs;
        wrapper.onTimeUpdate = (event, data) => original.onTimeUpdate.call(wrapper, event,
            { ...data, currentPosition: data.currentPosition + this.offsetMs });
        this._wrapper = wrapper;
        this._wrapperOriginal = original;
        this._sync = sync;
        this._onGroupEnabled = (_, enabled) => { if (!enabled) this.restoreWrapper(); };
        this._onPlayerChanged = () => { if (sync.getPlayerWrapper()?.player !== this.owner) this.restoreWrapper(); };
        this.owner.events.on(sync, 'enabled', this._onGroupEnabled);
        this.owner.events.on(sync, 'playerchange', this._onPlayerChanged);
    }

    restoreWrapper() {
        if (!this._wrapper) return;
        Object.assign(this._wrapper, this._wrapperOriginal);
        this.owner.events.off(this._sync, 'enabled', this._onGroupEnabled);
        this.owner.events.off(this._sync, 'playerchange', this._onPlayerChanged);
        this._wrapper = null;
        this._sync = null;
    }

    _fail(request, error) {
        if (request !== this._request) return;
        window.clearTimeout(request.timeout);
        this._request = null;
        this.ready = false;
        this._activeId = null;
        this._failure = error;
        request.reject(error);
        try { this.owner._toast?.(error.message); } catch (_) { }
        if (request.kind === 'seek') this.owner.events.trigger(this.owner, 'error', [{ type: 'mediadecodeerror' }]);
    }

    cancel(message = 'Native playback was stopped.') {
        ++this._serial;
        const request = this._request;
        if (request) {
            window.clearTimeout(request.timeout);
            request.reject(new Error(message));
            this._request = null;
        }
        this.ready = false;
        this._activeId = null;
        this._failure = new Error(message);
        this.paused();
    }

    dispose() {
        this.cancel();
        this.restoreWrapper();
        for (const [signal, handler] of this._signals.splice(0)) signal.disconnect(handler);
        this.owner.events.off?.(this.owner, 'playbackstart', this._onWebStart);
        this._eventsBound = false;
        window.removeEventListener?.('pagehide', this._onPageHide);
    }
}

window._mpvPlaybackLifecycle = mpvPlaybackLifecycle;
