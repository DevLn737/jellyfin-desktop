[CmdletBinding()]
param([string]$FFmpeg = 'ffmpeg.exe')
$ErrorActionPreference = 'Stop'
$executable = (Get-Command $FFmpeg -CommandType Application).Source
$arguments = @('-hide_banner','-loglevel','error','-y',
    '-f','lavfi','-i','color=c=red:s=128x72:r=10,drawbox=x=0:y=36:w=128:h=36:color=cyan:t=fill',
    '-f','lavfi','-i','color=c=lime:s=12x10:r=10',
    '-f','lavfi','-i','anullsrc=r=48000:cl=mono',
    '-filter_complex',"[0:v][1:v]overlay=x='mod(t*20,110)':y=8:eval=frame:shortest=1[v]",
    '-map','[v]','-map','2:a','-t','12','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p',
    '-threads','1','-bf','0','-g','20','-c:a','aac','-b:a','64k','-output_ts_offset','30',
    '-hls_time','4','-hls_playlist_type','vod','-hls_segment_options','mpegts_copyts=1',
    '-hls_segment_filename','segment-%03d.ts','-f','hls','index.m3u8')
Push-Location $PSScriptRoot
try {
    & $executable @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Synthetic composition fixture generation failed.' }
    $hashes = [ordered]@{}; $bytes = 0L
    foreach ($name in @('index.m3u8','segment-000.ts','segment-001.ts','segment-002.ts')) {
        $hashes[$name] = (Get-FileHash -LiteralPath $name -Algorithm SHA256).Hash.ToLowerInvariant()
        $bytes += (Get-Item -LiteralPath $name).Length
    }
    $info = [ordered]@{schemaVersion=1;kind='synthetic-red-cyan-video-with-moving-green-marker-and-silent-audio';
        sourceMedia='FFmpeg lavfi color/drawbox/overlay/anullsrc; no server or user media';
        ffmpegVersion=(& $executable -version | Select-Object -First 1);
        ffmpegExecutableSha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant();
        generatorArguments=$arguments;nominalStartMilliseconds=30000;durationMilliseconds=12000;generatedMediaBytes=$bytes;sha256=$hashes;
        acceptanceScope='Bounded visible Desktop composition regression only; never memory or release qualification.'}
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'fixture-info.json'),
        (($info | ConvertTo-Json -Depth 8)+"`n"),(New-Object Text.UTF8Encoding $false))
} finally { Pop-Location }
