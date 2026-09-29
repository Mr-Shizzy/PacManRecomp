#!/bin/sh
# Run an input-script test in an isolated copy of the game, so tests never
# read or write the player's settings (config.ini / pacman_options.ini next
# to build/PacManRecomp.exe).
#
# usage: tests/run.sh <script.txt> ["Key = value" ...]
#   Each extra argument becomes a line of the test's pacman_options.ini
#   ("config:Key = value" goes to its config.ini instead);
#   with none, the test runs on default settings.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
run="$here/run"

mkdir -p "$run"
rm -f "$run/config.ini" "$run/pacman_options.ini" "$run/pacman_scores.ini" "$run/keybinds.ini"
cp "$root/build/PacManRecomp.exe" "$root/build/SDL2.dll" "$run/"

script=$1
shift
for line in "$@"; do
    case "$line" in
        config:*) printf '%s\n' "${line#config:}" >> "$run/config.ini" ;;   # runner setting
        *)        printf '%s\n' "$line" >> "$run/pacman_options.ini" ;;
    esac
done

exec "$run/PacManRecomp.exe" "$root/pacman.nes" --script "$here/$(basename "$script")"
