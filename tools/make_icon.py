#!/usr/bin/env python3
"""Generate the NX Diag homebrew icon.

Draws a 32x32 pixel-art SoC: a dark chip package with pins on all four sides
and a centre die showing a green diagnostic pulse trace, then nearest-neighbour
upscales it to the 256x256 JPG the homebrew menu wants.

Run with:  uv run --with pillow python tools/make_icon.py
"""
from pathlib import Path
from math import hypot
from PIL import Image

S = 32          # logical pixel-art grid
SCALE = 8       # -> 256x256 final icon
OUT = Path(__file__).resolve().parent.parent / "icon.jpg"

# --- palette ---
BG      = (13, 15, 23)
PIN     = (150, 156, 170)     # package pins
PKG     = (28, 31, 42)        # chip package
PKG_HI  = (52, 58, 76)        # package bevel highlight
DIE     = (18, 26, 40)        # silicon die
DIE_FR  = (120, 180, 255)     # die frame (accent blue)
TRACE   = (120, 224, 140)     # diagnostic pulse trace (green)
NOTCH   = (120, 180, 255)     # pin-1 orientation notch

px = [[BG for _ in range(S)] for _ in range(S)]


def put(x, y, c):
    if 0 <= x < S and 0 <= y < S:
        px[y][x] = c


def rect(x0, y0, x1, y1, c):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(x, y, c)


# --- package pins: short stubs on every side ---
PINS = (7, 11, 15, 19, 23)
for p in PINS:
    rect(p, 3, p + 1, 6, PIN)        # top
    rect(p, 25, p + 1, 28, PIN)      # bottom
    rect(3, p, 6, p + 1, PIN)        # left
    rect(25, p, 28, p + 1, PIN)      # right

# --- chip package body ---
rect(6, 6, 25, 25, PKG)
# bevel highlight along the top/left edges
rect(6, 6, 25, 6, PKG_HI)
rect(6, 6, 6, 25, PKG_HI)
# round the four package corners
R = 3
for cx, cy, sx, sy in ((6, 6, 1, 1), (25, 6, -1, 1),
                       (6, 25, 1, -1), (25, 25, -1, -1)):
    acx, acy = cx + sx * (R - 1), cy + sy * (R - 1)
    for dx in range(R):
        for dy in range(R):
            if hypot(cx + sx * dx - acx, cy + sy * dy - acy) > R - 0.4:
                put(cx + sx * dx, cy + sy * dy, BG)

# --- pin-1 orientation notch (top-left die corner marker) ---
put(9, 9, NOTCH)

# --- centre die ---
rect(11, 11, 20, 20, DIE)
for x in range(10, 22):
    put(x, 10, DIE_FR)
    put(x, 21, DIE_FR)
for y in range(10, 22):
    put(10, y, DIE_FR)
    put(21, y, DIE_FR)

# --- diagnostic pulse trace across the die (a heartbeat waveform) ---
# A flat baseline with one sharp spike: the classic "it's alive" readout.
wave = [(11, 16), (12, 16), (13, 16), (14, 16), (15, 16),
        (16, 12), (17, 16), (18, 16), (19, 16), (20, 16)]
for i, (x, y) in enumerate(wave):
    put(x, y, TRACE)
    if i + 1 < len(wave):                       # connect to the next sample
        ny = wave[i + 1][1]
        for yy in range(min(y, ny), max(y, ny) + 1):
            put(wave[i + 1][0], yy, TRACE)

# --- emit ---
img = Image.new("RGB", (S, S))
img.putdata([px[y][x] for y in range(S) for x in range(S)])
img = img.resize((S * SCALE, S * SCALE), Image.NEAREST)
img.save(OUT, "JPEG", quality=95, subsampling=0)
print(f"wrote {OUT} ({img.width}x{img.height})")
