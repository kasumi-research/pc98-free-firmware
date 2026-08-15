#!/usr/bin/env python3
"""
Render cells out of a PC-98 FONT.ROM (np2 format) as ASCII art.

Made for eyeballing our generated font against a real PC-98 one:

    tools/fontdump.py A.rom --ank16 41,5c,b1 --compare B.rom
    tools/fontdump.py A.rom --kanji 16:1,4:2

Kanji cells are named ku:ten in decimal (JIS X 0208 row:cell).

SPDX-License-Identifier: MIT
"""

import argparse
import sys

OFF_ANK8 = 0x0000
OFF_ANK16_LO = 0x0800
OFF_ANK16_HI = 0x1000
OFF_KANJI = 0x1800
TEN_SLOTS = 96


def ank8(d, c):
    return [(d[c * 8 + r], 8) for r in range(8)]


def ank16(d, c):
    b = OFF_ANK16_LO + c * 16 if c < 0x80 else OFF_ANK16_HI + (c - 0x80) * 16
    return [(d[b + r], 8) for r in range(16)]


def kanji(d, ku, ten):
    b = OFF_KANJI + (ku - 1) * TEN_SLOTS * 32 + ((ten + 0x20) - 0x20) * 32
    return [((d[b + r] << 8) | d[b + 16 + r], 16) for r in range(16)]


def art(rows):
    out = []
    for v, w in rows:
        out.append("".join("#" if v & (1 << (w - 1 - i)) else "." for i in range(w)))
    return out


def show(cells, titles):
    """cells: list of row-lists, printed side by side."""
    widths = [len(art(c)[0]) for c in cells]
    print("  " + "  ".join(t.ljust(w) for t, w in zip(titles, widths)))
    arts = [art(c) for c in cells]
    for r in range(max(len(a) for a in arts)):
        print("  " + "  ".join(a[r] if r < len(a) else " " * w
                               for a, w in zip(arts, widths)))
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("font")
    ap.add_argument("--compare", help="second FONT.ROM, rendered alongside")
    ap.add_argument("--ank8", help="comma-separated hex codes")
    ap.add_argument("--ank16", help="comma-separated hex codes")
    ap.add_argument("--kanji", help="comma-separated ku:ten (decimal)")
    ap.add_argument("--coverage", action="store_true",
                    help="summarise filled slots instead of rendering")
    args = ap.parse_args()

    fonts = [open(args.font, "rb").read()]
    names = [args.font]
    if args.compare:
        fonts.append(open(args.compare, "rb").read())
        names.append(args.compare)
    for d, n in zip(fonts, names):
        if len(d) != 0x46800:
            sys.exit(f"fontdump: {n}: {len(d)} bytes, expected 0x46800")

    if args.coverage:
        for d, n in zip(fonts, names):
            a8 = sum(1 for c in range(256) if any(v for v, _ in ank8(d, c)))
            a16 = sum(1 for c in range(256) if any(v for v, _ in ank16(d, c)))
            nk = sum(1 for ku in range(1, 93) for ten in range(1, 95)
                     if any(v for v, _ in kanji(d, ku, ten)))
            print(f"{n}: ank8 {a8}/256  ank16 {a16}/256  kanji {nk}/8648")
        return

    for spec, fn, label in ((args.ank8, ank8, "ank8"),
                            (args.ank16, ank16, "ank16")):
        if not spec:
            continue
        for tok in spec.split(","):
            c = int(tok, 16)
            show([fn(d, c) for d in fonts],
                 [f"{label} {c:#04x} [{n.split('/')[-1]}]" for n in names])

    if args.kanji:
        for tok in args.kanji.split(","):
            ku, ten = (int(x) for x in tok.split(":"))
            show([kanji(d, ku, ten) for d in fonts],
                 [f"ku{ku}:ten{ten} [{n.split('/')[-1]}]" for n in names])


if __name__ == "__main__":
    main()
