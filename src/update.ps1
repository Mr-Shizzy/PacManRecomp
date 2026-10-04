# Pac-Man updater: started by the game (launcher "Update now?" -> Yes) after
# it closes itself. Shows a small window with a progress bar while it
# downloads the new Easy Build, builds it from the ROM in the game folder,
# swaps in the new program files and starts the game again. Only the program
# files are replaced: settings, keys, high scores and mods are never touched.
# It works in a temporary folder inside the game folder (update-temp, about
# 1 GB at most), which is deleted at the end.
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

$Work = Join-Path $GameDir 'update-temp'    # on the game's own drive
$Exe  = Join-Path $GameDir $ExeName
$script:cancel   = $false
$script:noCancel = $false   # set once installing starts (past the point of no return)
$script:changed  = $false   # the game files were left half replaced
$script:shown    = 0        # progress shown, 0-100 (never goes back)
$script:proc     = $null
$script:wc       = $null
# While this exists the game refuses to start (updater.c), so a copy opened
# mid-update can't lock the files being replaced. Released before restarting.
$script:mutex = New-Object Threading.Mutex($false, 'Local\PacManRecomp-update')

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
    "It needs up to 1 GB of temporary files (in the game folder), which are deleted when it's finished, " +
    "and the game starts again by itself.") $false
$note.ForeColor = [Drawing.Color]::DimGray
$btn = New-Object Windows.Forms.Button
$btn.Text = 'Cancel'
$btn.Location = New-Object Drawing.Point(410, 192)
$btn.Size = New-Object Drawing.Size(90, 30)
$btn.Add_Click({ $script:cancel = $true })
$form.Controls.Add($btn)
$form.Add_FormClosing({ param($s, $e)
    if (-not $script:done) { $e.Cancel = $true; if (-not $script:noCancel) { $script:cancel = $true } }
})
$form.Show()

function Pump($ms) {
    $end = [DateTime]::Now.AddMilliseconds($ms)
    do { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 30 } while ([DateTime]::Now -lt $end)
    if ($script:cancel -and -not $script:noCancel) { throw 'CANCELLED' }
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
    if ($script:wc -and $script:wc.IsBusy) {
        $script:wc.CancelAsync()
        for ($i = 0; $i -lt 50 -and $script:wc.IsBusy; $i++) { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 100 }
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
            $state = if ($script:changed) {
                'Some game files may have been replaced. To repair the game, build it again with the Easy Build.'
            } else { 'Your game was not changed.' }
            [Windows.Forms.MessageBox]::Show($form,
                "The update didn't work: $message`n`n$state$extra",
                'Pac-Man update', 'OK', 'Warning') | Out-Null
            Show-Progress $script:shown 'Cleaning up...' ''
        }
    }
    Remove-Work
    if ($ok) { Show-Progress 100 "Done! Starting Pac-Man $Version..." 'Temporary files deleted.'; Start-Sleep -Milliseconds 1200 }
    if ($script:mutex) { $script:mutex.Dispose(); $script:mutex = $null }   # let the game start
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
    # Windows paths stop at 259 characters unless long paths are switched on;
    # the build's deepest file is ~150 below update-temp (see updater.c).
    $longPaths = $false
    try {
        $longPaths = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' `
                      -Name LongPathsEnabled -ErrorAction Stop).LongPathsEnabled -eq 1
    } catch {}
    if (-not $longPaths -and $GameDir.TrimEnd([char]92).Length -gt 96) {
        throw ("the game folder's path is too long for Windows to build the update inside it. " +
               "Move the game folder somewhere with a shorter path, for example C:\Games\Pac-Man, " +
               "then try again")
    }
    $freeGB = $null
    try {
        $drive = New-Object IO.DriveInfo([IO.Path]::GetPathRoot($GameDir))
        $freeGB = $drive.AvailableFreeSpace / 1GB
        $driveName = $drive.Name.TrimEnd([char]92)          # "D:\" -> "D:"
    } catch {}                                              # network folder: can't tell
    if ($freeGB -ne $null -and $freeGB -lt 2) {
        throw ("there isn't enough free space on drive {0} (where the game is). Updating needs " +
               "about 2 GB free there while it works; you have {1:N1} GB.") -f $driveName, $freeGB
    }
    New-Item -ItemType Directory -Force $Work | Out-Null
    $zip = Join-Path $Work 'update.zip'
    Show-Progress 2 'Downloading the new version...' ''
    if (Test-Path -LiteralPath $ZipUrl) { Copy-Item -LiteralPath $ZipUrl $zip }   # local test package
    else {
        $script:wc = New-Object Net.WebClient
        $script:wc.DownloadFileAsync([Uri]$ZipUrl, $zip)
        while ($script:wc.IsBusy) {
            Pump 200
            if (Test-Path $zip) { Show-Progress 2 $null ('{0:N1} MB' -f ((Get-Item $zip).Length / 1MB)) }
        }
        if (-not (Test-Path $zip) -or (Get-Item $zip).Length -lt 1000) { throw "couldn't download the new version (no internet?)" }
    }
    # Unpack straight into update-temp, without the zip's top folder, so the
    # build's paths stay as short as possible.
    Show-Progress 5 'Unpacking...' ''
    $za = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $names = @($za.Entries | ForEach-Object { $_.FullName })
        $top = ($names[0] -split '[/\\]')[0] + '/'
        $strip = @($names | Where-Object { -not $_.StartsWith($top) }).Count -eq 0
        $n = 0
        foreach ($e in $za.Entries) {
            $rel = if ($strip) { $e.FullName.Substring($top.Length) } else { $e.FullName }
            if (-not $rel -or $rel -match '(^|[/\\])\.\.([/\\]|$)') { continue }
            $dest = Join-Path $Work $rel
            if ($rel.EndsWith('/')) { New-Item -ItemType Directory -Force $dest | Out-Null; continue }
            $dir = Split-Path $dest -Parent
            if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dest, $true)
            if ((++$n % 200) -eq 0) { Pump 0 }
        }
    } finally { $za.Dispose() }
    Remove-Item $zip -Force
    $root = $Work
    if (-not (Test-Path (Join-Path $root 'build.ps1'))) { throw 'the download has no build script' }

    # The ROM the game already uses (rom.cfg, else the .nes in the game folder).
    $rom = $null
    $cfg = Join-Path $GameDir 'rom.cfg'
    if (Test-Path $cfg) {
        $p = "$(Get-Content $cfg -Raw)".Trim()
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
    $script:proc = Start-Process (Join-Path $PSHOME 'powershell.exe') -PassThru -WindowStyle Hidden -RedirectStandardOutput $out `
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

    # Install: every program file the new build made (exe, DLLs, launcher
    # assets, guide...), so a later version can add files. Never the
    # player's own: the ROM, rom.cfg, *.ini (settings, keys, scores), mods\.
    Show-Progress 95 'Installing...' ''
    $script:noCancel = $true; $btn.Enabled = $false      # past the point of no return
    $new = Join-Path $root 'Game'
    $plan = @()
    foreach ($f in Get-ChildItem $new -Recurse -File) {
        $rel = $f.FullName.Substring($new.Length + 1)
        if ($rel -match '\.(nes|ini)$' -or $rel -eq 'rom.cfg' -or $rel -match '^mods[\\/]') { continue }
        $dest = if ($rel -eq 'PacManRecomp.exe') { $Exe } else { Join-Path $GameDir $rel }
        $plan += @{ From = $f.FullName; To = $dest; Back = (Join-Path $Work "backup\$($plan.Count)"); Had = (Test-Path -LiteralPath $dest) }
    }
    # Back up what gets replaced, so a failure can put the old game back.
    New-Item -ItemType Directory -Force (Join-Path $Work 'backup') | Out-Null
    foreach ($c in $plan) { if ($c.Had) { Copy-Item -LiteralPath $c.To $c.Back -Force } }
    for ($try = 1; ; $try++) {
        try {
            foreach ($c in $plan) {
                $dir = Split-Path $c.To -Parent
                if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
                Copy-Item -LiteralPath $c.From $c.To -Force
                $c.Done = $true
            }
            break
        } catch {
            if ($try -lt 10) { Pump 1000; continue }
            foreach ($c in $plan) {           # put back what was replaced
                if (-not $c.Done) { continue }
                try {
                    if ($c.Had) { Copy-Item -LiteralPath $c.Back $c.To -Force }
                    elseif (Test-Path -LiteralPath $c.To) { Remove-Item -LiteralPath $c.To -Force }
                } catch { $script:changed = $true }
            }
            throw "couldn't replace the game files (is the game still open?)"
        }
    }
    Show-Progress 97 'Deleting temporary files...' ''
    Finish $true ''
} catch {
    Finish $false $_.Exception.Message
}
