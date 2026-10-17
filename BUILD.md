# Building Ms. Pac-Man Maze Madness for Linux

This repository contains the **setup** for a native Linux build of
*Ms. Pac-Man Maze Madness* (PlayStation, USA, `SLUS-01018`) made with
[PSXRecomp](https://github.com/mstan/psxrecomp). It does **not** contain the
game. You build the game yourself from **your own copy**: the build reads the
game's code from a disc image of your copy and translates it to native code on
your machine. No game files are provided, and neither are methods for making
a disc image (see [Legal](#legal)).

Nothing produced from your disc (`disc/`, `generated/`, `build-*/`, `.cache/`)
should ever be shared or committed.

The result runs at a steady 60 fps on Linux, including gameplay with many
ghosts on screen, saving, and classic mode (tested on a Steam Deck running
CachyOS).

## 1. What you need

**A disc image made from your own physical copy** of the USA release
(`SLUS-01018`), as a set of one `.cue` and seven `.bin` files, named exactly:

```
Ms. Pac-Man - Maze Madness.cue
Ms. Pac-Man - Maze Madness (Track 1).bin
...
Ms. Pac-Man - Maze Madness (Track 7).bin
```

The build checks Track 1 against these checksums (also in `game.toml`) and
stops if your image differs:

| | |
|---|---|
| Size | 298,348,848 bytes |
| MD5 | `8465d1516bc6e0e7ade19a30acecc60a` |
| SHA-1 | `cdf130d0807f498b1705e423bea3104993263b62` |

**A Linux machine** with about 16 GB of RAM (12 GB of RAM plus swap also
works with the memory settings below) and about 8 GB of free disk space.

**Tools and libraries.** Tested on CachyOS (Arch-based) with GCC 16, CMake 4.4,
Ninja 1.13 and Python 3.14:

```sh
# Arch / CachyOS / SteamOS developer mode (tested)
sudo pacman -S --needed base-devel git cmake ninja python sdl3 zlib openssl \
    brotli zstd libglvnd libx11
```

```sh
# Debian / Ubuntu (likely equivalents, untested)
sudo apt install build-essential git cmake ninja-build python3 zlib1g-dev \
    libssl-dev libbrotli-dev libzstd-dev libgl-dev libx11-dev
```

Python 3.11 or newer is required. SDL3 is optional: when your system has no
SDL3 package, the build downloads the pinned SDL3 release automatically.

## 2. Get the source

```sh
git clone --recursive <this repository's URL> MsPacManMazeMadnessRecomp
cd MsPacManMazeMadnessRecomp
```

If you cloned without `--recursive`, run `git submodule update --init --recursive`.

## 3. Put your disc in `disc/`

Copy (or symlink) the `.cue` and all seven `.bin` files into `disc/`:

```sh
cp /path/to/your/image/"Ms. Pac-Man - Maze Madness"* disc/
```

## 4. Build the recompiler

```sh
./psxrecomp/tools/ci/build_emitters.sh
```

This builds `build-recompiler/psxrecomp-game` (and `psxrecomp-bios`).

## 5. Generate the game code from your disc

```sh
PSXRECOMP_AOT_WORKERS=2 python3 psxrecomp/psxrecomp_cli.py generate \
    --config game.toml --project-root . \
    --disc "disc/Ms. Pac-Man - Maze Madness.cue"
```

This verifies your disc image, extracts the boot program, translates it to C, and
then translates the two programs the game loads from disc (`MAIN.EXE` for
the 3D game, `CLASSIC.EXE` for classic mode) as described by
`aot/overlays.json`. Expect a few minutes (about 2 on a Steam Deck) and
about 360 MB of C in `generated/`.

`PSXRECOMP_AOT_WORKERS=2` limits how many programs are translated at once.
The default (all CPU cores but two) can run out of memory. Use `1` if you
have less than 16 GB of RAM.

## 6. Build the game

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17
cmake --build build-release --target psx-runtime -j2
```

`-j2` matters: the largest generated file is about 220 MB of C, and compiling
several of those at once at `-O3` can exhaust memory. Expect about 10
minutes on a Steam Deck.
`-DCMAKE_CXX_STANDARD=17` is needed with GCC 16 and harmless elsewhere.

The build goes quiet for several minutes at
`Building C object .../generated/overlays_static_0003.c.o`: that single file
is about 220 MB of C. It isn't stuck (`top` shows a busy `cc1`). If you
interrupt it, run the same command again; finished files are kept.

The game is `build-release/MsPacManMazeMadness_Recompiled`.

## 7. Play

```sh
./launch-mspacman.sh
```

The game finds your disc in `disc/`. If it can't, it opens a file picker.
Memory card saves go to `saves/`.

### Controls

Player 1 uses the first connected gamepad (`p1_device = "auto"` in
`game.toml`). On a Steam Deck, the built-in controls work directly, both
standalone and when launched from Steam.

| Gamepad (Xbox layout / Steam Deck) | PlayStation |
|---|---|
| D-pad, left stick | D-pad |
| A / B / X / Y | Cross / Circle / Square / Triangle |
| Menu (☰) | Start (pause) |
| View (⧉) | Select |
| LB / RB / LT / RT | L1 / R1 / L2 / R2 |

To play with the keyboard instead, set `p1_device = "keyboard"` in
`game.toml` and rebuild (or edit `build-release/game.toml` directly).
Keyboard defaults: arrows = D-pad, X / S / Z / A = Cross / Circle / Square /
Triangle, Return = Start, Right Shift = Select.

### Optional: Steam or desktop menu

To launch it from your desktop's application menu, create
`~/.local/share/applications/mspacman-maze-madness-recomp.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Ms. Pac-Man Maze Madness (Recompiled)
Exec=/full/path/to/MsPacManMazeMadnessRecomp/launch-mspacman.sh
Icon=/full/path/to/MsPacManMazeMadnessRecomp/build-release/assets/psxrecomp.png
Terminal=false
Categories=Game;
```

In Steam: **Add a Game > Add a Non-Steam Game**, then pick it from the list
(or browse to `launch-mspacman.sh`). That also makes it available in the Steam
Deck's Game Mode.

## Widescreen and smooth motion

Both are on by default:

- **16:9 widescreen** ("squash" mode, the classic GTE widescreen hack): the 3D
  menus and mazes fill a 16:9 screen. FMV and flat 2D screens, including
  classic mode, stay 4:3, and the HUD keeps its original proportions. It comes
  from this repository's mod, `mods/preloaded/packages/mpmm.enhancement.widescreen`,
  which has two features:
  - `widescreen`: selects 16:9.
  - `squash-billboards`: keeps billboard objects, such as Ms. Pac-Man's body,
    round instead of stretched.
- **Smooth motion** (frame generation): when the game draws fewer than 60
  frames per second, the renderer draws the in-between frames from recorded
  data. No game code runs again.

The game reads `build-release/game.toml` and `build-release/mods/state.toml`.
`game.toml` is copied from the project root on every build, so edit the root
copy for permanent changes and rebuild, or edit `build-release/game.toml` to
try something right away.

**Turn off widescreen** (back to the original 4:3 picture): add this to
`build-release/mods/state.toml` (create the file if it doesn't exist):

```toml
format_version = 2

[[package]]
id = "mpmm.enhancement.widescreen"

[[feature]]
package_id = "mpmm.enhancement.widescreen"
id = "widescreen"
enabled = false
```

**Native-wide instead of squash**: set `native_wide = true` under
`[widescreen]` in `game.toml`, and disable `squash-billboards` the same way as
above (otherwise billboards come out too narrow). Native-wide doesn't stretch
any 2D art, but some edges show small rendering glitches (see Known issues).

**Turn off smooth motion**: set `render_thread = false` and
`frame_generation = false` under `[video]` in `game.toml`.

## Troubleshooting

**The build or generate step gets killed, or the machine freezes.** It ran
out of memory. Lower `PSXRECOMP_AOT_WORKERS` to `1` and build with `-j1`. To
make sure only the build gets killed rather than your desktop, run a step
inside a memory cap:

```sh
systemd-run --user --scope -p MemoryMax=10G -p MemorySwapMax=4G <command>
```

**`FATAL: STALE RECOMPILER BINARY`.** The recompiler was built from a
different framework version than the one checked out. Rebuild it with
`./psxrecomp/tools/ci/build_emitters.sh`, or pass `--force-emitters` to
`generate`.

**Disc verification fails.** Your disc image doesn't match the USA release
(see the digests above). Other releases need their own `game.toml` and
`aot/overlays.json`.

**The gamepad does nothing.** Check that `game.toml` has
`p1_device = "auto"`. Gamepads connected after launch are picked up
automatically.

## What this repository adds to the framework defaults

- `aot/overlays.json`: which programs on the disc get compiled natively.
  `CLASSIC.EXE` has three `excluded_ranges` (its strings, tables, maze layouts
  and assets: data that would otherwise be decoded as code), each pinned by a
  SHA-256 of the original bytes, plus `strict_bounds`. `MAIN.EXE` lists 33
  extra `entries`: functions the game reaches only through pointers it sets at
  runtime, which static analysis can't find. Without them the ghost and pellet
  logic ran in the interpreter at about 35 fps.
  The exclusions also keep code generation small: without them the recompiler
  decodes CLASSIC.EXE's data as code and runs out of memory.
- `CMakeLists.txt`: links the generated overlay code
  (`GAME_OVERLAY_STATIC_C`).
- `game.toml`: relative disc path, gamepad as player 1, smooth motion, and
  widescreen: `[widescreen]` (squash mode, the 3D-game detector
  `gte_game_mode`, `auto_ui_squash` for the HUD), `[widescreen.cull]` (the
  game's edge culls, widened for native-wide mode only), and
  `[recompiler] mod_instruction_sites` (the billboard hooks below).
- `mods/src/mpmm_widescreen.c` and
  `mods/preloaded/packages/mpmm.enhancement.widescreen`: the widescreen mod.
  It selects 16:9, and its billboard hooks fix Ms. Pac-Man's body in squash
  mode: the game projects only the billboard's centre through the GTE, which
  squash compresses, and computes its width on the CPU, which it doesn't. The
  hooks scale that width at the builders' corner math (`0x80049F3C`,
  `0x80088BD4`, `0x8008A534`) by the live squash factor.
- `launch-mspacman.sh`: starts the game directly, without the launcher and
  without background overlay compiling.

## Known issues

- Squash widescreen: the main menu's left and right borders show small
  rendering glitches, and the Ms. Pac-Man logo looks slightly stretched (it's
  made of 2D tiles).
- Native-wide widescreen: some edges show small rendering glitches, because
  not every edge cull in the game has been widened yet.
- With the default OpenGL renderer, ghosts can be drawn **under** the pellets.
  The software renderer draws them correctly
  (`./launch-mspacman.sh --renderer software`), so this is an OpenGL renderer
  bug in the framework.
- Pellet shadows are drawn over the ghosts in both renderers. This may be how
  the original looks.

## Legal

- This repository contains no game code, data or BIOS, and doesn't link to
  any. You need your own physical copy of the original game; the port is
  rebuilt from a disc image of that copy, on your own machine, and runs on the
  bundled MIT-licensed OpenBIOS.
- Methods for dumping, ripping or extracting game discs are not provided or
  supported here.
- Don't share your `disc/`, `generated/`, `.cache/` or build folders, or
  binaries built from them: they are derived from the game.
- See the README's [Disclaimer](README.md#disclaimer),
  [Copyright and trademarks](README.md#copyright-and-trademarks) and
  [Credits](README.md#credits).
