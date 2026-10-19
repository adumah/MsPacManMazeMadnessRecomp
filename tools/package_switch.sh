#!/usr/bin/env bash
# Stage the Switch SD card folder from a finished build-switch/ (BUILD-SWITCH.md
# step 7). The result is for your own console only: mspacman.nro contains code
# translated from your disc, so never share it or the folder.
#
#   tools/package_switch.sh [build dir] [output dir]
#
# Defaults: build-switch, build-switch/sdcard. Copy the output's contents to
# the root of the SD card: it holds switch/mspacman/. With --with-disc, the
# .cue and .bin files in disc/ are copied into switch/mspacman/disc/ as well.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
with_disc=0
args=()
for a in "$@"; do
    case "$a" in
        --with-disc) with_disc=1 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) args+=("$a") ;;
    esac
done
build="${args[0]:-$root/build-switch}"
out="${args[1]:-$build/sdcard}"
app="$out/switch/mspacman"

for f in mspacman.nro game.toml game_options.toml bios/openbios.bin assets/psxrecomp.png; do
    if [ ! -e "$build/$f" ]; then
        echo "package_switch: $build/$f is missing; build first (BUILD-SWITCH.md step 6)" >&2
        exit 1
    fi
done
if [ ! -d "$build/mods/bundled" ]; then
    echo "package_switch: $build/mods/bundled is missing; build first" >&2
    exit 1
fi

# Only what this script stages is replaced. Anything the game wrote on a
# previous run (saves/, logs, mods/state.toml) never lives in the output.
rm -rf "$app"
mkdir -p "$app/disc" "$app/bios" "$app/assets" "$app/mods"
cp "$build/mspacman.nro" "$build/game.toml" "$build/game_options.toml" "$app/"
cp "$build/bios/openbios.bin" "$app/bios/"
[ -e "$build/bios/OpenBIOS.LICENSE" ] && cp "$build/bios/OpenBIOS.LICENSE" "$app/bios/"
cp "$build/assets/psxrecomp.png" "$app/assets/"
cp -r "$build/mods/bundled" "$app/mods/"
[ -e "$build/mods/README.md" ] && cp "$build/mods/README.md" "$app/mods/"

if [ "$with_disc" = 1 ]; then
    shopt -s nullglob
    disc_files=("$root/disc/"*.cue "$root/disc/"*.bin)
    if [ ${#disc_files[@]} -eq 0 ]; then
        echo "package_switch: no .cue/.bin files in $root/disc" >&2
        exit 1
    fi
    # -L: disc/ may hold symlinks to the image elsewhere.
    cp -L "${disc_files[@]}" "$app/disc/"
fi

echo "Staged $app"
echo "Copy the contents of $out to the root of your SD card."
if [ "$with_disc" = 0 ]; then
    echo "Then put your .cue and seven .bin files in /switch/mspacman/disc/ on the card."
fi
