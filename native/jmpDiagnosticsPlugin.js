// Local diagnostics observes the existing connection only while capture is active.
// Shared SyncPlay payloads stay untouched: the manager may convert When to Date.
const JMP_SYNCPLAY_COMMANDS = ['Unpause', 'Pause', 'Stop', 'Seek'];
const JMP_SYNCPLAY_EVENTS = { Unpause: 'resume', Pause: 'pause', Stop: 'command', Seek: 'seek' };
const JMP_SYNCPLAY_STATES = ['Idle', 'Waiting', 'Paused', 'Playing'];

function jmpSyncPlayNumbers(data) {
    const metrics = {};
    if (typeof data.PositionTicks === 'number' && Number.isFinite(data.PositionTicks) &&
        data.PositionTicks >= 0 && data.PositionTicks <= Number.MAX_SAFE_INTEGER) {
        metrics.positionMs = data.PositionTicks / 10000;
    }
    if (typeof data.PlaybackRate === 'number' && Number.isFinite(data.PlaybackRate) && data.PlaybackRate > 0) {
        metrics.playbackRate = data.PlaybackRate;
    }
    let when;
    if (Object.prototype.toString.call(data.When) === '[object Date]') {
        when = data.When.getTime();
    } else if (typeof data.When === 'string' &&
        /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d{1,7})?(?:Z|[+-]\d{2}:\d{2})$/.test(data.When)) {
        when = Date.parse(data.When);
    }
    if (Number.isFinite(when) && when >= 0 && when <= Number.MAX_SAFE_INTEGER) metrics.whenUtcMs = when;
    return metrics;
}

class jmpDiagnosticsPlugin {
    constructor({ events, ServerConnections }) {
        this.name = 'JMP Diagnostics Plugin';
        this.type = 'input';
        this.id = 'jmpDiagnosticsPlugin';
        this._events = events;
        this._connections = ServerConnections;
        this._unsubscribers = [];
        this._generation = 0;
        this._disposed = false;
        this._unsupportedClients = new WeakSet();

        this.ready = (async () => {
            const api = await window.apiPromise;
            if (this._disposed) return;
            const diagnostics = api?.diagnostics;
            if (!diagnostics?.captureChanged?.connect || !diagnostics.recordSyncPlayEvent ||
                !events?.on || !events?.off || !ServerConnections?.currentApiClient) return;
            this._diagnostics = diagnostics;
            this._captureChanged = () => this._refresh();
            this._connectionChanged = () => {
                this._refresh();
                // apiclientcreated may run before currentApiClient is assigned.
                Promise.resolve().then(() => this._refresh());
            };
            this._signedIn = () => {
                this._signedOutClient = null;
                this._refresh();
            };
            this._signedOut = () => {
                this._signedOutClient = this._activeClient || this._currentClient();
                this._detach();
            };
            diagnostics.captureChanged.connect(this._captureChanged);
            events.on(ServerConnections, 'apiclientcreated', this._connectionChanged);
            events.on(ServerConnections, 'localusersignedin', this._signedIn);
            events.on(ServerConnections, 'localusersignedout', this._signedOut);
            this._pageHide = () => this.dispose();
            window.addEventListener('pagehide', this._pageHide);
            this._refresh();
        })().catch(() => { this.dispose(); });
    }

    _currentClient() {
        try { return this._connections.currentApiClient(); }
        catch (_) { return null; }
    }

    _refresh() {
        if (this._disposed || !this._diagnostics) return;
        const client = this._diagnostics.capturing ? this._currentClient() : null;
        if (client === this._activeClient) return;
        this._detach();
        if (!client || client === this._signedOutClient || this._unsupportedClients.has(client)) return;
        this._activeClient = client;
        const generation = this._generation;
        const receive = (type, message) => {
            if (this._disposed || !this._diagnostics.capturing || generation !== this._generation) return;
            if (this._currentClient() !== client) { this._refresh(); return; }
            try { this._recordMessage(type, message); } catch (_) { /* Ignore malformed optional metrics. */ }
        };
        try {
            if (typeof client.subscribe === 'function') {
                for (const type of ['SyncPlayCommand', 'SyncPlayGroupUpdate']) {
                    const unsubscribe = client.subscribe([type], message => receive(type, message));
                    if (typeof unsubscribe !== 'function') throw new Error('Unsupported subscription cleanup');
                    this._unsubscribers.push(unsubscribe);
                }
            } else {
                const handler = (_, message) => receive(message?.MessageType, message);
                this._events.on(client, 'message', handler);
                this._unsubscribers.push(() => this._events.off(client, 'message', handler));
            }
        } catch (_) {
            // Optional metrics must never affect playback or leave a partial subscription.
            // Do not repeatedly register an unremovable callback on unknown adapters.
            if ((typeof client === 'object' && client !== null) || typeof client === 'function') {
                this._unsupportedClients.add(client);
            }
            this._detach();
        }
    }

    _recordMessage(type, message) {
        const data = message?.Data;
        if (!data || typeof data !== 'object') return;
        if (type === 'SyncPlayCommand') {
            const command = typeof data.Command === 'number' ? JMP_SYNCPLAY_COMMANDS[data.Command] : data.Command;
            if (!Object.prototype.hasOwnProperty.call(JMP_SYNCPLAY_EVENTS, command)) return;
            const metrics = jmpSyncPlayNumbers(data);
            metrics.status = JMP_SYNCPLAY_COMMANDS.indexOf(command);
            this._record(JMP_SYNCPLAY_EVENTS[command], metrics);
        } else if (type === 'SyncPlayGroupUpdate') {
            if (data.Type === 'GroupJoined') this._record('join', {});
            else if (data.Type === 'GroupLeft') this._record('leave', {});
            else if (data.Type === 'StateUpdate' &&
                (data.Data?.Reason === 'Buffer' || data.Data?.Reason === 9)) {
                const state = typeof data.Data.State === 'number'
                    ? data.Data.State : JMP_SYNCPLAY_STATES.indexOf(data.Data.State);
                const metrics = {};
                if (Number.isInteger(state) && state >= 0 && state < JMP_SYNCPLAY_STATES.length) metrics.status = state;
                this._record('buffering', metrics);
            }
        }
    }

    _record(event, metrics) {
        try { this._diagnostics.recordSyncPlayEvent(event, metrics); }
        catch (_) { /* Unavailable diagnostics does not interrupt the connection. */ }
    }

    _detach() {
        ++this._generation;
        this._activeClient = null;
        for (const unsubscribe of this._unsubscribers.splice(0)) {
            try { unsubscribe(); } catch (_) { /* Continue removing other subscriptions. */ }
        }
    }

    dispose() {
        if (this._disposed) return;
        this._disposed = true;
        this._detach();
        try { this._diagnostics?.captureChanged?.disconnect(this._captureChanged); } catch (_) { }
        for (const [name, handler] of [
            ['apiclientcreated', this._connectionChanged],
            ['localusersignedin', this._signedIn],
            ['localusersignedout', this._signedOut]
        ]) {
            if (handler) {
                try { this._events.off(this._connections, name, handler); } catch (_) { }
            }
        }
        if (this._pageHide) window.removeEventListener('pagehide', this._pageHide);
    }
}

window._jmpDiagnosticsPlugin = jmpDiagnosticsPlugin;
