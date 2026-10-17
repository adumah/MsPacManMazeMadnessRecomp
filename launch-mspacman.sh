#!/bin/sh
# Launch Ms. Pac-Man Maze Madness (PSXRecomp, native AOT build).
# Add this script to Steam as a Non-Steam Game so Steam Input exposes the
# Steam Deck controls as a standard gamepad.
cd "$(dirname "$(readlink -f "$0")")/build-release" || exit 1
# The AOT code is linked in; don't compile overlay caches in the background
# while playing.
export PSX_OVERLAY_AUTOCOMPILE_OFF=1
exec ./MsPacManMazeMadness_Recompiled --no-launcher "$@"
