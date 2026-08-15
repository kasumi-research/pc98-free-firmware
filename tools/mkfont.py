#!/usr/bin/env python3
"""
Build a free PC-98 character generator ROM (np2 FONT.ROM format).

Sources
-------
  GNU Unifont JP (BDF)   16x16 kanji + 8x16 halfwidth.
                         The JP variant matters: plain Unifont's CJK
                         ideographs are Wen Quan Yi *Chinese* forms and
                         are visibly wrong on a Japanese machine.  Its
                         kanji are public domain (Izumi glyphs since
                         12.1.03); the font as a whole is OFL-1.1 and
                         GPLv2+ with the GNU Font Embedding Exception.
  IchigoJam font (bin)   8x8 halfwidth.  MIT.  Unifont has no 8x8 face,
                         and every Japanese bitmap family sizes its
                         halfwidth cell at half its fullwidth cell, so
                         an 8x8-kanji font necessarily has a 4x8 ANK
                         face -- the wrong shape.  IchigoJam is a true
                         8x8 character-cell font with all 63 JIS X 0201
                         kana present.

Output
------
  Exactly 0x46800 bytes.  The loader (qemu hw/i386/pc98/pc98-font.c)
  rejects any other size.

      0x00000  ANK 8x8   256 x 8
      0x00800  ANK 8x16  chars 0x00-0x7F, 16 bytes each
      0x01000  ANK 8x16  chars 0x80-0xFF
      0x01800  kanji, ku-major from ku 1, 92 ku, 96 cells per ku
               (ten byte 0x20..0x7F), each cell 32 bytes =
               16 left-half rows then 16 right-half rows

SPDX-License-Identifier: MIT
"""

import argparse
import sys

FONT_SIZE = 0x46800
OFF_ANK8 = 0x0000
OFF_ANK16_LO = 0x0800
OFF_ANK16_HI = 0x1000
OFF_KANJI = 0x1800
KU_COUNT = 92
TEN_SLOTS = 96          # ten byte 0x20..0x7F; real JIS ten is 0x21..0x7E


# ---------------------------------------------------------------- BDF

class Glyph:
    __slots__ = ("w", "h", "xo", "yo", "rows")


def parse_bdf(path):
    """-> {codepoint: Glyph}.  rows are ints, MSB = leftmost pixel of a
    byte-padded field of width ceil(w/8)*8."""
    glyphs = {}
    cp = None
    g = None
    in_bitmap = False
    with open(path, "r", errors="replace") as f:
        for line in f:
            if in_bitmap:
                if line.startswith("ENDCHAR"):
                    in_bitmap = False
                    if cp is not None and cp >= 0:
                        glyphs[cp] = g
                    cp, g = None, None
                else:
                    g.rows.append(int(line.strip() or "0", 16))
                continue
            if line.startswith("ENCODING"):
                cp = int(line.split()[1])
            elif line.startswith("BBX"):
                g = Glyph()
                p = line.split()
                g.w, g.h, g.xo, g.yo = (int(x) for x in p[1:5])
                g.rows = []
            elif line.startswith("BITMAP"):
                in_bitmap = True
    return glyphs


def render(g, cell_w, cell_h=16, fbb_y=-2, center=False):
    """Rasterise a BDF glyph into cell_h rows of cell_w bits (MSB left).

    center: horizontally centre a glyph narrower than the cell.  Needed
    for the fullwidth kanji cells, because several characters the PC-98
    keeps there are drawn halfwidth in Unifont -- the roman numerals in
    NEC row 13 most visibly.  Left-aligning those puts them against the
    cell edge where the real ROM centres them.
    """
    out = [0] * cell_h
    if g is None:
        return out
    pad_w = ((g.w + 7) // 8) * 8
    x_shift = (cell_w - g.w) // 2 if (center and g.w < cell_w) else 0
    # BDF origin: the glyph's bottom-left sits at (xo, yo); the cell
    # spans y = fbb_y .. fbb_y + cell_h - 1, with row 0 at the top.
    top = (fbb_y + cell_h - 1) - (g.yo + g.h - 1)
    for i, bits in enumerate(g.rows):
        r = top + i
        if not (0 <= r < cell_h):
            continue
        acc = out[r]
        for j in range(g.w):
            if bits & (1 << (pad_w - 1 - j)):
                x = g.xo + j + x_shift
                if 0 <= x < cell_w:
                    acc |= 1 << (cell_w - 1 - x)
        out[r] = acc
    return out


# ------------------------------------------------- ANK code -> Unicode

def ank_unicode(c):
    """PC-98 ANK slot -> Unicode, or None if we do not fill it.

    NOT Latin-1 above 0x80, and not pure ASCII below it: this is
    JIS X 0201.  0x5C is YEN, confirmed by rendering a real PC-98
    character generator ROM.  0x7E is a tilde-like wave there rather
    than the standard's overline, so we follow the machine, not the
    standard.
    """
    if c == 0x5C:
        return 0x00A5           # YEN SIGN
    if c == 0x7E:
        return 0x007E           # the real ROM draws a tilde here
    if 0x20 <= c <= 0x7D:
        return c                # ASCII
    if 0xA1 <= c <= 0xDF:
        return 0xFF61 + (c - 0xA1)   # halfwidth katakana
    return None


# --------------------------------------------- procedural graphics set

def graphics_cell(c, h):
    """The PC-98 block-graphics slots, which are arithmetic rather than
    artwork.  Derived by rendering a real PC-98 character generator ROM.
    Returns None for slots we do not synthesise."""
    if 0x80 <= c <= 0x87:                       # fill from the bottom
        n = (c - 0x80 + 1) * h // 8
        return [0x00] * (h - n) + [0xFF] * n
    if 0x88 <= c <= 0x8E:                       # fill from the left
        n = c - 0x88 + 1
        return [(0xFF << (8 - n)) & 0xFF] * h
    if c == 0x8F:                               # cross
        mid = h // 2
        return [0xFF if r == mid else 0x08 for r in range(h)]
    return None


# ------------------------------------------------------------ builders

def build_ank(glyphs, cell_h, ichigojam=None):
    """256 cells of cell_h rows.  ichigojam (2048 bytes) supplies the
    8x8 face, which Unifont cannot."""
    cells = []
    filled = 0
    for c in range(256):
        rows = graphics_cell(c, cell_h)
        if rows is None:
            if ichigojam is not None and ank_unicode(c) is not None:
                rows = list(ichigojam[c * 8:(c + 1) * 8])
                # IchigoJam draws ASCII at 0x5C and 0x7E, where
                # JIS X 0201 wants YEN and a wave dash.  Unifont has
                # both, but scaling its 16-row glyphs down to 8 loses
                # them, so the two cells are drawn by hand.
                if c == 0x5C:
                    rows = [0x00, 0x44, 0x28, 0x7C, 0x10, 0x7C, 0x10, 0x00]
                elif c == 0x7E:
                    rows = [0x00, 0x00, 0x32, 0x4C, 0x00, 0x00, 0x00, 0x00]
            else:
                cp = ank_unicode(c)
                rows = render(glyphs.get(cp), 8, cell_h) if cp is not None \
                    else [0] * cell_h
        if any(rows):
            filled += 1
        cells.append(bytes(rows))
    return cells, filled


def kuten_to_unicode(ku, ten):
    """JIS X 0208 row/cell -> Unicode.

    Goes via Shift_JIS and the cp932 codec rather than euc_jp, because
    a PC-98 CG ROM is not plain JIS X 0208: it also carries the NEC
    extension rows, which euc_jp cannot decode.  cp932 covers row 13
    (NEC special characters -- circled digits, roman numerals, units,
    No./TEL ligatures) and rows 89-92 (NEC-selected IBM extensions).
    Rows 9-12 are PC-98-specific and are in no standard codec; they
    remain blank.
    """
    if ku % 2:
        s2 = ten + 0x3F if ten <= 63 else ten + 0x40
    else:
        s2 = ten + 0x9E
    s1 = (ku + 1) // 2 + (0x80 if ku <= 62 else 0xC0)
    try:
        ch = bytes([s1, s2]).decode("cp932")
    except (UnicodeDecodeError, ValueError):
        return None
    return ord(ch) if len(ch) == 1 else None


def build_kanji(glyphs):
    """ku 1..92 x 96 ten slots, 32 bytes per cell."""
    blank = bytes(32)
    out = bytearray()
    filled = 0
    for ku in range(1, KU_COUNT + 1):
        for slot in range(TEN_SLOTS):
            ten_byte = 0x20 + slot
            cell = blank
            if 0x21 <= ten_byte <= 0x7E:
                cp = kuten_to_unicode(ku, ten_byte - 0x20)
                if cp is not None:
                    rows = render(glyphs.get(cp), 16, center=True)
                    if any(rows):
                        cell = bytes([r >> 8 for r in rows]) + \
                               bytes([r & 0xFF for r in rows])
                        filled += 1
            out += cell
    return bytes(out), filled


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--unifont-jp", required=True, help="unifont_jp BDF")
    ap.add_argument("--ichigojam", required=True,
                    help="ichigojam-font.bin (2048 bytes)")
    ap.add_argument("-o", "--out", required=True)
    args = ap.parse_args()

    ij = open(args.ichigojam, "rb").read()
    if len(ij) != 2048:
        sys.exit(f"mkfont: {args.ichigojam}: expected 2048 bytes, "
                 f"got {len(ij)}")

    print(f"parsing {args.unifont_jp} ...", flush=True)
    glyphs = parse_bdf(args.unifont_jp)
    print(f"  {len(glyphs)} glyphs")

    ank8, n8 = build_ank(glyphs, 8, ichigojam=ij)
    ank16, n16 = build_ank(glyphs, 16)
    kanji, nk = build_kanji(glyphs)

    img = bytearray(b"\x00" * FONT_SIZE)
    for c in range(256):
        img[OFF_ANK8 + c * 8:OFF_ANK8 + (c + 1) * 8] = ank8[c]
    for c in range(0x80):
        img[OFF_ANK16_LO + c * 16:OFF_ANK16_LO + (c + 1) * 16] = ank16[c]
    for c in range(0x80, 0x100):
        b = OFF_ANK16_HI + (c - 0x80) * 16
        img[b:b + 16] = ank16[c]
    img[OFF_KANJI:OFF_KANJI + len(kanji)] = kanji

    if len(img) != FONT_SIZE:
        sys.exit(f"mkfont: internal error, image is {len(img)} bytes")
    if OFF_KANJI + len(kanji) != FONT_SIZE:
        sys.exit(f"mkfont: kanji region ends at "
                 f"{OFF_KANJI + len(kanji):#x}, expected {FONT_SIZE:#x}")

    with open(args.out, "wb") as f:
        f.write(img)

    print(f"{args.out}: {len(img)} bytes (0x{len(img):X})")
    print(f"  ANK 8x8   {n8:3d}/256 slots filled")
    print(f"  ANK 8x16  {n16:3d}/256 slots filled")
    print(f"  kanji     {nk}/{KU_COUNT * 94} cells filled")


if __name__ == "__main__":
    main()
