#!/bin/sh
# Make the release zip: the game, ready to play once it has a ROM.
#
# The exe is built here from the three repos' committed sources only (git
# archive: no ROM, no generated/), with PACMAN_GAME_DLL=ON, so it holds no
# game code. On the player's PC it makes game.dll from their own ROM, with
# the recompiler and TinyCC shipped in its tools\ folder.
#
# usage (from PacManRecomp, with ../nesrecomp and ../recomp-ui beside it, and
# clang, CMake and Ninja on the PATH):
#   sh easybuild/make_package.sh 1.1.0 [out_dir]
set -e
ver=$1
[ -n "$ver" ] || { echo "usage: $0 <version> [out_dir]"; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
pac=$(dirname "$here")

# The release must match the version built into the game (CMakeLists.txt
# project VERSION): the updater compares the two, and a mismatch would offer
# the same update forever. TEST_PACKAGE=1 skips this (updater tests).
built=$(sed -n 's/^project(PacManRecomp VERSION \([0-9.]*\).*/\1/p' "$pac/CMakeLists.txt")
if [ "$ver" != "$built" ] && [ -z "$TEST_PACKAGE" ]; then
    echo "version $ver doesn't match CMakeLists.txt's project VERSION $built"; exit 1
fi
top=$(dirname "$pac")
out=${2:-$pac/release}
mkdir -p "$out" && out=$(cd "$out" && pwd)
out_w=$(cd "$out" && pwd -W)   # for Windows' tar
# Two zips:
# - $name.zip, for players and the 1.1+ updater: the game's files at the top,
#   so unzipping gives one folder with PacManRecomp.exe in it;
# - $old.zip, only for the 1.0.x updaters, which look for a release file
#   ending in "EasyBuild.zip" holding build.ps1 (see easybuild/build.ps1).
name="PacManRecomp-$ver-Windows"
old="PacManRecomp-$ver-for-1.0.x-updater-EasyBuild"
work="$out/work"
game="$work/stage"

# TinyCC 0.9.27 (LGPL), from its official page; its source goes along.
tcc_url=http://download.savannah.gnu.org/releases/tinycc
tcc_bin=tcc-0.9.27-win64-bin.zip
tcc_src=tcc-0.9.27.tar.bz2
cache="$out/cache"
mkdir -p "$cache"
for f in $tcc_bin $tcc_src; do
    [ -f "$cache/$f" ] || curl -sSfL -o "$cache/$f" "$tcc_url/$f"
done
(cd "$cache" && sha256sum -c - <<EOF
34a721949a2583fdff725312da092fa0f5f1f284b702e6f811c6954714faabb2 *$tcc_bin
de23af78fca90ce32dff2dd45b3432b2334740bb9bb7b05bf60fdbfc396ceb9c *$tcc_src
EOF
)

rm -rf "$out/$name.zip" "$out/$old.zip" "$work"
mkdir -p "$work/src" "$game"
for repo in PacManRecomp nesrecomp recomp-ui; do
    mkdir -p "$work/src/$repo"
    git -C "$top/$repo" archive HEAD | tar -x -C "$work/src/$repo"
done
src="$work/src"
[ ! -e "$src/PacManRecomp/generated" ] || { echo "generated/ is committed?"; exit 1; }

# The recompiler, and the game exe without the game code.
cmake -S "$src/nesrecomp/recompiler" -B "$work/rc" -G Ninja \
    -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release > "$work/rc.log"
cmake --build "$work/rc" >> "$work/rc.log"
cmake -S "$src/PacManRecomp" -B "$work/game" -G Ninja -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DPACMAN_GAME_DLL=ON \
    > "$work/game.log"
cmake --build "$work/game" >> "$work/game.log"

cp "$work/game/PacManRecomp.exe" "$work/game/SDL2.dll" "$game/"
cp -r "$work/game/assets" "$game/"
cp "$src/PacManRecomp/docs/MODDING.md" "$game/Modding guide.md"

t="$game/tools"
mkdir -p "$t/gamedll" "$t/include" "$t/tcc"
cp "$work/rc/NESRecomp.exe" "$src/PacManRecomp/game.toml" "$t/"
cp "$src/PacManRecomp/src/gamedll/game_dll.c" "$src/PacManRecomp/src/gamedll/game_dll_abi.h" "$t/gamedll/"
for h in nes_runtime.h coroutine.h mod_function_hooks.h; do
    cp "$src/nesrecomp/runner/include/$h" "$t/include/"
done
mkdir -p "$work/tcc"
unzip -q "$cache/$tcc_bin" -d "$work/tcc"
cp -r "$work/tcc/tcc/tcc.exe" "$work/tcc/tcc/libtcc.dll" "$work/tcc/tcc/include" \
    "$work/tcc/tcc/lib" "$t/tcc/"
cp "$cache/$tcc_src" "$t/tcc/"
tar -xjf "$cache/$tcc_src" -C "$work/tcc" tcc-0.9.27/COPYING

l="$game/licenses"
mkdir -p "$l"
cp "$src/PacManRecomp/LICENSE" "$l/PacManRecomp.txt"
cp "$src/nesrecomp/LICENSE" "$l/NESRecomp.txt"
cp "$src/recomp-ui/LICENSE" "$l/recomp-ui.txt"
cp "$src/nesrecomp/runner/external/SDL2/COPYING.txt" "$l/SDL2.txt"
cp "$src/recomp-ui/src/third_party/imgui/LICENSE.txt" "$l/Dear ImGui.txt"
cp "$src/recomp-ui/assets/common/fonts/NOTICE.md" "$l/fonts.md"
cp "$src/recomp-ui/assets/common/img/NOTICE.md" "$l/images.md"
cp "$work/tcc/tcc-0.9.27/COPYING" "$l/TinyCC (LGPL).txt"

cp "$here/READ ME FIRST.txt" "$game/"

# Windows' own tar writes a standard zip (Compress-Archive uses backslashes).
zip() { /c/Windows/System32/tar.exe -a -c -f "$@"; }

# The players' zip: the game's files at the top.
(cd "$game" && zip "$out_w/$name.zip" *)

# The 1.0.x updaters' zip: <top>\build.ps1 + <top>\Pac-Man Recomp\.
mkdir -p "$work/old/$old"
cp "$here/build.ps1" "$work/old/$old/"
cp -r "$game" "$work/old/$old/Pac-Man Recomp"
(cd "$work/old" && zip "$out_w/$old.zip" "$old")

rm -rf "$work"
echo "made $out/$name.zip"
echo " and $out/$old.zip"
