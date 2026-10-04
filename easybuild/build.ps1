# PacManRecomp Easy Build (Windows): builds the game on this PC from your own
# ROM. "Build Pac-Man.bat" runs it with -Gui: a window with progress bars,
# the finished game in the "Pac-Man Recomp" folder, and the downloaded build
# tools and temporary build files deleted at the end.
#
# Without -Gui it runs as a console script, the way the in-game updater
# (src/update.ps1, built into games from 1.0.2 on) runs it, hidden. Keep that
# mode's contract: the one .nes file in this folder, the finished game in
# .\Game, nothing deleted (the updater deletes its whole folder), "PROBLEM:"
# before an error, exit code 1 on failure, and the step messages and build-log
# headings the updater follows for its progress bar ("Step N of 4",
# "Downloading <tool>", "==== Compiling the game"...).
param([switch]$Gui)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$Src     = Join-Path $Root 'source'
$Tools   = Join-Path $Root 'build-tools'
$Pac     = Join-Path $Src 'PacManRecomp'
$OutName = if ($Gui) { 'Pac-Man Recomp' } else { 'Game' }
$Game    = Join-Path $Root $OutName
$Log     = Join-Path $Root 'build-log.txt'
$RomCrc  = '9E4E9CC2'                          # PRG + CHR, without the header
$Version = '?'
try {
    $m = Select-String -Path (Join-Path $Pac 'CMakeLists.txt') -Pattern 'project\(PacManRecomp VERSION ([0-9.]+)'
    if ($m) { $Version = $m.Matches[0].Groups[1].Value }
} catch {}

$script:cancel  = $false
$script:proc    = $null
$script:wc      = $null
$script:overall = 0.0

# ---- the window (-Gui) ------------------------------------------------------------
if ($Gui) {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    [Windows.Forms.Application]::EnableVisualStyles()
    $form = New-Object Windows.Forms.Form
    $form.Text = "Pac-Man Recomp $Version - Easy Build"
    $form.ClientSize = New-Object Drawing.Size(560, 330)
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.StartPosition = 'CenterScreen'
    $form.Font = New-Object Drawing.Font('Segoe UI', 10)
    function New-Label($y, $h, $text, $size, $bold, $color) {
        $l = New-Object Windows.Forms.Label
        $l.Location = New-Object Drawing.Point(20, $y)
        $l.Size = New-Object Drawing.Size(520, $h)
        $l.Text = $text
        $style = if ($bold) { [Drawing.FontStyle]::Bold } else { [Drawing.FontStyle]::Regular }
        $l.Font = New-Object Drawing.Font('Segoe UI', $size, $style)
        if ($color) { $l.ForeColor = $color }
        $form.Controls.Add($l)
        return $l
    }
    function New-Bar($y) {
        $b = New-Object Windows.Forms.ProgressBar
        $b.Location = New-Object Drawing.Point(20, $y)
        $b.Size = New-Object Drawing.Size(520, 22)
        $b.Maximum = 1000
        $form.Controls.Add($b)
        return $b
    }
    $null     = New-Label 12 28 "Pac-Man Recomp $Version" 14 $true $null
    $null     = New-Label 40 20 'Easy Build for Windows: makes the game on this PC from your own ROM.' 9 $false ([Drawing.Color]::DimGray)
    $uiStep   = New-Label 72 22 'Starting...' 10 $true $null
    $uiBar    = New-Bar 96
    $uiDetail = New-Label 120 20 '' 9 $false ([Drawing.Color]::DimGray)
    $null     = New-Label 146 20 'Overall' 9 $false $null
    $uiAll    = New-Bar 166
    $uiInfo   = New-Label 200 70 '' 9 $false ([Drawing.Color]::DimGray)
    $uiInfo.BorderStyle = 'FixedSingle'
    $uiInfo.Padding = New-Object Windows.Forms.Padding(6)
    $buttons = @{}
    foreach ($b in @(@('Cancel', 440), @('Close', 440), @('Open folder', 330), @('Play', 220), @('Open log', 330))) {
        $btn = New-Object Windows.Forms.Button
        $btn.Text = $b[0]
        $btn.Location = New-Object Drawing.Point($b[1], 286)
        $btn.Size = New-Object Drawing.Size(100, 30)
        $btn.Visible = $b[0] -eq 'Cancel'
        $form.Controls.Add($btn)
        $buttons[$b[0]] = $btn
    }
    $buttons['Cancel'].Add_Click({ $script:cancel = $true })
    $buttons['Close'].Add_Click({ $form.Close() })
    $buttons['Open folder'].Add_Click({ Start-Process explorer.exe -ArgumentList "`"$Game`"" })
    $buttons['Play'].Add_Click({ Start-Process (Join-Path $Game 'PacManRecomp.exe') -WorkingDirectory $Game; $form.Close() })
    $buttons['Open log'].Add_Click({ Start-Process notepad.exe -ArgumentList "`"$Log`"" })
    $form.Add_FormClosing({ param($s, $e)
        if (-not $script:finished) { $e.Cancel = $true; $script:cancel = $true }
    })
    $form.Show()
}

function Pump($ms) {
    $end = [DateTime]::Now.AddMilliseconds($ms)
    do {
        if ($Gui) { [Windows.Forms.Application]::DoEvents() }
        Start-Sleep -Milliseconds 30
    } while ([DateTime]::Now -lt $end)
    if ($script:cancel) { throw 'CANCELLED' }
}

# Console messages (the updater reads these) and the window's step line.
function Say($text, $info) {
    Write-Host $text -ForegroundColor Cyan
    if ($Gui) {
        $uiStep.Text = $text.Trim().TrimEnd('.')
        if ($info) { $uiInfo.Text = $info }
        [Windows.Forms.Application]::DoEvents()
    }
}

# Progress: this task's share of the whole build is [$from, $to] percent.
$script:span = @(0, 0)
function Task([double]$from, [double]$to) { $script:span = @($from, $to); Show-Bar 0 '' }
# Windows animates a bar's fill slowly, so it lags far behind fast updates;
# stepping one past the value and back skips the animation.
function Set-Bar($bar, [int]$v) {
    $v = [Math]::Max(0, [Math]::Min($bar.Maximum, $v))
    if ($v -lt $bar.Maximum) { $bar.Value = $v + 1 }
    $bar.Value = $v
}
# $frac: 0..1 done, or below 0 when the length is unknown (a moving bar).
function Show-Bar([double]$frac, $detail) {
    if (-not $Gui) { return }
    if ($frac -lt 0) {
        $uiBar.Style = 'Marquee'
    } else {
        $uiBar.Style = 'Continuous'
        Set-Bar $uiBar (1000 * [Math]::Min(1, $frac))
        $all = $script:span[0] + ($script:span[1] - $script:span[0]) * [Math]::Min(1, $frac)
        if ($all -gt $script:overall) { $script:overall = $all }
    }
    Set-Bar $uiAll (10 * $script:overall)
    if ($detail -ne $null) { $uiDetail.Text = $detail }
    [Windows.Forms.Application]::DoEvents()
}

function Remove-Folder($path) {
    # rmdir in the background, so the window keeps drawing (slow on exFAT).
    if (-not (Test-Path -LiteralPath $path)) { return }
    $p = Start-Process cmd.exe -ArgumentList "/d /c rmdir /s /q `"$path`"" -WindowStyle Hidden -PassThru
    while (-not $p.HasExited) {
        if ($Gui) { [Windows.Forms.Application]::DoEvents() }
        Start-Sleep -Milliseconds 50
    }
}

# Temporary files: the build tools and everything the build made in source\.
function Remove-Temp {
    if (-not $Gui) { return }                       # the updater cleans up itself
    Show-Bar -1 'Deleting temporary files...'
    foreach ($p in @($Tools, (Join-Path $Src 'nesrecomp\build'), (Join-Path $Pac 'build'),
                     (Join-Path $Pac 'generated'))) { Remove-Folder $p }
    Remove-Item (Join-Path $Pac 'pacman.nes') -Force -ErrorAction SilentlyContinue
    Get-ChildItem $Tools -Filter *.zip -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
}

function Stop-Work {
    if ($script:proc -and -not $script:proc.HasExited) {
        & taskkill.exe /T /F /PID $script:proc.Id 2>&1 | Out-Null
    }
    if ($script:wc -and $script:wc.IsBusy) {
        $script:wc.CancelAsync()
        for ($i = 0; $i -lt 50 -and $script:wc.IsBusy; $i++) {
            if ($Gui) { [Windows.Forms.Application]::DoEvents() }
            Start-Sleep -Milliseconds 100
        }
    }
}

function Fail($text) {
    Write-Host ''
    Write-Host "PROBLEM: $text" -ForegroundColor Red
    if (-not $Gui) { exit 1 }
    throw "PROBLEM: $text"
}

# Download with a byte count (WebClient in the background, file size polled).
function Get-File($url, $out, $name) {
    $total = 0
    try {
        $rq = [Net.HttpWebRequest]::Create($url); $rq.Method = 'HEAD'
        $rs = $rq.GetResponse(); $total = $rs.ContentLength; $rs.Close()
    } catch {}
    $script:wc = New-Object Net.WebClient
    $script:wc.DownloadFileAsync([Uri]$url, $out)
    while ($script:wc.IsBusy) {
        Pump 200
        $got = if (Test-Path $out) { (Get-Item $out).Length } else { 0 }
        if ($total -gt 0) { Show-Bar ($got / $total) ('{0:N0} of {1:N0} MB' -f ($got / 1MB), ($total / 1MB)) }
        else { Show-Bar -1 ('{0:N0} MB' -f ($got / 1MB)) }
    }
    if (-not (Test-Path $out) -or ($total -gt 0 -and (Get-Item $out).Length -ne $total)) {
        throw "download failed"
    }
}

# Unzip entry by entry, with a file count.
function Expand-Zip($zip, $dir) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $za = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $n = 0; $count = $za.Entries.Count
        foreach ($e in $za.Entries) {
            $dest = Join-Path $dir $e.FullName
            if ($e.FullName.EndsWith('/')) { New-Item -ItemType Directory -Force $dest | Out-Null }
            else {
                $parent = Split-Path $dest -Parent
                if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Force $parent | Out-Null }
                [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $true)
            }
            if ((++$n % 25) -eq 0) { Pump 0; Show-Bar ($n / $count) ('{0:N0} of {1:N0} files' -f $n, $count) }
        }
    } finally { $za.Dispose() }
}

# A build command: run hidden, output appended to build-log.txt, its
# [done/total] counter shown. The log heading is what the updater follows.
function Run($what, $exe, [string[]]$argv) {
    [IO.File]::AppendAllText($Log, "`r`n==== $what`r`n> $exe $($argv -join ' ')`r`n")
    $env:EASYBUILD_CMD = (@($exe) + $argv | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    $env:EASYBUILD_LOG = $Log
    $script:proc = Start-Process cmd.exe -WorkingDirectory $Pac -WindowStyle Hidden -PassThru `
        -ArgumentList '/d /s /c "%EASYBUILD_CMD% >>"%EASYBUILD_LOG%" 2>&1"'
    $null = $script:proc.Handle                     # keeps ExitCode readable
    $heading = "==== $what"
    while (-not $script:proc.HasExited) {
        Pump 300
        $text = ''
        try {
            $fs = [IO.File]::Open($Log, 'Open', 'Read', 'ReadWrite')
            try { $text = (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
        } catch {}
        $at = $text.LastIndexOf($heading)
        $m = if ($at -ge 0) { [regex]::Matches($text.Substring($at), '\[(\d+)/(\d+)\]') } else { @() }
        if ($m.Count -gt 0) {
            $last = $m[$m.Count - 1]
            $done = [int]$last.Groups[1].Value; $total = [int]$last.Groups[2].Value
            $more = "$done of $total files"
            if ($what -eq 'Compiling the game' -and $total - $done -le 3) { $more += ' (the last ones are the biggest)' }
            Show-Bar ($done / [Math]::Max(1, $total)) $more
        } else { Show-Bar -1 $null }
    }
    $script:proc.WaitForExit()
    $code = $script:proc.ExitCode
    $script:proc = $null
    if ($code -ne 0) { Fail "$what failed. Details are in build-log.txt (please include it if you ask for help)." }
}

try {
    Remove-Item $Log -ErrorAction SilentlyContinue

    # ---- 1. Check the folder and the ROM --------------------------------------------
    Task 0 2
    Say 'Step 1 of 4: looking for your Pac-Man ROM...' 'Checking that this is the right Pac-Man ROM. Everything is made from it: nothing from the game is downloaded.'
    $longPaths = $false
    try { $longPaths = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name LongPathsEnabled -ErrorAction Stop).LongPathsEnabled -eq 1 } catch {}
    # The build's deepest file is ~141 characters below this folder; Windows
    # paths stop at 259 unless long paths are switched on.
    if (-not $longPaths -and $Root.TrimEnd([char]92).Length -gt 108) {
        Fail "This folder's path is too long for Windows to build in. Move the whole folder somewhere shorter, for example C:\Games, and run Build Pac-Man.bat again."
    }
    $free = $null
    try {
        $drive = New-Object IO.DriveInfo([IO.Path]::GetPathRoot($Root))
        $free = $drive.AvailableFreeSpace; $driveName = $drive.Name.TrimEnd([char]92)
    } catch {}                                      # a network folder: can't tell
    if ($free -ne $null -and $free -lt 2GB) {
        Fail ("Not enough free space on drive {0}. Building needs about 2 GB free while it works (the temporary files are deleted at the end); you have {1:N1} GB." -f $driveName, ($free / 1GB))
    }

    $roms = @(Get-ChildItem -Path $Root -Filter *.nes -File)
    if ($roms.Count -eq 0) { Fail "No ROM found. Put your Pac-Man ROM (a .nes file) in this folder:`n  $Root`nthen run Build Pac-Man.bat again." }
    if ($roms.Count -gt 1) { Fail "More than one .nes file in this folder. Leave only your Pac-Man ROM here." }
    $rom = $roms[0].FullName
    Add-Type -TypeDefinition @'
public static class Crc32 {
    public static string Of(byte[] d, int start) {
        uint[] t = new uint[256];
        for (uint i = 0; i < 256; i++) { uint c = i; for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; }
        uint crc = 0xFFFFFFFFu;
        for (int i = start; i < d.Length; i++) crc = t[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
        return (crc ^ 0xFFFFFFFFu).ToString("X8");
    }
}
'@
    $bytes = [IO.File]::ReadAllBytes($rom)
    if ($bytes.Length -lt 16 -or $bytes[0] -ne 0x4E -or $bytes[1] -ne 0x45 -or $bytes[2] -ne 0x53) { Fail "$($roms[0].Name) is not an NES ROM." }
    $crc = [Crc32]::Of($bytes, 16)
    if ($crc -ne $RomCrc) { Fail "$($roms[0].Name) is not the right ROM (CRC32 $crc, expected $RomCrc).`nYou need Pac-Man (USA) (Namco) for the NES." }
    Say "  Found $($roms[0].Name): correct ROM."
    Copy-Item $rom (Join-Path $Pac 'pacman.nes') -Force
    Show-Bar 1 ''

    # ---- 2. Build tools -------------------------------------------------------------
    $downloads = @(
        @{ Name = 'llvm-mingw'; Url = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip'; Sha = 'E3AD77D117A4BEA19A7A3B333341824D79A5A371004A10E25B8504E7B3047666'; Get = @(2, 26); Unpack = @(26, 38) },
        @{ Name = 'cmake';      Url = 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip';               Sha = 'BACE36E94B31C68AB6FA295F26DFA11219E0701CF7C94B0284A7D1CB13DAC536'; Get = @(38, 41); Unpack = @(41, 47) },
        @{ Name = 'ninja';      Url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip';                           Sha = '07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65'; Get = @(47, 48); Unpack = @(48, 48) }
    )
    Say 'Step 2 of 4: getting the build tools (only the first time; about 245 MB)...' 'Downloading free build tools (a C compiler, CMake and Ninja) from their official pages. Each is checked against its known fingerprint.'
    New-Item -ItemType Directory -Force $Tools | Out-Null
    foreach ($d in $downloads) {
        $dir = Join-Path $Tools $d.Name
        if (Test-Path (Join-Path $dir '.done')) { continue }
        $zip = Join-Path $Tools "$($d.Name).zip"
        Task $d.Get[0] $d.Get[1]
        Say "  Downloading $($d.Name)..." 'Downloading free build tools (a C compiler, CMake and Ninja) from their official pages. Each is checked against its known fingerprint.'
        try { Get-File $d.Url $zip $d.Name }
        catch { if ($_.Exception.Message -eq 'CANCELLED') { throw }; Fail "Couldn't download $($d.Name). Check your internet connection and try again.`n$($_.Exception.Message)" }
        $sha = [Security.Cryptography.SHA256]::Create()
        $fs = [IO.File]::OpenRead($zip)
        try { $hash = -join ($sha.ComputeHash($fs) | ForEach-Object { $_.ToString('X2') }) } finally { $fs.Dispose() }
        if ($hash -ne $d.Sha) { Remove-Item $zip -Force; Fail "The $($d.Name) download didn't match its expected fingerprint; it was deleted. Try again later." }
        Task $d.Unpack[0] $d.Unpack[1]
        Say "  Unpacking $($d.Name)..." 'Unpacking the tools: thousands of small files. This is slow on USB sticks and exFAT drives, and while antivirus checks each file.'
        if (Test-Path $dir) { Remove-Folder $dir }
        Expand-Zip $zip $dir
        Remove-Item $zip -Force
        New-Item -ItemType File (Join-Path $dir '.done') | Out-Null
    }
    $llvmBin  = Join-Path (Get-ChildItem (Join-Path $Tools 'llvm-mingw') -Directory | Select-Object -First 1).FullName 'bin'
    $cmakeBin = Join-Path (Get-ChildItem (Join-Path $Tools 'cmake') -Directory | Select-Object -First 1).FullName 'bin'
    $env:PATH = (@($llvmBin, $cmakeBin, (Join-Path $Tools 'ninja')) -join ';') + ';' + $env:PATH

    # ---- 3. Translate the ROM and compile -------------------------------------------
    $cc = @('-G', 'Ninja', '-Wno-dev', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_BUILD_TYPE=Release')
    Task 48 50
    Say 'Step 3 of 4: building the recompiler...' 'Building NESRecomp, the program that translates the NES game''s code into C.'
    Run 'Configuring the recompiler' 'cmake' (@('-S', '../nesrecomp/recompiler', '-B', '../nesrecomp/build/recompiler') + $cc)
    Task 50 58
    Run 'Building the recompiler' 'cmake' @('--build', '../nesrecomp/build/recompiler')
    Task 58 62
    Say '  Translating your ROM into C...' 'Translating Pac-Man''s 6502 program into C code, here on your PC.'
    Run 'Translating the ROM' '..\nesrecomp\build\recompiler\NESRecomp.exe' @('pacman.nes', '--game', 'game.toml')
    Task 62 65
    Say 'Step 4 of 4: compiling the game (this takes a few minutes)...' 'Compiling the translated game and its launcher into PacManRecomp.exe. The last files are the biggest, so the bar slows down near the end.'
    Run 'Configuring the game' 'cmake' (@('-S', '.', '-B', 'build', '-DCMAKE_CXX_COMPILER=clang++') + $cc)
    Task 65 96
    Run 'Compiling the game' 'cmake' @('--build', 'build')

    # ---- 4. The game folder -----------------------------------------------------------
    Task 96 97
    Say "  Copying the game into the `"$OutName`" folder..." "Copying the game into the `"$OutName`" folder. Settings, high scores and mods already in it are kept."
    New-Item -ItemType Directory -Force $Game | Out-Null
    Copy-Item (Join-Path $Pac 'build\PacManRecomp.exe') $Game -Force
    Copy-Item (Join-Path $Pac 'build\SDL2.dll') $Game -Force
    New-Item -ItemType Directory -Force (Join-Path $Game 'assets') | Out-Null   # launcher fonts and pictures
    Copy-Item (Join-Path $Pac 'build\assets\*') (Join-Path $Game 'assets') -Recurse -Force
    Copy-Item $rom (Join-Path $Game 'pacman.nes') -Force
    Set-Content -Path (Join-Path $Game 'rom.cfg') -Value 'pacman.nes' -NoNewline -Encoding ascii   # next to the exe
    Copy-Item (Join-Path $Pac 'docs\MODDING.md') (Join-Path $Game 'Modding guide.md') -Force

    if ($Gui) {
        Task 97 100
        $uiStep.Text = 'Cleaning up'
        $uiInfo.Text = 'Deleting the build tools and temporary build files. Only your game folder is left.'
        $buttons['Cancel'].Enabled = $false
        Remove-Temp
        Remove-Item $Log -ErrorAction SilentlyContinue
        $script:overall = 100; $uiBar.Style = 'Continuous'; Show-Bar 1 ''
    }
    Write-Host ''
    Write-Host "DONE! Your game is in the $OutName folder: double-click PacManRecomp.exe to play." -ForegroundColor Green
    Write-Host "(You can move the $OutName folder anywhere you like. The rest of this folder can be deleted.)"
    if ($Gui) {
        $uiStep.Text = 'Done!'
        $uiStep.ForeColor = [Drawing.Color]::ForestGreen
        $uiDetail.Text = ''
        $uiInfo.Text = "Your game is in the `"$OutName`" folder. You can move that folder anywhere you like; everything else here can be deleted (or kept, to build again)."
        $buttons['Cancel'].Visible = $false
        foreach ($b in 'Close', 'Open folder', 'Play') { $buttons[$b].Visible = $true }
    }
} catch {
    if (-not $Gui) { throw }
    $err = $_
    $msg = $err.Exception.Message
    try { Stop-Work } catch {}
    $buttons['Cancel'].Visible = $false
    $uiBar.Style = 'Continuous'
    if ($msg -eq 'CANCELLED') {
        $uiStep.Text = 'Cancelled. Cleaning up...'
        try { Remove-Temp } catch {}
        Remove-Item $Log -ErrorAction SilentlyContinue
        $uiStep.Text = 'Cancelled.'
        $uiInfo.Text = 'Nothing was built, and the temporary files were deleted. Run Build Pac-Man.bat again whenever you like.'
    } else {
        if (-not ($msg -like 'PROBLEM:*')) {
            [IO.File]::AppendAllText($Log, "`r`n==== Error`r`n$($err | Out-String)")
            $msg = "Something went wrong: $msg"
        }
        $uiStep.Text = 'Problem'
        $uiStep.ForeColor = [Drawing.Color]::Firebrick
        $uiDetail.Text = 'Cleaning up...'
        try { Remove-Temp } catch {}
        $uiDetail.Text = ''
        $uiInfo.ForeColor = [Drawing.Color]::Firebrick
        $uiInfo.Text = ($msg -replace '^PROBLEM:\s*', '')
        if (Test-Path $Log) { $buttons['Open log'].Visible = $true }
    }
    $buttons['Close'].Visible = $true
}

if ($Gui) {
    $script:finished = $true
    while ($form.Visible) { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 }
}
