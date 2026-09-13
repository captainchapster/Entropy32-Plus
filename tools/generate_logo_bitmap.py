#!/usr/bin/env python3
"""
Regenerates logo_bitmap.h, the 128x32 boot splash shown on the OLED
before the "Booting..." text screen.

Renders TEXT with FONT, thresholds it to 1-bit, and packs it into
SSD1306 page-column format (H/8 pages x 128 columns, LSB = top pixel of
each column) - the exact layout U8x8's drawTile() expects, so the
firmware can blit it with a plain memcpy_P() and no runtime conversion.

The logo is only 24px tall (3 of the display's 4 text rows) so the
bottom row stays free for a live boot status line.

Requires Pillow: pip install pillow

Usage:
    python3 generate_logo_bitmap.py
"""

from PIL import Image, ImageDraw, ImageFont

W, H = 128, 24  # bottom 8px row is reserved for the status line, not the logo
SCALE = 8  # supersample, then downscale for smoother thresholding
FONT = "C:/Windows/Fonts/segoeuiz.ttf"  # Segoe UI Bold Italic
TEXT = "ENTROPY32"
MARGIN_X, MARGIN_Y = 2, 1
THRESHOLD = 100
OUT_HEADER = "../logo_bitmap.h"


def measure(size):
    img = Image.new("L", (4, 4), 0)
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT, size * SCALE)
    b = d.textbbox((0, 0), TEXT, font=f)
    return f, b


def find_largest_fit():
    lo, hi, best = 6, 40, None
    while lo <= hi:
        mid = (lo + hi) // 2
        f, b = measure(mid)
        w = (b[2] - b[0]) / SCALE
        h = (b[3] - b[1]) / SCALE
        if w <= W - 2 * MARGIN_X and h <= H - 2 * MARGIN_Y:
            best = (mid, f, b)
            lo = mid + 1
        else:
            hi = mid - 1
    return best


def render(font, bbox):
    img = Image.new("L", (W * SCALE, H * SCALE), 0)
    draw = ImageDraw.Draw(img)
    w = bbox[2] - bbox[0]
    h = bbox[3] - bbox[1]
    x = (W * SCALE - w) // 2 - bbox[0]
    y = (H * SCALE - h) // 2 - bbox[1]
    draw.text((x, y), TEXT, fill=255, font=font)
    small = img.resize((W, H), Image.LANCZOS)
    return small.point(lambda p: 255 if p > THRESHOLD else 0, mode="L").convert("1")


def pack(bw):
    px = bw.load()
    out = []
    for page in range(H // 8):
        for col in range(W):
            byte = 0
            for bit in range(8):
                row = page * 8 + bit
                if px[col, row]:
                    byte |= 1 << bit
            out.append(byte)
    return out


def write_header(out):
    with open(OUT_HEADER, "w") as f:
        f.write(f"// Auto-generated boot logo bitmap, {W}x{H}, SSD1306 page-column format\n")
        f.write(f"// ({H // 8} pages x {W} columns, LSB = top pixel of each column - matches\n")
        f.write("// U8x8's drawTile() layout directly, no conversion needed at runtime).\n")
        f.write(f'// Font: Segoe UI Bold Italic, text "{TEXT}"\n')
        f.write("// Regenerate with tools/generate_logo_bitmap.py if the wordmark changes.\n")
        f.write(f"const uint8_t LOGO_BITMAP[{len(out)}] PROGMEM = {{\n")
        for i in range(0, len(out), 16):
            row = ", ".join(f"0x{b:02x}" for b in out[i:i + 16])
            f.write(f"  {row},\n")
        f.write("};\n")


if __name__ == "__main__":
    size, font, bbox = find_largest_fit()
    bw = render(font, bbox)
    out = pack(bw)
    write_header(out)
    print(f"chosen font size: {size}, wrote {OUT_HEADER} ({len(out)} bytes)")
