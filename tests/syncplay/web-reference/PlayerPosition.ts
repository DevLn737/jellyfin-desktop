/**
 * Normalizes an asynchronously reported player position to milliseconds.
 *
 * Some native players expose their event position in milliseconds but return
 * seconds from currentTimeAsync(). Prefer the interpretation closest to the
 * latest event position so standards-compliant players remain unchanged.
 */
export function normalizeAsyncPlayerPosition(position: number, referencePosition: number): number {
    if (!Number.isFinite(position) || !Number.isFinite(referencePosition)) {
        return position;
    }

    const positionAsMilliseconds = position * 1000;
    const reportedUnitDifference = Math.abs(referencePosition - position);
    const secondsUnitDifference = Math.abs(referencePosition - positionAsMilliseconds);

    // The native bridge may also truncate the seconds value to an integer.
    // Its event position is already in milliseconds and retains sub-second
    // precision, so prefer that value once the seconds contract is detected.
    return secondsUnitDifference < reportedUnitDifference ? referencePosition : position;
}
