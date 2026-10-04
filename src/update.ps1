# Pac-Man updater: started by the game (launcher "Update now?" -> Yes) after
# it closes itself. Shows a small window with a progress bar while it
# downloads the new release ("...-Windows.zip"), swaps in its program files
# and starts the game again, which then sets itself up from the ROM
# (a few seconds; see game_dll_host.c). Only the program files are replaced:
# settings, keys, high scores and mods are never touched. It works in a
# temporary folder inside the game folder (update-temp, about 30 MB), which
# is deleted at the end.
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
$note  = New-Label 124 58 ("This usually takes less than a minute. Your settings, keys, high scores and mods are kept. " +
    "Its temporary files (in the game folder) are deleted when it's finished, and the game starts " +
    "again by itself.") $false
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

function Remove-Work {
    for ($i = 0; $i -lt 10 -and (Test-Path $Work); $i++) {
        try { Remove-Item $Work -Recurse -Force } catch { Start-Sleep -Seconds 1 }
    }
}

function Finish($ok, $message) {
    $script:done = $true
    if (-not $ok) {
        $btn.Enabled = $false
        if ($message -eq 'CANCELLED') {
            Show-Progress $script:shown 'Cancelled. Cleaning up...' ''
        } else {
            $log = Join-Path $GameDir 'update-log.txt'
            $extra = if (Test-Path $log) { "`n`nDetails are in:`n$log" } else { '' }
            $state = if ($script:changed) {
                'Some game files may have been replaced. To repair the game, download it again from its GitHub page.'
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

try {
    Show-Progress 1 'Waiting for the game to close...' ''
    if ($WaitPid) { for ($i = 0; $i -lt 300 -and (Get-Process -Id $WaitPid -ErrorAction SilentlyContinue); $i++) { Pump 100 } }
    Remove-Item (Join-Path $GameDir 'update-log.txt') -Force -ErrorAction SilentlyContinue

    Remove-Work
    # Windows paths stop at 259 characters unless long paths are switched on;
    # the release's deepest file is ~60 below update-temp (see updater.c).
    $longPaths = $false
    try {
        $longPaths = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' `
                      -Name LongPathsEnabled -ErrorAction Stop).LongPathsEnabled -eq 1
    } catch {}
    if (-not $longPaths -and $GameDir.TrimEnd([char]92).Length -gt 180) {
        throw ("the game folder's path is too long for Windows to unpack the update inside it. " +
               "Move the game folder somewhere with a shorter path, for example C:\Games\Pac-Man, " +
               "then try again")
    }
    $freeMB = $null
    try {
        $drive = New-Object IO.DriveInfo([IO.Path]::GetPathRoot($GameDir))
        $freeMB = $drive.AvailableFreeSpace / 1MB
        $driveName = $drive.Name.TrimEnd([char]92)          # "D:\" -> "D:"
    } catch {}                                              # network folder: can't tell
    if ($freeMB -ne $null -and $freeMB -lt 200) {
        throw ("there isn't enough free space on drive {0} (where the game is). Updating needs " +
               "about 200 MB free there; you have {1:N0} MB.") -f $driveName, $freeMB
    }
    New-Item -ItemType Directory -Force $Work | Out-Null
    $zip = Join-Path $Work 'update.zip'
    Show-Progress 2 'Downloading the new version...' ''
    if (Test-Path -LiteralPath $ZipUrl) { Copy-Item -LiteralPath $ZipUrl $zip }   # local test package
    else {
        try {
            $resp = [Net.WebRequest]::Create($ZipUrl).GetResponse()
            $total = $resp.ContentLength
            $in = $resp.GetResponseStream()
            $file = [IO.File]::Create($zip)
            try {
                $buf = New-Object byte[] 65536
                $got = 0; $last = [DateTime]::MinValue
                while (($n = $in.Read($buf, 0, $buf.Length)) -gt 0) {
                    $file.Write($buf, 0, $n); $got += $n
                    if (([DateTime]::Now - $last).TotalMilliseconds -gt 150) {
                        $last = [DateTime]::Now
                        $pct = if ($total -gt 0) { 2 + 78 * $got / $total } else { 2 }
                        $of = if ($total -gt 0) { ' of {0:N1} MB' -f ($total / 1MB) } else { ' MB' }
                        Show-Progress $pct $null (('{0:N1}' -f ($got / 1MB)) + $of)
                        if ($script:cancel) { throw 'CANCELLED' }
                    }
                }
            } finally { $file.Dispose(); $in.Dispose(); $resp.Dispose() }
        } catch {
            if ($_.Exception.Message -eq 'CANCELLED') { throw 'CANCELLED' }
            throw "couldn't download the new version (no internet?)"
        }
        if ((Get-Item $zip).Length -lt 1000) { throw "couldn't download the new version (no internet?)" }
    }
    # Unpack straight into update-temp, without the zip's top folder.
    Show-Progress 80 'Unpacking...' ''
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
    $new = $Work                          # the zip holds the game's files at its top
    if (-not (Test-Path -LiteralPath (Join-Path $new 'PacManRecomp.exe'))) {
        throw 'the download is missing the game'
    }

    # Install: every program file of the new version (exe, DLLs, launcher
    # assets, tools, guide...), so a later version can add files. Never the
    # player's own: the ROM, rom.cfg, *.ini (settings, keys, scores), mods\.
    # The old game.dll stays: the new exe sees it was made for another
    # version and makes a new one from the ROM when it starts.
    Show-Progress 85 'Installing...' ''
    $script:noCancel = $true; $btn.Enabled = $false      # past the point of no return
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
