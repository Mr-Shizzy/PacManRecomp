#!/bin/sh
# Make the Easy Build zip for a GitHub release: the three repos' committed
# sources (no ROM, no generated code, no builds) plus the easybuild/ files.
#
# usage (from PacManRecomp, with ../nesrecomp and ../recomp-ui beside it):
#   sh easybuild/make_package.sh 1.0 [out_dir]
set -e
ver=$1
[ -n "$ver" ] || { echo "usage: $0 <version> [out_dir]"; exit 1; }
here=$(cd "$(dirname "$0")" && pwd)
pac=$(dirname "$here")
top=$(dirname "$pac")
out=${2:-$pac/release}
name="PacManRecomp-$ver-EasyBuild"
stage="$out/$name"

rm -rf "$stage" "$out/$name.zip"
mkdir -p "$stage/source"
for repo in PacManRecomp nesrecomp recomp-ui; do
    mkdir -p "$stage/source/$repo"
    git -C "$top/$repo" archive HEAD | tar -x -C "$stage/source/$repo"
done
rm -rf "$stage/source/PacManRecomp/easybuild"           # it's at the top level
cp "$here/Build Pac-Man.bat" "$here/build.ps1" "$here/READ ME FIRST.txt" "$stage/"

(cd "$out" && powershell -NoProfile -Command \
    "Compress-Archive -Path '$name' -DestinationPath '$name.zip' -Force")
rm -rf "$stage"
echo "made $out/$name.zip"
