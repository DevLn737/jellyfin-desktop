[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'test-windows-live-playback.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Live composition owner does not parse.'}
$source=$ast.FindAll({param($node)$node -is [Management.Automation.Language.StringConstantExpressionAst] -and
    $node.Value.StartsWith('using System;')},$true)
if($source.Count -ne 1){throw 'Expected one actual capture helper.'}
$assemblies=@([Drawing.Bitmap].Assembly.Location,[Drawing.Color].Assembly.Location)+
    @([Drawing.Bitmap].Assembly.GetReferencedAssemblies() | ForEach-Object {[Reflection.Assembly]::Load($_).Location})
Add-Type -ReferencedAssemblies @($assemblies | Select-Object -Unique) -TypeDefinition $source[0].Value
foreach($scale in @(1.0,1.25)) {
    $bitmap=New-Object Drawing.Bitmap ([int](640*$scale)),([int](480*$scale))
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    try {
        $a=[int[]]@((224*$scale),(336*$scale),(77*$scale),(38*$scale))
        $b=[int[]]@((435*$scale),(336*$scale),(77*$scale),(38*$scale))
        $graphics.Clear([Drawing.Color]::FromArgb(176,0,176))
        # Match the actual detail card: the central yellow card must not be
        # confused with the background samples used to verify opaque purple.
        $graphics.FillRectangle([Drawing.Brushes]::Yellow,[int](256*$scale),[int](182*$scale),[int](128*$scale),[int](86*$scale))
        if(-not [JmpLivePlaybackWindow]::Classify($bitmap,$a,$b,$scale).DetailPurple){throw 'Opaque baseline not recognized.'}
        $graphics.Clear([Drawing.Color]::Red)
        $graphics.FillRectangle([Drawing.Brushes]::Cyan,0,[int](240*$scale),$bitmap.Width,[int](240*$scale))
        $graphics.FillRectangle([Drawing.Brushes]::Blue,[int](10*$scale),[int](10*$scale),[int](30*$scale),[int](30*$scale))
        $graphics.FillRectangle([Drawing.Brushes]::Lime,[int](80*$scale),[int](70*$scale),[int](30*$scale),[int](30*$scale))
        # A moving fixture marker can cross the old quarter-height sample.
        # The real classifier must still identify the unobstructed red band.
        $graphics.FillRectangle([Drawing.Brushes]::Lime,[int](300*$scale),[int](100*$scale),[int](40*$scale),[int](40*$scale))
        $pixels=[JmpLivePlaybackWindow]::Classify($bitmap,$a,$b,$scale)
        if(-not $pixels.Red -or -not $pixels.Cyan -or -not $pixels.Blue -or $pixels.GreenCount -lt 3 -or
            $pixels.TooltipACyanFraction -lt .9){throw 'Actual pixel classifier rejected clean video.'}
        $graphics.FillRectangle([Drawing.Brushes]::Yellow,$a[0],$a[1],$a[2],$a[3])
        $pixels=[JmpLivePlaybackWindow]::Classify($bitmap,$a,$b,$scale)
        if($pixels.TooltipAYellowFraction -lt .7 -or $pixels.TooltipACyanFraction -ge .9){throw 'Tooltip presence/trail was missed.'}
        $graphics.FillRectangle([Drawing.Brushes]::Cyan,$a[0],$a[1],$a[2],$a[3])
        $graphics.FillRectangle([Drawing.Brushes]::Yellow,$b[0],$b[1],$b[2],$b[3])
        $pixels=[JmpLivePlaybackWindow]::Classify($bitmap,$a,$b,$scale)
        if($pixels.TooltipACyanFraction -lt .9 -or $pixels.TooltipBYellowFraction -lt .7){throw 'Moved tooltip classification failed.'}
        $graphics.Clear([Drawing.Color]::Black)
        $pixels=[JmpLivePlaybackWindow]::Classify($bitmap,$a,$b,$scale)
        if($pixels.Red -or $pixels.Cyan -or $pixels.GreenCount){throw 'Black/stale video was accepted.'}
    } finally {$graphics.Dispose();$bitmap.Dispose()}
}
Write-Host 'Actual capture pixel classifier passed synthetic opaque/video/tooltip/black cases at DPR1 and1.25; no Win32 or app call.'
