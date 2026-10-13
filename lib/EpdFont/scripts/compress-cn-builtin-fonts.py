#!/usr/bin/env python3
"""Losslessly pack common CJK fonts into bounded raw-DEFLATE groups."""

import argparse
import hashlib
from pathlib import Path
import re
import zlib

from verify_compression import compact_aligned_to_packed, parse_glyphs, parse_groups, parse_hex_array

GROUP_BYTES = 4096


def array_body(text, name):
    match = re.search(rf"{name}\[\d*\] = \{{(.*?)\n\}};", text, re.S)
    if not match:
        raise ValueError(f"Missing array {name}")
    return match.group(1)


def packed_glyphs(text, name):
    glyphs = parse_glyphs(array_body(text, name + "Glyphs"))
    bitmap = parse_hex_array(array_body(text, name + "Bitmaps"))
    group_match = re.search(rf"{name}Groups\[\] = \{{(.*?)\n\}};", text, re.S)
    if not group_match:
        return glyphs, [bitmap[g["dataOffset"]:g["dataOffset"] + g["dataLength"]] for g in glyphs]
    packed = [None] * len(glyphs)
    for group in parse_groups(group_match.group(1)):
        start = group["compressedOffset"]
        raw = zlib.decompress(bitmap[start:start + group["compressedSize"]], -15)
        if len(raw) != group["uncompressedSize"]:
            raise ValueError("Invalid decompressed group size")
        offset = 0
        for index in range(group["firstGlyphIndex"], group["firstGlyphIndex"] + group["glyphCount"]):
            g = glyphs[index]
            length = ((g["width"] + 3) // 4) * g["height"]
            packed[index] = bytes(compact_aligned_to_packed(raw[offset:offset + length], g["width"], g["height"]))
            offset += length
        if offset != len(raw):
            raise ValueError("Unexpected bytes after group glyphs")
    if any(p is None for p in packed):
        raise ValueError("Incomplete glyph coverage")
    return glyphs, packed


def align_glyph(packed, width, height):
    aligned = bytearray(((width + 3) // 4) * height)
    stride = (width + 3) // 4
    for y in range(height):
        for x in range(width):
            position = y * width + x
            pixel = (packed[position // 4] >> ((3 - position % 4) * 2)) & 3
            aligned[y * stride + x // 4] |= pixel << ((3 - x % 4) * 2)
    return aligned


def compressed_header(text, name):
    glyphs, packed = packed_glyphs(text, name)
    if not glyphs or any(len(p) != g["dataLength"] for g, p in zip(glyphs, packed)):
        raise ValueError("Invalid packed glyph lengths")
    bitmap = bytearray()
    groups = []
    rows = []
    block = bytearray()
    first = 0
    packed_offset = 0

    def flush(end):
        compressor = zlib.compressobj(9, wbits=-15)
        data = compressor.compress(block) + compressor.flush()
        groups.append((len(bitmap), len(data), len(block), end - first, first))
        bitmap.extend(data)

    for index, (g, p) in enumerate(zip(glyphs, packed)):
        aligned = align_glyph(p, g["width"], g["height"])
        if len(aligned) > GROUP_BYTES:
            raise ValueError(f"Glyph {index} exceeds {GROUP_BYTES} bytes")
        if block and len(block) + len(aligned) > GROUP_BYTES:
            flush(index)
            block.clear()
            first = index
            packed_offset = 0
        rows.append(tuple(g[key] for key in ("width", "height", "advanceX", "left", "top", "dataLength"))
                    + (packed_offset,))
        packed_offset += len(p)
        block.extend(aligned)
    flush(len(glyphs))
    if len(groups) > 65535:
        raise ValueError("Too many groups")
    bitmap_body = "\n".join("    " + ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 16]) + ","
                            for i in range(0, len(bitmap), 16))
    text = re.sub(rf"static const uint8_t {name}Bitmaps\[\d+\] = \{{.*?\n\}};",
                  f"static const uint8_t {name}Bitmaps[{len(bitmap)}] = {{\n{bitmap_body}\n}};", text, flags=re.S)
    glyph_body = "\n".join("    { " + ", ".join(map(str, row)) + " }," for row in rows)
    text = re.sub(rf"static const EpdGlyph {name}Glyphs\[\] = \{{.*?\n\}};",
                  f"static const EpdGlyph {name}Glyphs[] = {{\n{glyph_body}\n}};", text, flags=re.S)
    text = re.sub(rf"static const EpdFontGroup {name}Groups\[\] = \{{.*?\n\}};\n*", "", text, flags=re.S)
    group_body = "\n".join("    { " + ", ".join(map(str, group)) + " }," for group in groups)
    descriptor = f"static const EpdFontData {name} = {{"
    text = text.replace(descriptor, f"static const EpdFontGroup {name}Groups[] = {{\n{group_body}\n}};\n\n" + descriptor)
    # Only the groups pointer/count change in the descriptor; metrics and indices stay intact.
    pattern = rf"({re.escape(descriptor)}.*?\n    true,\n)    (?:nullptr|{name}Groups),\n    \d+,"
    text, count = re.subn(pattern, rf"\g<1>    {name}Groups,\n    {len(groups)},", text, count=1, flags=re.S)
    if count != 1:
        raise ValueError("Unexpected font descriptor")
    text = re.sub(r"// Lossless CJK compression:.*\n", "", text)
    digest = hashlib.sha256(b"".join(packed)).hexdigest()
    text = f"// Lossless CJK compression: zlib9, groups <=4096 B, packed SHA256 {digest}\n" + text
    decoded_glyphs, decoded = packed_glyphs(text, name)
    for before, after in zip(glyphs, decoded_glyphs):
        if {k: v for k, v in before.items() if k != "dataOffset"} != {k: v for k, v in after.items() if k != "dataOffset"}:
            raise ValueError("Glyph metrics changed")
    if decoded != packed:
        raise ValueError("Glyph pixels changed")
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path,
                        default=Path(__file__).resolve().parent.parent / "builtinFonts")
    args = parser.parse_args()
    outputs = []
    for size in (8, 10, 12):
        name = f"notosans_cjk_{size}"
        path = args.directory / f"{name}.h"
        outputs.append((path, compressed_header(path.read_text(), name)))
    for path, output in outputs:
        if path.read_text() == output:
            continue
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(output)
        temporary.replace(path)


if __name__ == "__main__":
    main()
