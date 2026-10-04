# Pac-Man updater: started by the game (launcher "Update now?" -> Yes) after
# it closes itself. Shows a small window with a progress bar while it
# downloads the new Easy Build, builds it from the ROM in the game folder,
# swaps in the new program files and starts the game again. Only the program
# files are replaced: settings, keys, high scores and mods are never touched.
# Everything it downloads is deleted at the end.
# Built into the exe (CMake embeds this file); written to %TEMP% to run.
param(
    [string]$GameDir,
    [string]$ExeName,
    [string]$ZipUrl,
    [string]$Version,
    [int]$WaitPid
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Add-Type -AssemblyName System.Windows.Forms, System.Drawing, System.IO.Compression.FileSystem

$Work = Join-Path $env:LOCALAPPDATA 'PacManRecomp-update'
$Exe  = Join-Path $GameDir $ExeName
$script:cancel = $false
$script:shown  = 0          # progress shown, 0-100 (never goes back)
$script:proc   = $null

# ---- the window ----------------------------------------------------------------
[Windows.Forms.Application]::EnableVisualStyles()
$font = New-Object Drawing.Font('Segoe UI', 10)
$form = New-Object Windows.Forms.Form
$form.Text = 'Pac-Man update'
$form.ClientSize = New-Object Drawing.Size(520, 236)
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.MinimizeBox = $true
$form.StartPosition = 'CenterScreen'
$form.Font = $font
$form.TopMost = $true

function New-Label($y, $h, $text, $bold) {
    $l = New-Object Windows.Forms.Label
    $l.Location = New-Object Drawing.Point(20, $y)
    $l.Size = New-Object Drawing.Size(480, $h)
    $l.Text = $text
    if ($bold) { $l.Font = New-Object Drawing.Font('Segoe UI', 11, [Drawing.FontStyle]::Bold) }
    $form.Controls.Add($l)
    return $l
}
$null  = New-Label 14 24 "Updating Pac-Man to version $Version" $true
$step  = New-Label 44 22 'Starting...' $false
$bar   = New-Object Windows.Forms.ProgressBar
$bar.Location = New-Object Drawing.Point(20, 70)
$bar.Size = New-Object Drawing.Size(480, 24)
$bar.Maximum = 100
$form.Controls.Add($bar)
$detail = New-Label 98 20 '' $false
$detail.ForeColor = [Drawing.Color]::DimGray
$note  = New-Label 124 58 ("This takes a few minutes. Your settings, keys, high scores and mods are kept. " +
    "Temporary files (the download and the build tools, about 1 GB) are deleted when it's finished, " +
    "and the game starts again by itself.") $false
$note.ForeColor = [Drawing.Color]::DimGray
$btn = New-Object Windows.Forms.Button
$btn.Text = 'Cancel'
$btn.Location = New-Object Drawing.Point(410, 192)
$btn.Size = New-Object Drawing.Size(90, 30)
$btn.Add_Click({ $script:cancel = $true })
$form.Controls.Add($btn)
$form.Add_FormClosing({ param($s, $e) if (-not $script:done) { $e.Cancel = $true; $script:cancel = $true } })
$form.Show()

function Pump($ms) {
    $end = [DateTime]::Now.AddMilliseconds($ms)
    do { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 30 } while ([DateTime]::Now -lt $end)
    if ($script:cancel) { throw 'CANCELLED' }
}
function Show-Progress($pct, $text, $more) {
    $pct = [Math]::Max(0, [Math]::Min(100, [int]$pct))
    if ($pct -gt $script:shown) { $script:shown = $pct }
    $bar.Value = $script:shown
    if ($text -ne $null) { $step.Text = $text }
    if ($more -ne $null) { $detail.Text = $more }
    [Windows.Forms.Application]::DoEvents()
}

# A file another process is writing, read without locking it.
function Read-Shared($path) {
    if (-not (Test-Path -LiteralPath $path)) { return '' }
    try {
        $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
        try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Dispose() }
    } catch { return '' }
}

function Remove-Work {
    for ($i = 0; $i -lt 10 -and (Test-Path $Work); $i++) {
        try { Remove-Item $Work -Recurse -Force } catch { Start-Sleep -Seconds 1 }
    }
}

function Stop-Build {
    if ($script:proc -and -not $script:proc.HasExited) {
        & taskkill.exe /T /F /PID $script:proc.Id 2>&1 | Out-Null
    }
}

function Finish($ok, $message) {
    $script:done = $true
    if (-not $ok) {
        Stop-Build
        $btn.Enabled = $false
        if ($message -eq 'CANCELLED') {
            Show-Progress $script:shown 'Cancelled. Cleaning up...' ''
        } else {
            $log = Join-Path $GameDir 'update-log.txt'
            $extra = if (Test-Path $log) { "`n`nDetails are in:`n$log" } else { '' }
            [Windows.Forms.MessageBox]::Show($form,
                "The update didn't work: $message`n`nYour game was not changed.$extra",
                'Pac-Man update', 'OK', 'Warning') | Out-Null
            Show-Progress $script:shown 'Cleaning up...' ''
        }
    }
    Remove-Work
    if ($ok) { Show-Progress 100 "Done! Starting Pac-Man $Version..." 'Temporary files deleted.'; Start-Sleep -Milliseconds 1200 }
    if (Test-Path $Exe) { Start-Process -FilePath $Exe -WorkingDirectory $GameDir }
    $form.Close()
    Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue
    exit
}

# Build stages: the build script's own messages -> where the bar starts.
$stages = @(
    @{ Text = 'Step 1 of 4';             Pct = 8;  Say = 'Checking your ROM...' },
    @{ Text = 'Downloading llvm-mingw';  Pct = 10; Say = 'Downloading the build tools (the biggest part)...' },
    @{ Text = 'Unpacking llvm-mingw';    Pct = 28; Say = 'Unpacking the build tools...' },
    @{ Text = 'Downloading cmake';       Pct = 32; Say = 'Downloading the build tools...' },
    @{ Text = 'Unpacking cmake';         Pct = 35; Say = 'Unpacking the build tools...' },
    @{ Text = 'Downloading ninja';       Pct = 37; Say = 'Downloading the build tools...' },
    @{ Text = 'Step 3 of 4';             Pct = 40; Say = 'Building the recompiler...'; Log = 'Building the recompiler' },
    @{ Text = 'Translating your ROM';    Pct = 50; Say = 'Translating your ROM into C...' },
    @{ Text = 'Step 4 of 4';             Pct = 55; Say = 'Compiling the game...'; Log = 'Compiling the game' }
)

try {
    Show-Progress 1 'Waiting for the game to close...' ''
    if ($WaitPid) { for ($i = 0; $i -lt 300 -and (Get-Process -Id $WaitPid -ErrorAction SilentlyContinue); $i++) { Pump 100 } }
    Remove-Item (Join-Path $GameDir 'update-log.txt') -Force -ErrorAction SilentlyContinue

    Remove-Work
    New-Item -ItemType Directory -Force $Work | Out-Null
    $zip = Join-Path $Work 'update.zip'
    Show-Progress 2 'Downloading the new version...' ''
    if (Test-Path -LiteralPath $ZipUrl) { Copy-Item -LiteralPath $ZipUrl $zip }   # local test package
    else {
        $wc = New-Object Net.WebClient
        $wc.DownloadFileAsync([Uri]$ZipUrl, $zip)
        while ($wc.IsBusy) {
            Pump 200
            if (Test-Path $zip) { Show-Progress 2 $null ('{0:N1} MB' -f ((Get-Item $zip).Length / 1MB)) }
        }
        if (-not (Test-Path $zip) -or (Get-Item $zip).Length -lt 1000) { throw "couldn't download the new version (no internet?)" }
    }
    Show-Progress 5 'Unpacking...' ''
    $src = Join-Path $Work 'src'
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

    # The release's own build script (checks the ROM, gets the tools, builds),
    # hidden; its messages and build log drive the progress bar.
    $out  = Join-Path $Work 'build-out.txt'
    $blog = Join-Path $root 'build-log.txt'
    $script:proc = Start-Process powershell -PassThru -WindowStyle Hidden -RedirectStandardOutput $out `
        -RedirectStandardError (Join-Path $Work 'build-err.txt') `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$(Join-Path $root 'build.ps1')`"")
    $null = $script:proc.Handle          # keeps ExitCode readable after exit
    $stage = -1; $stageAt = [DateTime]::Now
    while (-not $script:proc.HasExited) {
        Pump 400
        $text = Read-Shared $out
        for ($i = $stages.Count - 1; $i -gt $stage; $i--) {
            if ($text.Contains($stages[$i].Text)) { $stage = $i; $stageAt = [DateTime]::Now; break }
        }
        if ($stage -lt 0) { continue }
        $s = $stages[$stage]
        $next = if ($stage + 1 -lt $stages.Count) { $stages[$stage + 1].Pct } else { 95 }
        $pct = $s.Pct; $more = ''
        # Compiling: the build log's [done/total] counter, counted only after
        # this step's own heading in the log.
        $m = @()
        if ($s.Log) {
            $logText = Read-Shared $blog
            $at = $logText.LastIndexOf("==== $($s.Log)")
            if ($at -ge 0) { $m = [regex]::Matches($logText.Substring($at), '\[(\d+)/(\d+)\]') }
        }
        if ($m.Count -gt 0) {
            $last = $m[$m.Count - 1]
            $done = [int]$last.Groups[1].Value; $total = [int]$last.Groups[2].Value
            if ($total -gt 0) {
                $pct = $s.Pct + ($next - $s.Pct) * $done / $total
                $more = "$done of $total files"
                if ($s.Log -eq 'Compiling the game' -and $total - $done -le 3) {
                    $more += ' (the last ones are the biggest - almost there)'
                }
            }
        } else {
            # No counter here: creep forward slowly so it never looks stuck.
            $secs = ([DateTime]::Now - $stageAt).TotalSeconds
            $pct = $s.Pct + ($next - 1 - $s.Pct) * (1 - [Math]::Exp(-$secs / 40))
        }
        Show-Progress $pct $s.Say $more
    }
    $script:proc.WaitForExit()
    if ($script:proc.ExitCode -ne 0) {
        $why = ((Read-Shared $out) -split "`n" | Where-Object { $_ -match 'PROBLEM:' } | Select-Object -Last 1)
        if (Test-Path $blog) { Copy-Item $blog (Join-Path $GameDir 'update-log.txt') }
        if ($why) { throw ($why -replace '.*PROBLEM:\s*', '').Trim() }
        throw 'the build failed'
    }

    Show-Progress 95 'Installing...' ''
    $btn.Enabled = $false            # past the point of no return
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
            Pump 1000
        }
    }
    Show-Progress 97 'Deleting temporary files...' ''
    Finish $true ''
} catch {
    Finish $false $_.Exception.Message
}
