"""Turn a PSF1 console font with a unicode table into a C++ glyph table header."""
import gzip
import sys

src, dst = sys.argv[1], sys.argv[2]
data = gzip.open(src).read()
assert data[0:2] == b"\x36\x04", "not PSF1"
mode, height = data[2], data[3]
count = 512 if mode & 1 else 256
glyphs = [data[4 + i * height: 4 + (i + 1) * height] for i in range(count)]
table = data[4 + count * height:]

cp_to_glyph = {}
pos = 0
for g in range(count):
    while True:
        u = table[pos] | table[pos + 1] << 8
        pos += 2
        if u == 0xFFFF:
            break
        if u == 0xFFFE:  # start of a sequence: skip to the glyph's end marker
            continue
        cp_to_glyph.setdefault(u, g)

wanted = list(range(0x20, 0x7F)) + list(range(0x400, 0x460)) + [0x490, 0x491, 0x2013, 0x2014, 0x2026]
rows = []
for cp in wanted:
    if cp in cp_to_glyph:
        bits = ", ".join(f"0x{b:02X}" for b in glyphs[cp_to_glyph[cp]])
        rows.append(f"  {{0x{cp:04X}, {{{bits}}}}},")
with open(dst, "w", newline="\r\n") as f:
    f.write("// Generated from Terminus Bold 8x16 (CyrSlav-TerminusBold16.psf.gz, SIL OFL 1.1)\n")
    f.write("// by psf2header.py: ASCII, Cyrillic U+0400-U+045F, U+0490-U+0491 and dashes. Sorted by code point.\n")
    f.write("#pragma once\n#include <cstdint>\n\nnamespace banner {\n\n")
    f.write(f"constexpr int kGlyphWidth = 8;\nconstexpr int kGlyphHeight = {height};\n\n")
    f.write(f"struct Glyph {{ uint16_t code; uint8_t rows[{height}]; }};\n\n")
    f.write("inline constexpr Glyph kGlyphs[] = {\n" + "\n".join(rows) + "\n};\n\n}  // namespace banner\n")
print(len(rows), "glyphs")
