[CmdletBinding()]
param([Parameter(Mandatory)][string]$MsiPath, [string]$PortableZip, [switch]$IsolatedPortable)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$sourceRoot = Split-Path $PSScriptRoot -Parent
$manifest = Get-Content (Join-Path $sourceRoot 'build-manifest.json') -Raw | ConvertFrom-Json
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase([IO.Path]::GetFullPath($MsiPath), 0)
function Read-MsiRows([string]$query, [int]$columns) {
    $view = $database.OpenView($query)
    $null = $view.Execute()
    try {
        while ($record = $view.Fetch()) {
            $row = @()
            for ($column = 1; $column -le $columns; $column++) { $row += $record.StringData($column) }
            Write-Output -NoEnumerate $row
        }
    } finally { $null = $view.Close() }
}
$properties = @{}
foreach ($row in (Read-MsiRows 'SELECT `Property`, `Value` FROM `Property`' 2)) { $properties[$row[0]] = $row[1] }
if ($properties['UpgradeCode'].ToLowerInvariant() -ne '{abea6faf-13ea-465f-b2bd-a24b5524e98b}') { throw 'MSI upgrade identity changed' }
if ($properties['ProductName'] -ne 'Jellyfin Media Player') { throw 'Installed application identity changed' }
if ($properties['ProductVersion'] -ne $manifest.packageVersion) { throw 'MSI version does not match manifest' }
if ($properties['ALLUSERS'] -ne '1') { throw 'MSI must remain per-machine' }
$files = @(Read-MsiRows 'SELECT `FileName` FROM `File`' 1 | ForEach-Object { $_[0] })
if (-not ($files | Where-Object { $_ -match 'JellyfinMediaPlayer\.exe$' })) { throw 'Main executable is missing' }
if ($files | Where-Object { $_ -match '(\.pdb$|portable|settings-test|cache-test)' }) { throw 'MSI harvested portable or diagnostic data' }
$sequence = @{}
foreach ($row in (Read-MsiRows 'SELECT `Action`, `Sequence` FROM `InstallExecuteSequence`' 2)) { $sequence[$row[0]] = [int]$row[1] }
if ($sequence['RemoveExistingProducts'] -le $sequence['InstallInitialize'] -or
    $sequence['RemoveExistingProducts'] -ge $sequence['InstallFiles']) { throw 'MSI upgrade removal is not inside the rollback transaction' }
if ($PortableZip) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead([IO.Path]::GetFullPath($PortableZip))
    try {
        $entries = @($archive.Entries | ForEach-Object { $_.FullName.TrimStart('./').Replace('\', '/') })
        foreach ($required in @('JellyfinMediaPlayer.exe', 'vcruntime140.dll', 'msvcp140.dll', 'libmpv-2.dll', 'QtWebEngineProcess.exe', 'platforms/qwindows.dll', 'build-manifest.json')) {
            if ($entries -notcontains $required) { throw "Portable deployment is missing $required" }
        }
        if (($entries -contains 'portable') -ne $IsolatedPortable.IsPresent) { throw 'Portable profile marker differs from the explicit build option' }
        if ($entries | Where-Object { $_ -match '(^|/)(data|cache)/|\.pdb$' }) { throw 'Portable ZIP contains user data or debug symbols' }
    } finally { $archive.Dispose() }
}
Write-Host 'Windows package metadata and portable inventory passed'
