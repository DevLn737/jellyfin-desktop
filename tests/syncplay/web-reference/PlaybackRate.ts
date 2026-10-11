export const DEFAULT_PLAYBACK_RATE = 1.0;
export const MIN_PLAYBACK_RATE = 0.25;
export const MAX_PLAYBACK_RATE = 5.0;
export const MIN_EFFECTIVE_PLAYBACK_RATE = 0.2;
export const MAX_EFFECTIVE_PLAYBACK_RATE = 5.0;
export const MAX_CORRECTION_DURATION_MS = 5000;

export interface PlaybackRateCorrection {
    playbackRate: number;
    durationMs: number;
}

export function getPlaybackRateErrorMessage(status?: number): string {
    return status === 409 ?
        'MessageSyncPlayPlaybackRateNotSupported' :
        'MessageSyncPlayPlaybackRateError';
}

/** Supports both the legacy API client and the SDK's Axios error shape. */
export function getSyncPlayErrorStatus(error: unknown): number | undefined {
    if (!error || typeof error !== 'object') return undefined;

    const requestError = error as {
        status?: number;
        response?: { status?: number };
    };
    return requestError.response?.status ?? requestError.status;
}

export function normalizePlaybackRate(playbackRate: unknown): number {
    if (typeof playbackRate !== 'number'
        || !Number.isFinite(playbackRate)
        || playbackRate < MIN_PLAYBACK_RATE
        || playbackRate > MAX_PLAYBACK_RATE
    ) {
        return DEFAULT_PLAYBACK_RATE;
    }

    return playbackRate;
}

export function estimatePositionTicks(
    positionTicks: number,
    elapsedWallTimeMs: number,
    playbackRate: unknown
): number {
    return positionTicks + elapsedWallTimeMs * normalizePlaybackRate(playbackRate) * 10000;
}

export function getPlaybackRateCorrection(
    basePlaybackRate: unknown,
    diffMediaMs: number,
    preferredDurationMs: number,
    maxDurationMs = MAX_CORRECTION_DURATION_MS
): PlaybackRateCorrection | null {
    const base = normalizePlaybackRate(basePlaybackRate);
    if (!Number.isFinite(diffMediaMs)
        || !Number.isFinite(preferredDurationMs)
        || preferredDurationMs <= 0
        || !Number.isFinite(maxDurationMs)
        || maxDurationMs <= 0
    ) {
        return null;
    }

    const requestedRate = base + diffMediaMs / preferredDurationMs;
    const playbackRate = Math.min(
        MAX_EFFECTIVE_PLAYBACK_RATE,
        Math.max(MIN_EFFECTIVE_PLAYBACK_RATE, requestedRate)
    );
    const rateDelta = playbackRate - base;

    if (Math.abs(rateDelta) < Number.EPSILON) {
        return null;
    }

    const durationMs = Math.abs(diffMediaMs / rateDelta);
    if (!Number.isFinite(durationMs) || durationMs <= 0 || durationMs > maxDurationMs) {
        return null;
    }

    return { playbackRate, durationMs };
}
