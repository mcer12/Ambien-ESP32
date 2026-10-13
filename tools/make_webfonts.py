"""PixelFonts (Adafruit GFX headers) -> pixel-exact WOFF2 web fonts: src/web_fonts.h + screenshots/fonts/.

One font pixel = 1/16 em, so text is crisp at font sizes that are multiples of 16px.
Needs a prior `pio run` (reads .pio/libdeps) and `pip install fonttools brotli`.
"""
import io
import os
import re
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import newTable

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIXELFONTS = os.path.join(ROOT, ".pio", "libdeps", "esp32dev", "PixelFonts", "src")
HEADER_OUT = os.path.join(ROOT, "src", "web_fonts.h")
FILES_OUT = os.path.join(ROOT, "screenshots", "fonts")

PIXEL = 64            # font units per font pixel
UNITS_PER_EM = 16 * PIXEL

# file served at /fonts/<file>: (PixelFonts header, CSS family name, weight)
FONTS = {
    "text.woff2": ("pixelfont_extended_latin_13x7_thin", "PixelText", 400),
    "text-bold.woff2": ("pixelfont_extended_latin_13x7_semibold", "PixelText", 700),
    "small.woff2": ("pixelfont_extended_latin_9x5_thin", "PixelSmall", 400),
    "title.woff2": ("pixelfont_3d_condensed_extended_latin_13x7_semibold", "PixelTitle", 400),
}


def parse_gfx(path):
    """(first, last, glyphs) with glyphs[code] = (advance, [(x, y) lit pixels, y relative to the baseline, down])."""
    src = open(path, encoding="utf-8", errors="ignore").read()
    bitmap = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]+",
                                             re.search(r"uint8_t\s+\w+\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S).group(1))]
    table = re.search(r"GFXglyph\s+\w+\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S).group(1)
    entries = [tuple(int(v) for v in m) for m in re.findall(
        r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\}", table)]
    font = re.search(r"GFXfont\s+\w+\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S).group(1)
    first, last = [int(n, 0) for n in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b", font.split(",", 2)[2])[:2]]
    glyphs = {}
    for i, (offset, w, h, advance, xo, yo) in enumerate(entries):
        if advance == 0 and w == 0:
            continue  # empty slot
        pixels = [(xo + k % w, yo + k // w) for k in range(w * h) if bitmap[offset + k // 8] & (0x80 >> (k % 8))]
        glyphs[first + i] = (advance, pixels)
    return first, last, glyphs


def build_font(header, family, weight):
    _, _, glyphs = parse_gfx(os.path.join(PIXELFONTS, header + ".h"))
    order, cmap, outlines, metrics = [".notdef"], {}, {}, {}
    pen = TTGlyphPen(None)
    outlines[".notdef"] = pen.glyph()
    metrics[".notdef"] = (8 * PIXEL, 0)
    top = max((-y for _, px in glyphs.values() for _, y in px), default=1)
    bottom = max((y + 1 for _, px in glyphs.values() for _, y in px), default=0)

    for code, (advance, pixels) in sorted(glyphs.items()):
        name = "space" if code == 0x20 else f"uni{code:04X}"
        order.append(name)
        cmap[code] = name
        pen = TTGlyphPen(None)
        rows = {}
        for x, y in pixels:
            rows.setdefault(y, []).append(x)
        for y, xs in rows.items():
            xs.sort()
            start = prev = xs[0]
            for x in xs[1:] + [None]:
                if x is not None and x == prev + 1:
                    prev = x
                    continue
                x0, x1 = start * PIXEL, (prev + 1) * PIXEL       # one horizontal run of pixels
                y0, y1 = -(y + 1) * PIXEL, -y * PIXEL            # font units: y up from the baseline
                pen.moveTo((x0, y0)); pen.lineTo((x0, y1)); pen.lineTo((x1, y1)); pen.lineTo((x1, y0)); pen.closePath()
                if x is not None:
                    start = prev = x
        outlines[name] = pen.glyph()
        metrics[name] = (advance * PIXEL, min((x for x, _ in pixels), default=0) * PIXEL)

    # ascent + descent = exactly 1 em (16 font pixels), split in whole font pixels: with a line-height that is a
    # multiple of 16px - or the same as the font size (inputs, buttons) - the baseline lands on a whole CSS pixel.
    # A fractional baseline (e.g. 17 px text in a 24 px line) blurs every horizontal edge.
    extra = 16 - top - bottom
    if extra < 0:
        sys.exit(f"{header}: glyphs are {top + bottom} pixels tall, more than 16 (1 em)")
    ascent, descent = (top + extra - extra // 2) * PIXEL, (bottom + extra // 2) * PIXEL
    fb = FontBuilder(UNITS_PER_EM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(outlines)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ascent, descent=-descent)
    style = "Bold" if weight >= 700 else "Regular"
    fb.setupNameTable({"familyName": family, "styleName": style, "uniqueFontIdentifier": f"{family}-{style}",
                       "fullName": f"{family} {style}", "psName": f"{family}-{style}", "version": "Version 1.0"})
    fb.setupOS2(sTypoAscender=ascent, sTypoDescender=-descent, sTypoLineGap=0, usWinAscent=ascent,
                usWinDescent=descent, usWeightClass=weight, fsSelection=0x20 if weight >= 700 else 0x40)
    fb.setupPost()
    fb.font["head"].macStyle = 1 if weight >= 700 else 0  # must match the OS/2 bold flag
    gasp = newTable("gasp")  # "no anti-aliasing, no grid fitting" at all sizes, for renderers that honor it
    gasp.version = 1
    gasp.gaspRange = {0xFFFF: 0}
    fb.font["gasp"] = gasp
    fb.font.flavor = "woff2"
    out = io.BytesIO()
    fb.font.save(out)
    return out.getvalue(), len(glyphs)


def main():
    if not os.path.isdir(PIXELFONTS):
        sys.exit(f"{PIXELFONTS} not found: run `pio run` once so PlatformIO downloads PixelFonts")
    os.makedirs(FILES_OUT, exist_ok=True)
    lines = ["// Generated by tools/make_webfonts.py from PixelFonts - do not edit.",
             "// Pixel-exact WOFF2 web fonts for the web portal, served under /fonts/ (1 font pixel = 1/16 em).",
             "#ifndef WEB_FONTS_H", "#define WEB_FONTS_H", "", "#include <Arduino.h>", ""]
    table = []
    for i, (file, (header, family, weight)) in enumerate(FONTS.items()):
        data, count = build_font(header, family, weight)
        open(os.path.join(FILES_OUT, file), "wb").write(data)
        lines.append(f"// {file}: {family} {weight} from {header}, {count} glyphs, {len(data)} bytes")
        lines.append(f"static const uint8_t WEB_FONT_{i}[] PROGMEM = {{")
        for k in range(0, len(data), 24):
            lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[k:k + 24]) + ",")
        lines.append("};")
        table.append(f'    {{"/fonts/{file}", WEB_FONT_{i}, sizeof(WEB_FONT_{i})}},')
        print(f"{file}: {family} {weight}, {count} glyphs, {len(data)} bytes")
    lines += ["", "struct WebFont {", "    const char* path;", "    const uint8_t* data;", "    size_t size;", "};", "",
              "static const WebFont WEB_FONTS[] = {"] + table + ["};", "", "#endif", ""]
    open(HEADER_OUT, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    print(HEADER_OUT)


if __name__ == "__main__":
    main()
