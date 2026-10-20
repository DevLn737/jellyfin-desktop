-- Client-agnostic libmpv probe for stage 1 baseline measurements.
-- Loaded through mpv.conf: script=<path>/jfd-probe.lua and
-- script-opts=jfdprobe-out=<path to .jsonl>. Writes one JSON object per line.
local utils = require 'mp.utils'
local options = { out = '', interval = 0.25 }
require 'mp.options'.read_options(options, 'jfdprobe')
if options.out == '' then return end

local file = io.open(options.out, 'a')
if not file then
    mp.msg.error('jfd-probe: cannot open ' .. options.out)
    return
end

local function write(record)
    record.t = mp.get_time()
    file:write(utils.format_json(record), '\n')
    file:flush()
end

local numeric = {
    'avsync', 'frame-drop-count', 'decoder-frame-drop-count', 'vo-delayed-frame-count',
    'mistimed-frame-count', 'estimated-vf-fps', 'display-fps', 'time-pos', 'duration',
    'container-fps', 'estimated-display-fps', 'vsync-jitter'
}

local function sample()
    local record = { kind = 'sample' }
    for _, name in ipairs(numeric) do
        record[name] = mp.get_property_number(name)
    end
    record['hwdec-current'] = mp.get_property('hwdec-current')
    record['current-vo'] = mp.get_property('current-vo')
    record['current-ao'] = mp.get_property('current-ao')
    record['video-codec'] = mp.get_property('video-format')
    record.width = mp.get_property_number('video-params/w')
    record.height = mp.get_property_number('video-params/h')
    record.paused = mp.get_property_bool('pause')
    write(record)
end

for _, name in ipairs({ 'start-file', 'file-loaded', 'seek', 'playback-restart', 'end-file', 'video-reconfig' }) do
    mp.register_event(name, function(event)
        write({ kind = 'event', event = name, reason = event and event.reason or nil,
            ['time-pos'] = mp.get_property_number('time-pos') })
    end)
end

-- Harness markers: `script-message jfdprobe-mark <label>`.
mp.register_script_message('jfdprobe-mark', function(label)
    write({ kind = 'mark', label = label })
end)

write({ kind = 'probe-start', ['mpv-version'] = mp.get_property('mpv-version'),
    ['ffmpeg-version'] = mp.get_property('ffmpeg-version') })
mp.add_periodic_timer(options.interval, sample)
