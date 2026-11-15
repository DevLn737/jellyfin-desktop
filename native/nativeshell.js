const jmpInfo = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(
    Uint8Array.from(window.atob("@@data@@"), character => character.charCodeAt(0))));
window.jmpInfo = jmpInfo;

const features = [
    "filedownload",
    "displaylanguage",
    "htmlaudioautoplay",
    "htmlvideoautoplay",
    "externallinks",
    "clientsettings",
    "multiserver",
    "exitmenu",
    "remotecontrol",
    "fullscreenchange",
    "filedownload",
    "remotevideo",
    "displaymode",
    "screensaver",
    "fileinput"
];

const plugins = [
    'mpvVideoPlayer',
    'mpvAudioPlayer',
    'jmpInputPlugin',
    'jmpDiagnosticsPlugin',
    'jmpUpdatePlugin'
];

function loadScript(src) {
    return new Promise((resolve, reject) => {
        const s = document.createElement('script');
        s.src = src;
        s.onload = resolve;
        s.onerror = reject;
        document.head.appendChild(s);
    });
}

// Add plugin loaders
for (const plugin of plugins) {
    window[plugin] = async () => {
        if (window["_" + plugin]) return window["_" + plugin];
        await loadScript(`${jmpInfo.scriptPath}${plugin}.js`);
        return window["_" + plugin];
    };
}

window.NativeShell = {
    openUrl(url, target) {
        window.api.system.openExternalUrl(url);
    },

    downloadFile(downloadInfo) {
        window.api.system.openExternalUrl(downloadInfo.url);
    },

    openClientSettings() {
        showSettingsModal();
    },

    getPlugins() {
        return plugins;
    }
};

function getDeviceProfile() {
    const CodecProfiles = [];

    if (jmpInfo.settings.video.force_transcode_dovi) {
        CodecProfiles.push({
            'Type': 'Video',
            'Conditions': [
                {
                    'Condition': 'NotEquals',
                    'Property': 'VideoRangeType',
                    'Value': 'DOVI'
                }
            ]
        });
    }

    if (jmpInfo.settings.video.force_transcode_hdr) {
        CodecProfiles.push({
            'Type': 'Video',
            'Conditions': [
                {
                    'Condition': 'Equals',
                    'Property': 'VideoRangeType',
                    'Value': 'SDR'
                }
            ]
        });
    }

    if (jmpInfo.settings.video.force_transcode_hi10p) {
        CodecProfiles.push({
            'Type': 'Video',
            'Conditions': [
                {
                    'Condition': 'LessThanEqual',
                    'Property': 'VideoBitDepth',
                    'Value': '8',
                }
            ]
        });
    }

    if (jmpInfo.settings.video.force_transcode_hevc) {
        CodecProfiles.push({
            'Type': 'Video',
            'Codec': 'hevc',
            'Conditions': [
                {
                    'Condition': 'Equals',
                    'Property': 'Width',
                    'Value': '0',
                }
            ],
        });
        CodecProfiles.push({
            'Type': 'Video',
            'Codec': 'h265',
            'Conditions': [
                {
                    'Condition': 'Equals',
                    'Property': 'Width',
                    'Value': '0',
                }
            ],
        });
    }

    if (jmpInfo.settings.video.force_transcode_av1) {
        CodecProfiles.push({
            'Type': 'Video',
            'Codec': 'av1',
            'Conditions': [
                {
                    'Condition': 'Equals',
                    'Property': 'Width',
                    'Value': '0',
                }
            ],
        });
    }

    if (jmpInfo.settings.video.force_transcode_4k) {
        CodecProfiles.push({
            'Type': 'Video',
            'Conditions': [
                {
                    'Condition': 'LessThanEqual',
                    'Property': 'Width',
                    'Value': '1920',
                },
                {
                    'Condition': 'LessThanEqual',
                    'Property': 'Height',
                    'Value': '1080',
                }
            ]
        });
    }

    const DirectPlayProfiles = [{'Type': 'Audio'}, {'Type': 'Photo'}];

    if (!jmpInfo.settings.video.always_force_transcode) {
        DirectPlayProfiles.push({'Type': 'Video'});
    }

    return {
        'Name': 'Jellyfin Media Player',
        'MaxStaticBitrate': 1000000000,
        'MusicStreamingTranscodingBitrate': 1280000,
        'TimelineOffsetSeconds': 5,
        'TranscodingProfiles': [
            {'Type': 'Audio'},
            {
                'Container': 'ts',
                'Type': 'Video',
                'Protocol': 'hls',
                'AudioCodec': 'aac,mp3,ac3,opus,vorbis',
                'VideoCodec': jmpInfo.settings.video.allow_transcode_to_hevc
                    ? (
                        jmpInfo.settings.video.prefer_transcode_to_h265
                         ? 'h265,hevc,h264,mpeg4,mpeg2video'
                         : 'h264,h265,hevc,mpeg4,mpeg2video'
                    )
                    : 'h264,mpeg4,mpeg2video',
                'MaxAudioChannels': jmpInfo.settings.audio.channels === "2.0" ? '2' : '6'
            },
            {'Container': 'jpeg', 'Type': 'Photo'}
        ],
        DirectPlayProfiles,
        'ResponseProfiles': [],
        'ContainerProfiles': [],
        CodecProfiles,
        'SubtitleProfiles': [
            {'Format': 'srt', 'Method': 'External'},
            {'Format': 'srt', 'Method': 'Embed'},
            {'Format': 'ass', 'Method': 'External'},
            {'Format': 'ass', 'Method': 'Embed'},
            {'Format': 'sub', 'Method': 'Embed'},
            {'Format': 'sub', 'Method': 'External'},
            {'Format': 'ssa', 'Method': 'Embed'},
            {'Format': 'ssa', 'Method': 'External'},
            {'Format': 'smi', 'Method': 'Embed'},
            {'Format': 'smi', 'Method': 'External'},
            {'Format': 'pgssub', 'Method': 'Embed'},
            {'Format': 'dvdsub', 'Method': 'Embed'},
            {'Format': 'dvbsub', 'Method': 'Embed'},
            {'Format': 'pgs', 'Method': 'Embed'}
        ]
    };
}

async function createApi() {
    try {
        // Can't append script until document exists
        await new Promise(resolve => {
            document.addEventListener('DOMContentLoaded', resolve);
        });

        if (jmpInfo.softwareWebEngine) {
            // Hundreds of nested card containment boundaries make Chromium's
            // software PaintArtifactCompositor spend a frame budget layerizing
            // even a small animation. Retain size/style containment, but avoid
            // those independent layout/paint boundaries in the Desktop view.
            // This is local to the software client; GPU rendering keeps Web CSS.
            // A constructed sheet also works on servers with style-src 'self',
            // without weakening their CSP or fetching a local file remotely.
            const style = new CSSStyleSheet();
            style.replaceSync(`
                .card, .cardScalable, .cardIndicators { contain: style !important; }
                .cardPadder, .cardImageContainer { contain: size style !important; }
                /* Software Viz blends this full-window surface during every
                   dialog transition, including opaque library/SyncPlay pages.
                   Keep the modal hit area and each dialog's entry/exit animation;
                   omit dimming for all dialogs on the software backend. */
                .dialogBackdrop {
                    opacity: 0 !important;
                    will-change: auto !important;
                }
                /* Large form transitions repaint/rasterize a substantial part
                   of the software viewport on every animation frame. Open them
                   promptly; a nonzero duration preserves dialogHelper's
                   animationend cleanup, including the exit transition.
                   Small action sheets keep their normal animation. */
                .formDialog.dialog-fixedSize {
                    animation-duration: 1ms !important;
                    animation-delay: 0ms !important;
                }
            `);
            document.adoptedStyleSheets = [...document.adoptedStyleSheets, style];
        }

        if (typeof window.QWebChannel !== 'function') {
            await loadScript('qrc:///qtwebchannel/qwebchannel.js');
        }
    } catch (e) {
        // try clearing out any cached CSPs
        let foundCache = false;
        for (const cache of await caches.keys()) {
            const dataDeleted = await caches.delete(cache);
            if (dataDeleted) {
                foundCache = true;
            }
        }
        if (foundCache) {
            window.location.reload();
        }
        throw e;
    }

    const channel = await new Promise((resolve) => {
        /*global QWebChannel */
        new QWebChannel(window.qt.webChannelTransport, resolve);
    });
    return channel.objects;
}

const sectionsFromStorage = window.sessionStorage.getItem('sections');
if (sectionsFromStorage) {
    jmpInfo.sections = JSON.parse(sectionsFromStorage);
}

let rawSettings = {};
Object.assign(rawSettings, jmpInfo.settings);
const settingsFromStorage = window.sessionStorage.getItem('settings');
if (settingsFromStorage) {
    rawSettings = JSON.parse(settingsFromStorage);
    Object.assign(jmpInfo.settings, rawSettings);
}

const settingsDescriptionsFromStorage = window.sessionStorage.getItem('settingsDescriptions');
if (settingsDescriptionsFromStorage) {
    jmpInfo.settingsDescriptions = JSON.parse(settingsDescriptionsFromStorage);
}

jmpInfo.settingsDescriptionsUpdate = [];
jmpInfo.settingsUpdate = [];
window.apiPromise = createApi();
window.initCompleted = new Promise(async (resolve) => {
    window.api = await window.apiPromise;
    // The profile script contains startup settings. A new document/origin must
    // see changes made by the chooser or a previous page before exposing its
    // settings proxies; another origin's sessionStorage cannot supply them.
    const currentSettings = await new Promise(done => window.api.settings.allValues('', done));
    Object.assign(rawSettings, currentSettings);
    if (window.api.diagnostics) {
        const diagnostics = window.api.diagnostics;
        let roundTripPending = false;
        // No polling when capture is disabled; at most one outstanding sample.
        const latencyTimer = window.setInterval(() => {
            if (!diagnostics.capturing || roundTripPending) return;
            roundTripPending = true;
            const before = performance.now();
            diagnostics.ping(() => {
                roundTripPending = false;
                if (diagnostics.capturing) diagnostics.recordWebChannelLatency(performance.now() - before);
            });
        }, 1000);
        window.addEventListener('pagehide', () => window.clearInterval(latencyTimer), { once: true });
        window.NativeShell.Diagnostics = {
            recordSyncPlayEvent(event, metrics = {}) {
                if (diagnostics.capturing) diagnostics.recordSyncPlayEvent(event, metrics);
            }
        };
    }
    const settingUpdate = (section, key) => (
        (data) => new Promise(resolve => {
            rawSettings[section][key] = data;
            window.sessionStorage.setItem("settings", JSON.stringify(rawSettings));
            window.api.settings.setValue(section, key, data, resolve);
        })
    );
    const setSetting = (section, key) => {
        Object.defineProperty(jmpInfo.settings[section], key, {
            set: settingUpdate(section, key),
            get: () => rawSettings[section][key]
        });
    };
    for (const settingGroup of Object.keys(rawSettings)) {
        jmpInfo.settings[settingGroup] = {};
        for (const setting of Object.keys(rawSettings[settingGroup])) {
            setSetting(settingGroup, setting, jmpInfo.settings[settingGroup][setting]);
        }
    }
    window.api.settings.sectionValueUpdate.connect(
        (section, data) => {
            Object.assign(rawSettings[section], data);
            for (const callback of jmpInfo.settingsUpdate) {
                try {
                    callback(section, data);
                } catch (e) {
                    console.error("Update handler failed:", e);
                }
            }

            // Settings will be outdated if page reloads, so save them to session storage
            window.sessionStorage.setItem("settings", JSON.stringify(rawSettings));
        }
    );
    window.api.settings.groupUpdate.connect(
        (section, data) => {
            jmpInfo.settingsDescriptions[section] = data.settings;
            for (const callback of jmpInfo.settingsDescriptionsUpdate) {
                try {
                    callback(section, data);
                } catch (e) {
                    console.error("Description update handler failed:", e);
                }
            }

            // Settings will be outdated if page reloads, so save them to session storage
            window.sessionStorage.setItem("settingsDescriptions", JSON.stringify(jmpInfo.settingsDescriptions));
        }
    );
    resolve();
});

window.NativeShell.AppHost = {
    init() {},
    getDefaultLayout() {
        return jmpInfo.mode;
    },
    supports(command) {
        return features.includes(command.toLowerCase());
    },
    getDeviceProfile,
    getSyncProfile: getDeviceProfile,
    appName() {
        return "Jellyfin Media Player";
    },
    appVersion() {
        return navigator.userAgent.split(" ")[1];
    },
    deviceName() {
        return jmpInfo.deviceName;
    },
    exit() {
        window.api.system.exit();
    }
};

let activeSettingsModal;
let settingsTooltipSheet;

async function showSettingsModal() {
    await initCompleted;
    if (activeSettingsModal?.isConnected) {
        activeSettingsModal.querySelector('input, select, textarea, button')?.focus();
        return;
    }

    const tooltipCSS = `
        .nativeClientSettings .tooltip {
            position: relative;
            display: inline-block;
            margin-left: 0.5rem;
            font-size: 18px;
            vertical-align: sub;
        }

        .nativeClientSettings .tooltip .tooltip-text {
            visibility: hidden;
            width: max-content;
            max-width: 40em;
            background-color: black;
            color: white;
            text-align: left;
            position: absolute;
            z-index: 1;
            border-radius: 6px;
            padding: 5px;
            top: -4px;
            left: 25px;
            border: solid 1px grey;
            font-size: 12px;
        }

        .nativeClientSettings .tooltip:hover .tooltip-text {
            visibility: visible;
        }
        .nativeSettingsPicker {
            position: fixed; margin: 0; box-sizing: border-box;
            padding: .3em; color: #fff; background: #202020;
            border: 1px solid #777; border-radius: .3em;
            overflow-y: auto; overscroll-behavior: contain;
        }
        .nativeSettingsPicker::backdrop { background: transparent; }
        .nativeSettingsPicker [role="option"] {
            padding: .6em .8em; cursor: pointer; overflow-wrap: anywhere;
        }
        .nativeSettingsPicker [role="option"][data-active="true"] { background: #414141; }
        .nativeSettingsPicker [aria-selected="true"] { font-weight: bold; }
        .nativeClientSettings [role="combobox"] { text-align: left; cursor: pointer; }
        `;

    if (!settingsTooltipSheet) {
        settingsTooltipSheet = new CSSStyleSheet();
        settingsTooltipSheet.replaceSync(tooltipCSS);
        document.adoptedStyleSheets = [...document.adoptedStyleSheets, settingsTooltipSheet];
    }

    const modalContainer = document.createElement("div");
    modalContainer.className = "dialogContainer nativeClientSettings";
    modalContainer.style.backgroundColor = jmpInfo.softwareWebEngine ? 'transparent' : 'rgba(0,0,0,0.5)';
    activeSettingsModal = modalContainer;
    const cleanupCallbacks = [];
    const addCleanup = callback => cleanupCallbacks.push(callback);
    let closed = false;
    const teardown = () => {
        if (closed) return;
        closed = true;
        for (const callback of cleanupCallbacks) callback();
        if (activeSettingsModal === modalContainer) activeSettingsModal = undefined;
        modalContainer.remove();
    };
    const removal = new MutationObserver(() => {
        if (!modalContainer.isConnected) teardown();
    });
    removal.observe(document.body, { childList: true });
    addCleanup(() => removal.disconnect());
    // HTML selects use Qt's native popup window. Keep this chooser inside the
    // document's top layer so fullscreen window stacking cannot hide its list.
    const createChooser = (options, initialValue, label, commit) => {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'emby-select-withcolor emby-select';
        button.setAttribute('role', 'combobox');
        button.setAttribute('aria-haspopup', 'listbox');
        button.setAttribute('aria-expanded', 'false');
        button.setAttribute('aria-label', label);
        const popup = document.createElement('dialog');
        popup.className = 'nativeSettingsPicker';
        popup.setAttribute('role', 'listbox');
        popup.setAttribute('aria-label', label);
        popup.tabIndex = -1;
        popup.id = `native-settings-picker-${createChooser.nextId++}`;
        button.setAttribute('aria-controls', popup.id);
        let value = initialValue;
        let active = -1;
        let typeahead = '';
        let lastTyped = 0;
        const selectedIndex = () => options.findIndex(option => option.value == value);
        const optionNodes = options.map((option, index) => {
            const node = document.createElement('div');
            node.id = `${popup.id}-${index}`;
            node.setAttribute('role', 'option');
            node.textContent = option.title;
            popup.appendChild(node);
            node.addEventListener('click', () => choose(index));
            return node;
        });
        const setValue = next => {
            value = next;
            const selected = selectedIndex();
            button.textContent = selected < 0 ? String(next) : options[selected].title;
            optionNodes.forEach((node, index) => node.setAttribute('aria-selected', String(index === selected)));
        };
        const activate = index => {
            active = Math.max(0, Math.min(options.length - 1, index));
            optionNodes.forEach((node, i) => node.dataset.active = String(i === active));
            const node = optionNodes[active];
            if (node) {
                popup.setAttribute('aria-activedescendant', node.id);
                button.setAttribute('aria-activedescendant', node.id);
                node.scrollIntoView({ block: 'nearest' });
            }
        };
        const position = () => {
            const rect = button.getBoundingClientRect();
            const gap = 8;
            const below = window.innerHeight - rect.bottom - gap;
            const above = rect.top - gap;
            const downward = below >= Math.min(240, above);
            const available = Math.max(40, downward ? below : above);
            popup.style.width = `${Math.min(Math.max(rect.width, 180), window.innerWidth - gap * 2)}px`;
            popup.style.maxHeight = `${Math.min(320, available)}px`;
            popup.style.left = `${Math.max(gap, Math.min(rect.left, window.innerWidth - popup.getBoundingClientRect().width - gap))}px`;
            popup.style.top = `${downward ? Math.max(gap, rect.bottom) : Math.max(gap, rect.top - popup.getBoundingClientRect().height)}px`;
        };
        const closePicker = (restoreFocus = true) => {
            if (!popup.open) return;
            popup.close();
            window.removeEventListener('resize', position);
            button.setAttribute('aria-expanded', 'false');
            button.removeAttribute('aria-activedescendant');
            typeahead = '';
            if (restoreFocus && button.isConnected) button.focus({ preventScroll: true });
        };
        const choose = index => {
            if (!options[index]) return;
            setValue(options[index].value);
            closePicker();
            commit(options[index].value);
        };
        const openPicker = () => {
            if (popup.open || !options.length) return;
            popup.showModal();
            button.setAttribute('aria-expanded', 'true');
            position();
            activate(Math.max(0, selectedIndex()));
            popup.focus({ preventScroll: true });
            window.addEventListener('resize', position);
        };
        const navigate = event => {
            if (event.key === 'Tab' && popup.open) {
                event.preventDefault();
                closePicker(false);
                const controls = [...modalContainer.querySelectorAll('button, input, select, textarea, [tabindex]')]
                    .filter(control => !control.disabled && control.tabIndex >= 0 && control.getClientRects().length);
                const index = controls.indexOf(button);
                const next = controls[index + (event.shiftKey ? -1 : 1)];
                (next || button).focus({ preventScroll: true });
            } else if (['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) {
                event.preventDefault();
                const wasOpen = popup.open;
                openPicker();
                if (event.key === 'Home') activate(0);
                else if (event.key === 'End') activate(options.length - 1);
                else if (wasOpen) activate(active + (event.key === 'ArrowDown' ? 1 : -1));
            } else if (event.key === 'Enter' || event.key === ' ') {
                event.preventDefault();
                if (popup.open) choose(active); else openPicker();
            } else if (event.key === 'Escape' && popup.open) {
                event.preventDefault();
                closePicker();
            } else if (event.key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey) {
                event.preventDefault();
                openPicker();
                const now = performance.now();
                typeahead = now - lastTyped > 700 ? event.key : typeahead + event.key;
                lastTyped = now;
                const query = typeahead.toLocaleLowerCase();
                const start = query.length === 1 ? active + 1 : active;
                for (let offset = 0; offset < options.length; offset++) {
                    const index = (start + offset) % options.length;
                    if (options[index].title.toLocaleLowerCase().startsWith(query)) { activate(index); break; }
                }
            }
        };
        button.addEventListener('click', openPicker);
        button.addEventListener('keydown', navigate);
        popup.addEventListener('keydown', navigate);
        popup.addEventListener('cancel', event => { event.preventDefault(); closePicker(); });
        popup.addEventListener('click', event => {
            const rect = popup.getBoundingClientRect();
            if (event.target === popup && (event.clientX < rect.left || event.clientX > rect.right ||
                event.clientY < rect.top || event.clientY > rect.bottom)) closePicker();
        });
        modalContainer.appendChild(popup);
        setValue(value);
        return { element: button, setValue, destroy() { closePicker(false); popup.remove(); } };
    };
    createChooser.nextId = 0;
    modalContainer.addEventListener("click", e => {
        if (e.target == modalContainer) {
            teardown();
        }
    });
    document.body.appendChild(modalContainer);

    const modalContainer2 = document.createElement("div");
    modalContainer2.className = "focuscontainer dialog dialog-fixedSize dialog-small formDialog opened";
    modalContainer.appendChild(modalContainer2);

    const modalHeader = document.createElement("div");
    modalHeader.className = "formDialogHeader";
    modalContainer2.appendChild(modalHeader);

    const title = document.createElement("h3");
    title.className = "formDialogHeaderTitle";
    title.textContent = "Jellyfin Media Player Settings";
    modalHeader.appendChild(title);

    const modalContents = document.createElement("div");
    modalContents.className = "formDialogContent smoothScrollY";
    modalContents.style.paddingTop = "2em";
    modalContents.style.marginBottom = "6.2em";
    modalContainer2.appendChild(modalContents);

    const settingUpdateHandlers = {};
    const settingValueUpdaters = {};
    const sectionDescriptors = {};
    const registerValueUpdater = (section, key, updater) => {
        settingValueUpdaters[section][key] = updater;
    };
    for (const sectionOrder of jmpInfo.sections.sort((a, b) => a.order - b.order)) {
        const section = sectionOrder.key;
        const group = document.createElement("fieldset");
        group.className = "editItemMetadataForm editMetadataForm dialog-content-centered";
        group.style.border = 0;
        group.style.outline = 0;
        modalContents.appendChild(group);
        let sectionCleanup = [];
        addCleanup(() => { for (const callback of sectionCleanup) callback(); sectionCleanup = []; });

        const createSection = async (clear) => {
            if (clear) {
                for (const callback of sectionCleanup) callback();
                sectionCleanup = [];
                group.innerHTML = "";
            }

            settingValueUpdaters[section] = {};
            const values = jmpInfo.settings[section];
            const settings = jmpInfo.settingsDescriptions[section];
            sectionDescriptors[section] = JSON.stringify(settings);

            const legend = document.createElement("legend");
            const legendHeader = document.createElement("h2");
            legendHeader.textContent = section;
            legendHeader.style.textTransform = "capitalize";
            legend.appendChild(legendHeader);
            if (section == "other") {
                const legendSubHeader = document.createElement("h4");
                legendSubHeader.textContent = "Use this section to input custom MPV configuration. These will override the above settings.";
                legend.appendChild(legendSubHeader);
            }
            group.appendChild(legend);

            for (const setting of settings) {
                const label = document.createElement("label");
                label.className = "inputContainer";
                label.style.marginBottom = "1.8em";
                label.style.display = "block";

                let helpElement;
                if (setting.help) {
                    helpElement = document.createElement("div");
                    helpElement.className = "tooltip";
                    const helpIcon = document.createElement("span");
                    helpIcon.style.fontSize = "18px"
                    helpIcon.className = "material-icons help_outline";
                    helpElement.appendChild(helpIcon);
                    const tooltipElement = document.createElement("span");
                    tooltipElement.className = "tooltip-text";
                    tooltipElement.innerText = setting.help;
                    helpElement.appendChild(tooltipElement);
                }

                if (setting.options) {
                    const options = setting.options.map(option => {
                        let optionName = option.title;
                        const swTest = `${section}.${setting.key}.`;
                        const swTest2 = `${section}.`;
                        if (optionName.startsWith(swTest)) {
                            optionName = optionName.substring(swTest.length);
                        } else if (optionName.startsWith(swTest2)) {
                            optionName = optionName.substring(swTest2.length);
                        }
                        return { value: option.value, title: optionName };
                    });
                    const chooser = createChooser(options, values[setting.key],
                        setting.displayName || setting.key,
                        value => { jmpInfo.settings[section][setting.key] = value; });
                    const control = chooser.element;
                    control.dataset.settingKey = setting.key;
                    registerValueUpdater(section, setting.key, chooser.setValue);
                    sectionCleanup.push(chooser.destroy);
                    const labelText = document.createElement('label');
                    labelText.className = "inputLabel";
                    labelText.textContent = (setting.displayName ? setting.displayName : setting.key) + ": ";
                    label.appendChild(labelText);
                    if (helpElement) label.appendChild(helpElement);
                    label.appendChild(control);
                } else if (setting.inputType === "text") {
                    const control = document.createElement("input");
                    control.type = "text";
                    control.className = "emby-input";
                    control.dataset.settingKey = setting.key;
                    registerValueUpdater(section, setting.key, value => { if (control.value !== String(value)) control.value = value; });
                    control.value = values[setting.key];
                    control.addEventListener("change", e => {
                        jmpInfo.settings[section][setting.key] = e.target.value.trim();
                    });
                    const labelText = document.createElement('label');
                    labelText.className = "inputLabel";
                    labelText.textContent = (setting.displayName ? setting.displayName : setting.key) + ": ";
                    label.appendChild(labelText);
                    if (helpElement) label.appendChild(helpElement);
                    label.appendChild(control);
                } else if (setting.inputType === "textarea") {
                    const control = document.createElement("textarea");
                    control.className = "emby-select-withcolor emby-select";
                    control.style = "resize: none;"
                    control.value = values[setting.key];
                    control.rows = 5;
                    control.dataset.settingKey = setting.key;
                    registerValueUpdater(section, setting.key, value => { if (control.value !== String(value)) control.value = value; });
                    control.addEventListener("change", e =>
                    {
                        jmpInfo.settings[section][setting.key] = e.target.value;
                    });
                    const labelText = document.createElement('label');
                    labelText.className = "inputLabel";
                    labelText.textContent = (setting.displayName ? setting.displayName : setting.key) + ": ";
                    label.appendChild(labelText);
                    if (helpElement) label.appendChild(helpElement);
                    label.appendChild(control);
                } else {
                    const control = document.createElement("input");
                    control.type = "checkbox";
                    control.dataset.settingKey = setting.key;
                    registerValueUpdater(section, setting.key, value => { control.checked = value; });
                    control.checked = values[setting.key];
                    control.addEventListener("change", e => {
                        jmpInfo.settings[section][setting.key] = e.target.checked;
                    });
                    label.appendChild(control);
                    label.appendChild(document.createTextNode(" " + (setting.displayName ? setting.displayName : setting.key)));
                    if (helpElement) label.appendChild(helpElement);
                }

                group.appendChild(label);
            }
        };
        settingUpdateHandlers[section] = () => {
            if (sectionDescriptors[section] === JSON.stringify(jmpInfo.settingsDescriptions[section])) return;
            const focused = group.contains(document.activeElement) ? document.activeElement
                : group.querySelector('[role="combobox"][aria-expanded="true"]');
            const key = focused?.dataset.settingKey;
            const selection = focused && typeof focused.selectionStart === 'number'
                ? [focused.selectionStart, focused.selectionEnd] : null;
            createSection(true);
            if (key) {
                const replacement = [...group.querySelectorAll('[data-setting-key]')]
                    .find(control => control.dataset.settingKey === key);
                replacement?.focus({ preventScroll: true });
                if (selection && replacement?.setSelectionRange) replacement.setSelectionRange(...selection);
            }
        };
        createSection();
    }

    const onSectionUpdate = (section) => {
        if (section in settingUpdateHandlers) {
            settingUpdateHandlers[section]();
        }
    };
    jmpInfo.settingsDescriptionsUpdate.push(onSectionUpdate);
    const onValueUpdate = (section, data) => {
        for (const [key, value] of Object.entries(data || {})) {
            settingValueUpdaters[section]?.[key]?.(value);
        }
    };
    jmpInfo.settingsUpdate.push(onValueUpdate);
    addCleanup(() => {
        for (const [handlers, callback] of [[jmpInfo.settingsDescriptionsUpdate, onSectionUpdate],
            [jmpInfo.settingsUpdate, onValueUpdate]]) {
            const index = handlers.indexOf(callback);
            if (index !== -1) handlers.splice(index, 1);
        }
    });

    if (window.api.diagnostics) {
        const diagnostics = window.api.diagnostics;
        const group = document.createElement('fieldset');
        group.className = 'dialog-content-centered';
        group.style.border = '0';
        const heading = document.createElement('h2');
        heading.textContent = 'Диагностика';
        group.appendChild(heading);
        const help = document.createElement('p');
        help.textContent = 'Локальный сбор числовых показателей: последние 10 минут, до 16 MiB. ' +
            'Автоматическая остановка через 15 минут. Отчёт не содержит названий медиа, URL, токенов или обычных логов и никуда не отправляется.';
        group.appendChild(help);
        const status = document.createElement('p');
        status.setAttribute('role', 'status');
        group.appendChild(status);
        const buttons = {};
        for (const [method, label] of [['startCapture', 'Начать'], ['stopCapture', 'Остановить'],
            ['saveReport', 'Сохранить отчёт'], ['clearCapture', 'Очистить']]) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'raised emby-button';
            button.style.marginRight = '0.5em';
            button.textContent = label;
            button.addEventListener('click', () => diagnostics[method]());
            group.appendChild(button);
            buttons[method] = button;
        }
        const update = active => {
            status.textContent = active ? 'Сбор включён' : 'Сбор выключен';
            buttons.startCapture.disabled = active;
            buttons.stopCapture.disabled = !active;
        };
        const saved = success => {
            status.textContent = (success ? 'Отчёт сохранён. ' : 'Не удалось сохранить отчёт. ') +
                (diagnostics.capturing ? 'Сбор включён' : 'Сбор выключен');
        };
        diagnostics.captureChanged.connect(update);
        diagnostics.reportSaved.connect(saved);
        update(diagnostics.capturing);
        const devTools = document.createElement('p');
        devTools.textContent = 'Для отдельного профиля JavaScript/layout/paint запустите приложение один раз с --webengine-devtools ' +
            'и откройте http://127.0.0.1:9222. При обычном запуске порт выключен.';
        group.appendChild(devTools);
        modalContents.appendChild(group);
        addCleanup(() => {
            diagnostics.captureChanged.disconnect(update);
            diagnostics.reportSaved.disconnect(saved);
        });
    }

    if (jmpInfo.settings.main.userWebClient) {
        const group = document.createElement("fieldset");
        group.className = "editItemMetadataForm editMetadataForm dialog-content-centered";
        group.style.border = 0;
        group.style.outline = 0;
        modalContents.appendChild(group);
        const legend = document.createElement("legend");
        const legendHeader = document.createElement("h2");
        legendHeader.textContent = "Saved Server";
        legend.appendChild(legendHeader);
        const legendSubHeader = document.createElement("h4");
        legendSubHeader.textContent = (
            "The server you first connected to is your saved server. " +
            "It provides the web client for Jellyfin Media Player in the absence of a bundled one. " +
            "You can use this option to change it to another one. This does NOT log you off."
        );
        legend.appendChild(legendSubHeader);
        group.appendChild(legend);

        const resetSavedServer = document.createElement("button");
        resetSavedServer.className = "raised button-cancel block btnCancel emby-button";
        resetSavedServer.textContent = "Reset Saved Server"
        resetSavedServer.style.marginLeft = "auto";
        resetSavedServer.style.marginRight = "auto";
        resetSavedServer.style.maxWidth = "50%";
        resetSavedServer.addEventListener("click", async () => {
            window.jmpInfo.settings.main.userWebClient = '';
            window.location.href = jmpInfo.scriptPath + "/find-webclient.html";
        });
        group.appendChild(resetSavedServer);
    }

    const closeContainer = document.createElement("div");
    closeContainer.className = "formDialogFooter";
    modalContents.appendChild(closeContainer);

    const close = document.createElement("button");
    close.className = "raised button-cancel block btnCancel formDialogFooterItem emby-button";
    close.textContent = "Close"
    close.addEventListener("click", teardown);
    closeContainer.appendChild(close);
}
