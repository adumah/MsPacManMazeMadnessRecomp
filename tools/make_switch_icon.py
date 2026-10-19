#!/usr/bin/env python3
"""Make the Switch Homebrew Menu icon from your own disc (BUILD-SWITCH.md).

The icon is Ms. Pac-Man from the arcade cabinet side art that classic mode
shows beside the maze: a 156x240 TIM image inside CLASSIC.EXE. This script
reads CLASSIC.EXE from the disc image's data track, checks the image against
a known SHA-256 (so a different disc never yields a wrong picture), crops her
top-left figure (64x64) and scales it 4x, pixel for pixel, to the 256x256
JPEG that elf2nro embeds. No artwork is stored in this repository.

    tools/make_switch_icon.py --cue "disc/Ms. Pac-Man - Maze Madness.cue" \
        --out build-switch/icon.jpg

Needs Pillow (python3 -m pip install pillow, or your distribution's
python-pillow package). Exit status: 0 written, 2 Pillow missing, 1 any
other failure (the build then uses the default icon).
"""
import argparse
import hashlib
import os
import struct
import sys

SECTOR = 2352
SIDE_ART_SHA256 = "0e9c83d385a4544964525d38396a55ca843df5b8fd5fa7ddaa85b193859b490c"
SIDE_ART_SIZE = (156, 240)
CROP = (3, 2, 64, 64)  # x, y, width, height of her top-left figure
SCALE = 4


def data_track(cue_path):
    """The first BINARY file the cue names (the MODE2/2352 data track)."""
    base = os.path.dirname(os.path.abspath(cue_path))
    with open(cue_path, encoding="latin-1") as f:
        for line in f:
            line = line.strip()
            if line.upper().startswith("FILE "):
                name = line[5:].rsplit(" ", 1)[0].strip().strip('"')
                return os.path.join(base, name)
    raise ValueError(f"no FILE line in {cue_path}")


class Iso:
    """Just enough ISO 9660 over raw 2352-byte sectors to read one file."""

    def __init__(self, path):
        self.f = open(path, "rb")

    def sector(self, lba):
        self.f.seek(lba * SECTOR)
        raw = self.f.read(SECTOR)
        if len(raw) != SECTOR:
            raise ValueError(f"sector {lba} is past the end of the image")
        # Mode 2 Form 1 user data starts after the subheader; mode 1 doesn't
        # have one.
        return raw[24:24 + 2048] if raw[15] == 2 else raw[16:16 + 2048]

    def read(self, lba, size):
        out = bytearray()
        while len(out) < size:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:size])

    def find(self, name):
        root = self.sector(16)[156:156 + 34]
        lba, size = struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0]
        d = self.read(lba, size)
        i = 0
        while i < len(d):
            n = d[i]
            if n == 0:
                i = (i // 2048 + 1) * 2048
                continue
            nl = d[i + 32]
            entry = d[i + 33:i + 33 + nl].decode("latin-1").split(";")[0]
            if entry == name:
                return self.read(struct.unpack_from("<I", d, i + 2)[0],
                                 struct.unpack_from("<I", d, i + 10)[0])
            i += n
        raise ValueError(f"{name} not found on the disc")


def find_side_art(exe):
    """Offset and length of the 4-bit CLUT TIM with the side art's size."""
    i = 0
    while True:
        i = exe.find(b"\x10\x00\x00\x00\x08\x00\x00\x00", i)
        if i < 0:
            raise ValueError("side art image not found in CLASSIC.EXE")
        try:
            clut_len = struct.unpack_from("<I", exe, i + 8)[0]
            o = i + 8 + clut_len
            img_len = struct.unpack_from("<I", exe, o)[0]
            w, h = struct.unpack_from("<2H", exe, o + 8)
            if (w * 4, h) == SIDE_ART_SIZE and img_len == 12 + w * h * 2:
                return i, o + img_len - i
        except struct.error:
            pass
        i += 4


def decode_tim4(tim):
    """RGB rows of a 4-bit TIM with one 16-color palette."""
    clut_len = struct.unpack_from("<I", tim, 8)[0]
    clut = struct.unpack_from("<16H", tim, 8 + 12)
    palette = []
    for v in clut:
        r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
        palette.append(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)))
    o = 8 + clut_len
    w, h = struct.unpack_from("<2H", tim, o + 8)
    px = tim[o + 12:]
    width = w * 4
    pixels = []
    for y in range(h):
        for x in range(width):
            byte = px[y * w * 2 + (x >> 1)]
            pixels.append(palette[(byte >> (4 * (x & 1))) & 15])
    return width, h, pixels


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--cue", required=True, help="the disc image's .cue file")
    ap.add_argument("--out", required=True, help="output 256x256 JPEG")
    args = ap.parse_args()

    try:
        from PIL import Image
    except ImportError:
        print("make_switch_icon: Pillow is not installed; using the default icon",
              file=sys.stderr)
        return 2

    try:
        exe = Iso(data_track(args.cue)).find("CLASSIC.EXE")
        off, length = find_side_art(exe)
        tim = exe[off:off + length]
        if hashlib.sha256(tim).hexdigest() != SIDE_ART_SHA256:
            raise ValueError("the side art doesn't match the USA release")
        width, height, pixels = decode_tim4(tim)
    except (OSError, ValueError) as e:
        print(f"make_switch_icon: {e}; using the default icon", file=sys.stderr)
        return 1

    art = Image.new("RGB", (width, height))
    art.putdata(pixels)
    x, y, w, h = CROP
    icon = art.crop((x, y, x + w, y + h)).resize((w * SCALE, h * SCALE), Image.NEAREST)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    icon.save(args.out, "JPEG", quality=95)
    print(f"make_switch_icon: wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
