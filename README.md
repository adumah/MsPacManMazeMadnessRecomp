# Ms. Pac-Man Maze Madness — native Linux build

A native Linux build of *Ms. Pac-Man Maze Madness* (PlayStation, USA,
`SLUS-01018`) through static recompilation with
[PSXRecomp](https://github.com/mstan/psxrecomp).

This repository holds the **setup only**: configuration, a widescreen mod and
build scripts. It contains no game code or data. You build the game from
**your own copy** (see the [disclaimer](#disclaimer)).

## Features

- **Native code:** the game's programs are recompiled to native code ahead of
  time, including the two it loads from disc (`MAIN.EXE` for the 3D game,
  `CLASSIC.EXE` for classic mode).
- **A steady 60 fps on Linux**, including busy mazes full of ghosts and
  pellets (tested on a Steam Deck running CachyOS).
- **16:9 widescreen:** the 3D menus and mazes fill a widescreen display. FMV and
  flat 2D screens, including classic mode, stay 4:3, and the HUD keeps its
  original proportions. Billboard objects such as Ms. Pac-Man's body stay
  round. It's on by default and can be turned off (see
  [BUILD.md](BUILD.md#widescreen-and-smooth-motion)).
- **Smooth motion:** in-between frames are generated whenever the game draws
  fewer than 60 frames per second.
- **Gamepad support:** player 1 uses the first connected gamepad, including the
  Steam Deck's built-in controls, standalone or from Steam (Game Mode works
  too). Keyboard play is one setting away.
- **Memory card saves**, the 3D adventure, and classic mode.

## Building

Everything is built from your own copy of the game on your own machine. The
full guide, with requirements, the checksums your copy must match, controls
and troubleshooting, is **[BUILD.md](BUILD.md)**. In short:

1. Install the tools: `git cmake ninja python sdl3` and a C/C++ compiler.
2. `git clone --recursive https://github.com/adumah/MsPacManMazeMadnessRecomp`
3. Put the disc image you made from your own copy (one `.cue` and seven `.bin`
   files) into `disc/`.
4. Build the recompiler: `./psxrecomp/tools/ci/build_emitters.sh`
5. Generate the game code from your disc:
   `PSXRECOMP_AOT_WORKERS=2 python3 psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . --disc "disc/Ms. Pac-Man - Maze Madness.cue"`
6. Build:
   `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17`
   and `cmake --build build-release --target psx-runtime -j2`
7. Play: `./launch-mspacman.sh`

**Nintendo Switch:** a homebrew build for consoles running Atmosphère, built
the same way from your own copy, runs at full speed in 4:3. See
**[BUILD-SWITCH.md](BUILD-SWITCH.md)**.

## Repository layout

| Path | Purpose |
|---|---|
| `BUILD.md` | Step-by-step build from your disc |
| `BUILD-SWITCH.md`, `switch/`, `patches/`, `tools/package_switch.sh` | Nintendo Switch build |
| `game.toml` | Game, disc, runtime, widescreen and controller configuration |
| `aot/overlays.json` | Which disc programs are compiled natively, and how |
| `mods/` | Widescreen mod (16:9, round billboards) |
| `CMakeLists.txt` | Runtime build |
| `launch-mspacman.sh` | Starts the built game |
| `psxrecomp/` | Framework (git submodule, pinned) |
| `seeds/`, `symbols.toml`, `psx_symbols.h` | Boot program function seeds and symbol map |

Created at build time and never committed: `disc/` (your disc image),
`generated/` (C translated from it), `build-*/`, `.cache/`, `saves/`.

## Disclaimer

- **No game files are provided.** This repository does not contain, link to
  or distribute any part of *Ms. Pac-Man Maze Madness*: no disc images, game
  executables, game code, graphics, audio or other game data, and no BIOS.
- **You need your own physical copy.** Anyone who wants to build this port
  must own an original copy of the game (PlayStation, USA, `SLUS-01018`) and
  use a disc image made from that copy.
- **No extraction methods are provided.** This project does not provide,
  explain or support methods for dumping, ripping or extracting game discs.
  Requests for game files or disc images will be ignored.
- **Don't share what you build.** The disc image, the C code generated from
  it and the resulting executables are derived from the game. Keep them to
  yourself; don't upload or distribute them.

## Copyright and trademarks

*Ms. Pac-Man Maze Madness* is © 2000 Namco. *Ms. Pac-Man* and *Pac-Man* are
trademarks of Bandai Namco Entertainment Inc. PlayStation is a registered
trademark of Sony Interactive Entertainment Inc. All other trademarks belong
to their respective owners.

This is an independent, noncommercial fan project. It is not affiliated
with, endorsed by or sponsored by Bandai Namco, Sony Interactive
Entertainment, or any other rights holder. It distributes no copyrighted game
content: it only provides configuration and tools that let owners of the
original game run their own copy on modern hardware. If you are a rights
holder and have concerns, please open an issue.

## Credits

- **Namco**, the developers and publishers of *Ms. Pac-Man Maze Madness*, for
  the original game.
- **Sony Interactive Entertainment**, for the PlayStation platform the game
  was made for.
- **[PSXRecomp](https://github.com/mstan/psxrecomp)** by Matthew Stanley
  ([@mstan](https://github.com/mstan)) and contributors: the static
  recompiler, runtime, renderers, widescreen and smooth-motion features, and
  the project tooling this build is made with. Licensed under the
  [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0);
  see `psxrecomp/LICENSE` and `psxrecomp/THIRD_PARTY_ATTRIBUTION.md`.
- **[OpenBIOS](https://github.com/grumpycoders/pcsx-redux)** from PCSX-Redux
  (MIT): the free PlayStation BIOS bundled with every build.
- **[SDL](https://www.libsdl.org/)** (zlib license): window, input and audio.
