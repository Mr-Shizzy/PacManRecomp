# PacManRecomp Easy Build: builds the game on this PC from your own ROM.
# Started by "Build Pac-Man.bat". Downloads the free build tools (once), then
# translates your ROM and compiles the game into the "Game" folder.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'      # much faster downloads
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Root   = Split-Path -Parent $MyInvocation.MyCommand.Path
$Src    = Join-Path $Root 'source'
$Tools  = Join-Path $Root 'build-tools'
$Game   = Join-Path $Root 'Game'
$Pac    = Join-Path $Src 'PacManRecomp'
$RomCrc = '9E4E9CC2'                           # PRG + CHR, without the header

function Say($text)  { Write-Host $text -ForegroundColor Cyan }
function Fail($text) { Write-Host ''; Write-Host "PROBLEM: $text" -ForegroundColor Red; exit 1 }

# ---- 1. Find and check the ROM ------------------------------------------------
Say 'Step 1 of 4: looking for your Pac-Man ROM...'
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

# ---- 2. Build tools (downloaded once) -------------------------------------------
Say 'Step 2 of 4: getting the build tools (only the first time; about 245 MB)...'
$downloads = @(
    @{ Name = 'llvm-mingw'; Url = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip'; Sha = 'E3AD77D117A4BEA19A7A3B333341824D79A5A371004A10E25B8504E7B3047666' },
    @{ Name = 'cmake';      Url = 'https://github.com/Kitware/CMake/releases/download/v4.4.4/cmake-4.4.4-windows-x86_64.zip';               Sha = 'BACE36E94B31C68AB6FA295F26DFA11219E0701CF7C94B0284A7D1CB13DAC536' },
    @{ Name = 'ninja';      Url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip';                           Sha = '07FC8261B42B20E71D1720B39068C2E14FFCEE6396B76FB7A795FB460B78DC65' }
)
New-Item -ItemType Directory -Force $Tools | Out-Null
foreach ($d in $downloads) {
    $dir = Join-Path $Tools $d.Name
    if (Test-Path (Join-Path $dir '.done')) { continue }
    $zip = Join-Path $Tools "$($d.Name).zip"
    Say "  Downloading $($d.Name)..."
    try { Invoke-WebRequest -Uri $d.Url -OutFile $zip -UseBasicParsing }
    catch { Fail "Couldn't download $($d.Name). Check your internet connection and try again.`n$($_.Exception.Message)" }
    $sha = [Security.Cryptography.SHA256]::Create()
    $fs = [IO.File]::OpenRead($zip)
    try { $hash = -join ($sha.ComputeHash($fs) | ForEach-Object { $_.ToString('X2') }) } finally { $fs.Dispose() }
    if ($hash -ne $d.Sha) { Remove-Item $zip -Force; Fail "The $($d.Name) download didn't match its expected fingerprint; it was deleted. Try again later." }
    Say "  Unpacking $($d.Name)..."
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $dir)
    Remove-Item $zip -Force
    New-Item -ItemType File (Join-Path $dir '.done') | Out-Null
}
$llvmBin  = Join-Path (Get-ChildItem (Join-Path $Tools 'llvm-mingw') -Directory | Select-Object -First 1).FullName 'bin'
$cmakeBin = Join-Path (Get-ChildItem (Join-Path $Tools 'cmake') -Directory | Select-Object -First 1).FullName 'bin'
$bins = @($llvmBin, $cmakeBin, (Join-Path $Tools 'ninja'))
$env:PATH = ($bins -join ';') + ';' + $env:PATH

function Run($what, $exe, [string[]]$argv) {
    $log = Join-Path $Root 'build-log.txt'
    "`n==== $what`n> $exe $($argv -join ' ')" | Out-File $log -Append -Encoding utf8
    # Tools print warnings on stderr; only the exit code means failure.
    $ErrorActionPreference = 'Continue'
    & $exe @argv *>> $log
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { Fail "$what failed. Details are in build-log.txt (please include it if you ask for help)." }
}

# ---- 3. Translate the ROM and compile -------------------------------------------
Remove-Item (Join-Path $Root 'build-log.txt') -ErrorAction SilentlyContinue
Push-Location $Pac
try {
    Say 'Step 3 of 4: building the recompiler...'
    Run 'Configuring the recompiler' 'cmake' @('-S', '../nesrecomp/recompiler', '-B', '../nesrecomp/build/recompiler', '-G', 'Ninja', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_BUILD_TYPE=Release')
    Run 'Building the recompiler' 'cmake' @('--build', '../nesrecomp/build/recompiler')
    Say '  Translating your ROM into C...'
    Run 'Translating the ROM' '..\nesrecomp\build\recompiler\NESRecomp.exe' @('pacman.nes', '--game', 'game.toml')
    Say 'Step 4 of 4: compiling the game (this takes a few minutes)...'
    Run 'Configuring the game' 'cmake' @('-S', '.', '-B', 'build', '-G', 'Ninja', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_CXX_COMPILER=clang++', '-DCMAKE_BUILD_TYPE=Release')
    Run 'Compiling the game' 'cmake' @('--build', 'build')
} finally { Pop-Location }

# ---- 4. Put the game in the Game folder ---------------------------------------
New-Item -ItemType Directory -Force $Game | Out-Null
Copy-Item (Join-Path $Pac 'build\PacManRecomp.exe') $Game -Force
Copy-Item (Join-Path $Pac 'build\SDL2.dll') $Game -Force
New-Item -ItemType Directory -Force (Join-Path $Game 'assets') | Out-Null   # launcher fonts and pictures
Copy-Item (Join-Path $Pac 'build\assets\*') (Join-Path $Game 'assets') -Recurse -Force
Copy-Item $rom (Join-Path $Game 'pacman.nes') -Force
Set-Content -Path (Join-Path $Game 'rom.cfg') -Value 'pacman.nes' -NoNewline -Encoding ascii   # next to the exe
Copy-Item (Join-Path $Pac 'docs\MODDING.md') (Join-Path $Game 'Modding guide.md') -Force

Write-Host ''
Write-Host 'DONE! Your game is in the Game folder: double-click PacManRecomp.exe to play.' -ForegroundColor Green
Write-Host '(You can move the Game folder anywhere you like. The rest of this folder can be deleted.)'
