// The native updater supplies GitHub release metadata. Only this fork's
// validated release pages can be opened; downloads and installation stay manual.
const JMP_RELEASE_REPOSITORY = 'DevLn737/jellyfin-desktop';

function jmpParseVersion(value) {
    if (typeof value !== 'string') return null;
    const match = /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$/.exec(value);
    if (!match) return null;
    const prerelease = match[4] ? match[4].split('.') : [];
    if (prerelease.some(part => /^\d+$/.test(part) && part.length > 1 && part.startsWith('0'))) return null;
    return { value, core: match.slice(1, 4).map(BigInt), prerelease };
}

function jmpCompareVersions(left, right) {
    for (let index = 0; index < 3; index++) {
        if (left.core[index] !== right.core[index]) return left.core[index] > right.core[index] ? 1 : -1;
    }
    if (!left.prerelease.length || !right.prerelease.length) {
        return left.prerelease.length === right.prerelease.length ? 0 : (left.prerelease.length ? -1 : 1);
    }
    for (let index = 0; index < Math.max(left.prerelease.length, right.prerelease.length); index++) {
        const a = left.prerelease[index], b = right.prerelease[index];
        if (a === b) continue;
        if (a === undefined || b === undefined) return a === undefined ? -1 : 1;
        const aNumeric = /^\d+$/.test(a), bNumeric = /^\d+$/.test(b);
        if (aNumeric && bNumeric) return BigInt(a) > BigInt(b) ? 1 : -1;
        if (aNumeric !== bNumeric) return aNumeric ? -1 : 1;
        return a > b ? 1 : -1;
    }
    return 0;
}

function jmpSelectUpdate(payload, currentVersion) {
    const current = jmpParseVersion(currentVersion);
    if (!current) return null;
    let releases;
    try { releases = typeof payload === 'string' ? JSON.parse(payload) : payload; }
    catch (_) { return null; }
    if (!Array.isArray(releases)) releases = [releases];
    let selected = null;
    for (const release of releases) {
        if (!release || release.draft !== false || typeof release.prerelease !== 'boolean') continue;
        if (typeof release.tag_name !== 'string' || !release.tag_name.startsWith('v')) continue;
        const version = jmpParseVersion(release.tag_name.substring(1));
        if (!version || (release.prerelease !== Boolean(version.prerelease.length))) continue;
        if (!current.prerelease.length && version.prerelease.length) continue;
        if (jmpCompareVersions(version, current) <= 0) continue;
        const url = `https://github.com/${JMP_RELEASE_REPOSITORY}/releases/tag/${encodeURIComponent(release.tag_name)}`;
        if (release.html_url !== url) continue;
        if (!selected || jmpCompareVersions(version, selected.version) > 0) selected = { version, url };
    }
    return selected;
}

class jmpUpdatePlugin {
    constructor({ confirm }) {
        this.name = 'JMP Update Plugin';
        this.type = 'input';
        this.id = 'jmpUpdatePlugin';
        this.ready = (async () => {
            const api = await window.apiPromise;
            const userAgentVersion = /(?:JellyfinMediaPlayer\s+|JellyfinDesktop\/)([^\s]+)/.exec(navigator.userAgent);
            const currentVersion = window.jmpInfo?.appVersion || userAgentVersion?.[1] || '';
            let offeredVersion;
            const onUpdateNotify = async payload => {
                const update = jmpSelectUpdate(payload, currentVersion);
                if (!update || offeredVersion === update.version.value) return;
                offeredVersion = update.version.value;
                try {
                    await new Promise(resolve => setTimeout(resolve, 3000));
                    await confirm({
                        title: 'Update Available',
                        text: `Jellyfin Desktop version ${update.version.value} is available.`,
                        cancelText: 'Ignore',
                        confirmText: 'View release'
                    });
                    api.system.openExternalUrl(update.url);
                } catch (_) {
                    // A dismissed prompt never opens a page or downloads a file.
                }
            };
            api.system.updateInfoEmitted.connect(onUpdateNotify);
            api.system.checkForUpdates();
        })().catch(() => { /* Update checks never interrupt playback or startup. */ });
    }
}

window._jmpUpdatePlugin = jmpUpdatePlugin;
