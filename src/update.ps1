# Pac-Man updater: started by the game (launcher "Update now?" -> Yes) after
# it closes itself. Downloads the new Easy Build, builds it from the ROM in
# the game folder, swaps in the new program files and starts the game again.
# Only the program files are replaced: settings, keys, high scores and mods
# are never touched. Everything it downloads is deleted at the end.
# Built into the exe (CMake embeds this file); written to %TEMP% to run.
param(
    [string]$GameDir,
    [string]$ExeName,
    [string]$ZipUrl,
    [string]$Version,
    [int]$WaitPid
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'      # much faster downloads
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$Host.UI.RawUI.WindowTitle = "Pac-Man update"

$Work = Join-Path $env:LOCALAPPDATA 'PacManRecomp-update'
$Exe  = Join-Path $GameDir $ExeName

function Say($text) { Write-Host $text -ForegroundColor Cyan }

function Remove-Work {
    for ($i = 0; $i -lt 10 -and (Test-Path $Work); $i++) {
        try { Remove-Item $Work -Recurse -Force } catch { Start-Sleep -Seconds 1 }
    }
}

function Finish($ok, $message) {
    if (-not $ok) {
        Write-Host ''
        Write-Host "The update didn't work: $message" -ForegroundColor Red
        Write-Host 'Your game was not changed.'
        $log = Join-Path $GameDir 'update-log.txt'
        if (Test-Path $log) { Write-Host "Details are in $log" }
        Write-Host ''
        Read-Host 'Press Enter to start the game you have' | Out-Null
    }
    Remove-Work
    if (Test-Path $Exe) { Start-Process -FilePath $Exe -WorkingDirectory $GameDir }
    Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
    exit
}

try {
    Say "Updating Pac-Man to version $Version"
    Say '(this window closes by itself when it is done)'
    Write-Host ''
    if ($WaitPid) { try { Wait-Process -Id $WaitPid -Timeout 30 } catch {} }
    Remove-Item (Join-Path $GameDir 'update-log.txt') -Force -ErrorAction SilentlyContinue

    Remove-Work
    New-Item -ItemType Directory -Force $Work | Out-Null
    $zip = Join-Path $Work 'update.zip'
    Say 'Downloading the new version...'
    if (Test-Path -LiteralPath $ZipUrl) { Copy-Item -LiteralPath $ZipUrl $zip }   # local test package
    else { Invoke-WebRequest -Uri $ZipUrl -OutFile $zip -UseBasicParsing }
    $src = Join-Path $Work 'src'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $src)
    Remove-Item $zip -Force
    $root = Get-ChildItem $src -Recurse -Filter build.ps1 -File | Select-Object -First 1
    if (-not $root) { throw 'the download has no build script' }
    $root = $root.DirectoryName

    # The ROM the game already uses (rom.cfg, else the .nes in the game folder).
    $rom = $null
    $cfg = Join-Path $GameDir 'rom.cfg'
    if (Test-Path $cfg) {
        $p = (Get-Content $cfg -Raw).Trim()
        if ($p -and -not [IO.Path]::IsPathRooted($p)) { $p = Join-Path $GameDir $p }
        if ($p -and (Test-Path -LiteralPath $p)) { $rom = $p }
    }
    if (-not $rom) {
        $nes = @(Get-ChildItem $GameDir -Filter *.nes -File)
        if ($nes.Count -ge 1) { $rom = $nes[0].FullName }
    }
    if (-not $rom) { throw "couldn't find your Pac-Man ROM in $GameDir" }
    Copy-Item -LiteralPath $rom (Join-Path $root 'pacman.nes')

    # The release's own build script: checks the ROM, gets the tools, builds.
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'build.ps1')
    if ($LASTEXITCODE -ne 0) {
        $blog = Join-Path $root 'build-log.txt'
        if (Test-Path $blog) { Copy-Item $blog (Join-Path $GameDir 'update-log.txt') }
        throw 'the build failed'
    }

    Write-Host ''
    Say 'Installing...'
    $new = Join-Path $root 'Game'
    for ($try = 1; ; $try++) {
        try {
            Copy-Item (Join-Path $new 'PacManRecomp.exe') $Exe -Force
            Copy-Item (Join-Path $new 'SDL2.dll') $GameDir -Force
            Copy-Item (Join-Path $new 'Modding guide.md') $GameDir -Force
            New-Item -ItemType Directory -Force (Join-Path $GameDir 'assets') | Out-Null
            Copy-Item (Join-Path $new 'assets\*') (Join-Path $GameDir 'assets') -Recurse -Force
            break
        } catch {
            if ($try -ge 10) { throw "couldn't replace the game files (is the game still open?)" }
            Start-Sleep -Seconds 1
        }
    }
    Say "Done! Starting Pac-Man $Version..."
    Finish $true ''
} catch {
    Finish $false $_.Exception.Message
}
