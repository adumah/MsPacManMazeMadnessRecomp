# Building Ms. Pac-Man Maze Madness for Nintendo Switch

This builds a homebrew `.nro` of *Ms. Pac-Man Maze Madness* (PlayStation,
USA, `SLUS-01018`) for a Switch running custom firmware (Atmosphère), with
[PSXRecomp](https://github.com/mstan/psxrecomp). As with the
[Linux build](BUILD.md), this repository contains only the **setup**: you
build the game yourself, on a Linux PC, from a disc image of **your own
copy**. No game files are provided, and neither are methods for making a disc
image (see [Legal](#legal)).

Nothing produced from your disc (`disc/`, `generated/`, `build-*/`, `.cache/`,
the `.nro` and the SD card folder) should ever be shared or committed.

The result runs at full speed (60 fps) in the menus and mazes, including
saving and classic mode, in the original 4:3 picture. The intro videos run at
about 53 fps. Tested on hardware with Atmosphère.

## 1. What you need

- **The disc image** described in [BUILD.md, section 1](BUILD.md#1-what-you-need)
  (one `.cue` and seven `.bin` files, verified by the build).
- **A Linux PC** with the tools and libraries from that section (the
  recompiler and the code generator run on the PC) and about 16 GB of RAM.
- **[devkitPro](https://devkitpro.org/wiki/Getting_Started)** with the Switch
  toolchain and these libraries:

  ```sh
  sudo dkp-pacman -S switch-dev switch-sdl2 switch-mesa switch-libdrm_nouveau \
      switch-zlib switch-libzstd
  ```

  (On Arch-based systems with the devkitPro repositories added to pacman, use
  `sudo pacman -S` with the same package names.) Tested with devkitA64 r30
  (GCC 16.1), libnx 4.12.0, switch-sdl2 2.28.5 and switch-mesa 20.1.0.
- **Pillow** for Python (`python-pillow` on Arch, `python3-pil` on
  Debian/Ubuntu, or `python3 -m pip install pillow`), to make the Homebrew
  Menu icon from your disc. Without it the build uses the default icon.
- **A Switch with Atmosphère** and the Homebrew Menu, and a way to copy files
  to its SD card.

## 2. Get the source and apply the Switch patch

```sh
git clone --recursive <this repository's URL> MsPacManMazeMadnessRecomp
cd MsPacManMazeMadnessRecomp
git -C psxrecomp apply ../patches/psxrecomp-switch.patch
```

The patch adds what the framework needs on Horizon (no `dlopen`, an AArch64
fiber backend, socket headers) and the speed-ups that make full speed
possible: memoized overlay dispatch, disc read-ahead, batched DMA events and
a faster interrupt check. It's only needed for the Switch build.

## 3. Put your disc in `disc/`

As in [BUILD.md, section 3](BUILD.md#3-put-your-disc-in-disc).

## 4. Build the recompiler

```sh
./psxrecomp/tools/ci/build_emitters.sh
```

## 5. Generate the game code from your disc

```sh
PSX_CODEGEN_CYCLE_PER_INSN=0 PSXRECOMP_AOT_WORKERS=2 \
    python3 psxrecomp/psxrecomp_cli.py generate \
    --config game.toml --project-root . \
    --disc "disc/Ms. Pac-Man - Maze Madness.cue"
```

`PSX_CODEGEN_CYCLE_PER_INSN=0` charges CPU time per block of code instead of
per instruction. That's much faster on the Switch and is required for full
speed. The Linux build in [BUILD.md](BUILD.md) uses the default instead, so if
you build both from one checkout, run generate again (with or without the
variable) before switching.

## 6. Build the `.nro`

```sh
export PATH=/opt/devkitpro/portlibs/switch/bin:/opt/devkitpro/devkitA64/bin:/opt/devkitpro/tools/bin:$PATH
aarch64-none-elf-cmake -S . -B build-switch -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 -DPSX_SDL_BACKEND=SDL2 -DPSX_SDL3_FETCH=OFF \
    -DPSX_DEBUG_TOOLS=OFF -DPSX_NETPLAY=OFF -DPSX_RECOMP_UI=OFF \
    -DPSX_ENABLE_VULKAN=OFF
cmake --build build-switch -j1
```

Use `-j1`: the two largest generated files each need several GB of memory
to compile with the Switch compiler, and two at once can run out of memory.
To keep only the build from being killed if memory runs out, run it inside a
cap:

```sh
systemd-run --user --scope -p MemoryMax=10G -p MemorySwapMax=4G \
    cmake --build build-switch -j1
```

The result is `build-switch/mspacman.nro`.

Its Homebrew Menu icon is Ms. Pac-Man from the arcade cabinet art that
classic mode shows beside the maze, cut from your disc when CMake configures
(`tools/make_switch_icon.py`; CMake prints `Switch icon: ...`). To use your
own picture instead, put a 256x256 JPEG at `switch/icon.jpg` and configure
again; that file is ignored by git.

## 7. Put it on the SD card

```sh
tools/package_switch.sh --with-disc
```

This stages `build-switch/sdcard/switch/mspacman/` with the `.nro`, its
settings, the bundled OpenBIOS, the mod catalog and (with `--with-disc`) a
copy of your disc image. Copy the contents of `build-switch/sdcard/` to the
root of the SD card. Without `--with-disc`, copy your `.cue` and seven `.bin`
files to `/switch/mspacman/disc/` on the card yourself.

When updating, copy over the old folder: your saves (`saves/`) and settings
(`mods/state.toml`, `environment.txt`) are not part of the staged folder and
stay as they are.

## 8. Play

**Start it in place of a game, not from the album.** Hold **R** while
launching any installed game, then pick *Ms. Pac-Man Maze Madness
Recompiled* in the Homebrew Menu. Started from the album, homebrew runs as an
applet with too little memory for the game.

The game raises the CPU clock to 1785 MHz while it runs (the speed of the
console's own boost mode) and puts it back when you quit; no overclocking
tool is needed. A tool such as sys-clk that sets the CPU higher is left
alone; one that sets it lower for the same game will fight it, so remove that
setting.

Quit with the **HOME** button and close the software.

### Controls

Player 1 is the first controller (Joy-Cons attached, or a Pro Controller).

| Switch | PlayStation |
|---|---|
| D-pad, left stick | D-pad |
| B / A / Y / X (bottom / right / left / top) | Cross / Circle / Square / Triangle |
| + | Start (pause) |
| − | Select |
| L / R / ZL / ZR | L1 / R1 / L2 / R2 |

## Settings

**Widescreen 16:9.** Off by default on the Switch: its extra work costs
about 12% of each frame, which full speed can't spare. To try it, create
`/switch/mspacman/mods/state.toml` on the card with:

```toml
format_version = 2

[[package]]
id = "mpmm.enhancement.widescreen"

[[feature]]
package_id = "mpmm.enhancement.widescreen"
id = "widescreen"
enabled = true

[[feature]]
package_id = "mpmm.enhancement.widescreen"
id = "squash-billboards"
enabled = true
```

**`environment.txt`.** `/switch/mspacman/environment.txt` holds
`NAME=value` lines (`#` starts a comment), read at startup. Useful ones:

| Setting | Effect |
|---|---|
| `MPMM_CPU_BOOST=0` | Don't change the CPU clock |
| `MPMM_CPU_BOOST=1581` | Boost to another clock (MHz) |
| `PSX_RUNTIME_PERF_DIAG=1` | Log the emulated frame rate every 5 s (`PSX_RUNTIME_PERF_DIAG_MS=1000`: every second) |

**Log.** Each run writes `/switch/mspacman/log.txt` (the run before that is
`log-previous.txt`).

## Troubleshooting

**The build or generate step gets killed, or the machine freezes.** See
[BUILD.md, Troubleshooting](BUILD.md#troubleshooting).

**The game is slow.** Check that it was started in place of a game (holding
R), and that `log.txt` has a line like `CPU boost 1020 -> 1785 MHz (ok)`.
Check that `generate` ran with `PSX_CODEGEN_CYCLE_PER_INSN=0`.

**The game closes or the console shows an error.** Atmosphère writes a crash
report to `/atmosphere/crash_reports/` on the card. `log.txt` shows how far
startup got.

**"running as an applet" in `log.txt`.** It was started from the album; start
it in place of a game instead (section 8).

## Known issues

- The intro videos run slightly below full speed (about 53 fps), so their
  sound can crackle a little.
- The [known issues of the Linux build](BUILD.md#known-issues) apply too.

## Developing

To run a build straight from the PC over Wi-Fi, open the Homebrew Menu, press
**Y** (NetLoader), then:

```sh
nxlink -s -p mspacman/mspacman.nro build-switch/mspacman.nro
```

`-s` shows the game's log on the PC. `NAME=value` arguments after the `.nro`
work like lines in `environment.txt`. `SWITCH_PROF=1` turns on a sampling
profiler that writes 5-second windows to `/switch/mspacman/prof/`
(addresses are offsets into `build-switch/MsPacManMazeMadness_Recompiled.elf`).

## Legal

The [Legal section of BUILD.md](BUILD.md#legal) applies. In particular,
don't share the `.nro` or the SD card folder: they are built from your disc.
