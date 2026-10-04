# build.ps1 - only for updating from Pac-Man Recomp 1.0.x. You don't need it.
#
# The 1.0.x in-game updater unzips a release, runs build.ps1 and installs
# what it leaves in .\Game. Since 1.1 the release holds the finished game
# (it sets itself up from your ROM the first time it starts), so this just
# hands the updater the "Pac-Man Recomp" folder next to it. It ships only in
# the release's "...for-1.0.x-updater-EasyBuild.zip" (make_package.sh).
$ErrorActionPreference = 'Stop'
$from = Join-Path $PSScriptRoot 'Pac-Man Recomp'
$to   = Join-Path $PSScriptRoot 'Game'
try {
    if (-not (Test-Path -LiteralPath (Join-Path $from 'PacManRecomp.exe'))) {
        throw 'the download is missing the game'
    }
    if (Test-Path -LiteralPath $to) { Remove-Item -LiteralPath $to -Recurse -Force }
    Copy-Item -LiteralPath $from $to -Recurse -Force
    exit 0
} catch {
    Write-Host "PROBLEM: $($_.Exception.Message)"
    exit 1
}
