#!/usr/bin/env python3
"""png2gb.py -- PNG -> Game Boy 2bpp tile data converter.

Implements the pipeline stage specified in docs/graphics.md:

    PNG source assets
          |  tools/png2gb.py
    validation (dimensions, 8x8 tile alignment, palette limits, sprite
                size, unsupported colors, duplicate tiles, tile counts)
          |
    GB-native data (tileset bytes, tilemaps, OAM sprite defs, palettes)

Supports canonical DMG grayscale, classic GB green, and custom 4-shade palettes,
with optional extraction of specific tile coordinate lists.
"""

import sys
import argparse
import json
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("png2gb.py: requires Pillow (`pip install Pillow`)")

MAX_COLORS = 4
TILE_SIZE = 8

PALETTES = {
    "canonical": [
        (255, 255, 255),  # 0: white / lightest
        (170, 170, 170),  # 1: light gray
        (85, 85, 85),     # 2: dark gray
        (0, 0, 0),        # 3: black / darkest
    ],
    "gb_green": [
        (224, 248, 207),  # 0: lightest green (#E0F8CF)
        (134, 192, 108),  # 1: light green (#86C06C)
        (48, 104, 80),    # 2: dark green (#306850)
        (7, 24, 33),      # 3: darkest green (#071821)
    ]
}


class Png2GbError(Exception):
    """Validation failure: (asset path, rule violated, detail)."""
    def __init__(self, asset, rule, detail):
        self.asset = asset
        self.rule = rule
        self.detail = detail
        super().__init__(f"{asset}: [{rule}] {detail}")


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def load_and_validate(path, max_colors=MAX_COLORS, allow_per_tile=False):
    """Load a PNG and validate it against the GB tile constraints.
    Returns (PIL.Image in RGB, tiles_x, tiles_y)."""
    asset = str(path)
    try:
        img = Image.open(path)
    except Exception as e:
        raise Png2GbError(asset, "unreadable", str(e))

    img = img.convert("RGB")
    w, h = img.size

    if w % TILE_SIZE != 0 or h % TILE_SIZE != 0:
        raise Png2GbError(
            asset, "tile-alignment",
            f"{w}x{h} is not a multiple of {TILE_SIZE}x{TILE_SIZE} "
            f"(GB tiles are {TILE_SIZE}x{TILE_SIZE} pixels)"
        )

    if allow_per_tile:
        # Validate that each 8x8 tile has at most 4 colors
        px = img.load()
        tiles_x, tiles_y = w // TILE_SIZE, h // TILE_SIZE
        for ty in range(tiles_y):
            for tx in range(tiles_x):
                ox, oy = tx * TILE_SIZE, ty * TILE_SIZE
                t_cols = {px[ox + x, oy + y] for y in range(TILE_SIZE) for x in range(TILE_SIZE)}
                if len(t_cols) > 4:
                    raise Png2GbError(
                        asset, "palette-limit",
                        f"tile ({tx},{ty}) uses {len(t_cols)} colors; max is 4 (2bpp)"
                    )
        return img, tiles_x, tiles_y

    colors = img.getcolors(maxcolors=256)
    if colors is None or len(colors) > max_colors:
        n = "more than 256" if colors is None else str(len(colors))
        raise Png2GbError(
            asset, "palette-limit",
            f"image uses {n} distinct colors; GB tiles support at most "
            f"{max_colors} (2bpp)"
        )

    return img, w // TILE_SIZE, h // TILE_SIZE


def build_shade_map(img, asset, palette_name="canonical"):
    """Map each distinct color in the image to its exact GB shade index (0-3)
    against the requested 4-shade palette. Fails strictly on any off-palette pixel."""
    palette = PALETTES.get(palette_name, PALETTES["canonical"])
    colors = [c for _, c in img.getcolors(maxcolors=256)]
    shade_map = {}
    for color in colors:
        if color in palette:
            shade_map[color] = palette.index(color)
        else:
            raise Png2GbError(
                asset, "unsupported-color",
                f"RGB{color} is not in the {palette_name} palette {palette}"
            )
    return shade_map


def _assign_remaining_shades(colors):
    """Assign shade indices to a list of colors (already sorted brightest-first),
    using the existing contrast-maximising skip pattern:
      1 color  -> {0}
      2 colors -> {0, 3}
      3 colors -> {0, 2, 3}
      4 colors -> {0, 1, 2, 3}
    """
    n = len(colors)
    if n <= 1:
        return {colors[0]: 0}
    elif n == 2:
        return {colors[0]: 0, colors[1]: 3}
    elif n == 3:
        return {colors[0]: 0, colors[1]: 2, colors[2]: 3}
    else:
        return {colors[0]: 0, colors[1]: 1, colors[2]: 2, colors[3]: 3}


def get_tile_shade_map(img, tile_x, tile_y, anchor_color=None):
    """Build a {RGB_tuple: shade_index} map for one 8x8 tile.

    If anchor_color is set (an RGB tuple), that color is pinned to index 0
    regardless of luminance. Remaining colors are sorted by luminance
    (brightest first) and assigned to the remaining indices using the
    contrast-maximising skip pattern (e.g. 1 remaining -> {3},
    2 remaining -> {2, 3}, 3 remaining -> {1, 2, 3}).
    """
    px = img.load()
    ox, oy = tile_x * TILE_SIZE, tile_y * TILE_SIZE
    unique = {px[ox + col, oy + row] for row in range(TILE_SIZE) for col in range(TILE_SIZE)}

    if anchor_color is not None and anchor_color in unique:
        # Pin anchor to index 0; sort and assign remaining colors
        rest = sorted([c for c in unique if c != anchor_color], key=lum, reverse=True)
        shade_map = {anchor_color: 0}
        n = len(rest)
        if n == 1:
            shade_map[rest[0]] = 3
        elif n == 2:
            shade_map[rest[0]] = 2
            shade_map[rest[1]] = 3
        elif n == 3:
            shade_map[rest[0]] = 1
            shade_map[rest[1]] = 2
            shade_map[rest[2]] = 3
        return shade_map

    # Default: sort all colors by luminance (brightest = index 0)
    colors = sorted(list(unique), key=lum, reverse=True)
    return _assign_remaining_shades(colors)


def load_shade_map(path, asset):
    """Load a strict per-tile shade sidecar: {"tx,ty": {"#hex": shade}}.

    Generated by palette_compiler.py from the tileset JSON ramp assignment
    + palette.txt slot colors (see generated/tiles/<set>_shades.json).
    Pixel colors resolve by exact value -- no luminance guessing."""
    try:
        raw = json.loads(Path(path).read_text())
    except (OSError, ValueError) as e:
        raise Png2GbError(asset, "shade-map", f"cannot load {path}: {e}")
    out = {}
    for key, mapping in raw.items():
        try:
            tx, ty = (int(p) for p in str(key).split(","))
        except ValueError:
            raise Png2GbError(asset, "shade-map", f"bad tile key {key!r} (want 'tx,ty')")
        cell = {}
        for hexcol, shade in mapping.items():
            try:
                rgb = parse_hex_color(hexcol)
            except ValueError as e:
                raise Png2GbError(asset, "shade-map", f"tile {key}: {e}")
            if not isinstance(shade, int) or not 0 <= shade <= 3:
                raise Png2GbError(asset, "shade-map",
                                  f"tile {key}: shade {shade!r} for {hexcol} outside 0-3")
            cell[rgb] = shade
        out[(tx, ty)] = cell
    return out


def strict_tile_shade_map(img, tile_x, tile_y, cell_map, asset, anchor_color=None):
    """Exact shade map for one tile from the sidecar (no luminance sort).

    Every pixel's color must be listed; the anchor (when present in the
    tile) must map to shade 0. Anything else is a hard error naming the
    tile and the offending color."""
    px = img.load()
    ox, oy = tile_x * TILE_SIZE, tile_y * TILE_SIZE
    unique = {px[ox + col, oy + row] for row in range(TILE_SIZE) for col in range(TILE_SIZE)}
    shade_map = {}
    for color in unique:
        if color not in cell_map:
            raise Png2GbError(
                asset, "off-ramp-pixel",
                f"tile ({tile_x},{tile_y}) uses #{color[0]:02x}{color[1]:02x}{color[2]:02x}, "
                f"which is not in its ramp -- repaint the pixel or repoint the tile's "
                f"\"palette\" in tools/level_editor/tilesets/*.json")
        shade_map[color] = cell_map[color]
    if anchor_color is not None and anchor_color in shade_map and shade_map[anchor_color] != 0:
        raise Png2GbError(
            asset, "anchor-mismatch",
            f"tile ({tile_x},{tile_y}): anchor maps to shade {shade_map[anchor_color]}, want 0")
    return shade_map


def encode_tile(img, tile_x, tile_y, shade_map, anchor_color=None):
    """Encode one 8x8 tile block starting at (tile_x*8, tile_y*8) into
    16 bytes of GB 2bpp tile data."""
    if shade_map is None:
        shade_map = get_tile_shade_map(img, tile_x, tile_y, anchor_color=anchor_color)
    px = img.load()
    out = bytearray()
    ox, oy = tile_x * TILE_SIZE, tile_y * TILE_SIZE
    for row in range(TILE_SIZE):
        lo = 0
        hi = 0
        for col in range(TILE_SIZE):
            shade = shade_map[px[ox + col, oy + row]]
            bit_pos = 7 - col
            if shade & 0b01:
                lo |= (1 << bit_pos)
            if shade & 0b10:
                hi |= (1 << bit_pos)
        out.append(lo)
        out.append(hi)
    return bytes(out)


def ascii_preview(tile_bytes):
    """Render a tile's on/off pattern as an ASCII comment."""
    lines = []
    for row in range(TILE_SIZE):
        lo = tile_bytes[row * 2]
        hi = tile_bytes[row * 2 + 1]
        chars = []
        for col in range(TILE_SIZE):
            bit_pos = 7 - col
            shade = (((hi >> bit_pos) & 1) << 1) | ((lo >> bit_pos) & 1)
            chars.append(" .:#"[shade])
        lines.append("".join(chars))
    return lines


def format_c_array(name, all_tile_bytes, tile_count, raw_inc=False):
    """Emit a C byte array with an ASCII-art comment per tile row."""
    lines = []
    if not raw_inc:
        lines.append(f"const uint8_t {name}[{len(all_tile_bytes)}] = {{")
    for t in range(tile_count):
        tile = all_tile_bytes[t * 16:(t + 1) * 16]
        preview = ascii_preview(tile)
        if tile_count > 1:
            lines.append(f"    /* tile {t} */")
        for row in range(TILE_SIZE):
            lo, hi = tile[row * 2], tile[row * 2 + 1]
            comma = "," if not (not raw_inc and t == tile_count - 1 and row == TILE_SIZE - 1) else ""
            lines.append(f"    0x{lo:02X}, 0x{hi:02X}{comma}   /* {preview[row]} */")
    if not raw_inc:
        lines.append("};")
    return "\n".join(lines)


def parse_hex_color(s):
    """Parse a hex color string like '#7bb660' or '7bb660' into an RGB tuple."""
    s = s.lstrip('#')
    if len(s) != 6:
        raise ValueError(f"expected 6-digit hex color, got '{s}'")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def convert(path, name, palette_name="canonical", tile_coords=None, raw_inc=False,
            anchor_color=None, shade_map_path=None):
    is_auto = (palette_name == "auto")
    img, tiles_x, tiles_y = load_and_validate(path, max_colors=MAX_COLORS, allow_per_tile=is_auto)
    shade_map = None if is_auto else build_shade_map(img, str(path), palette_name=palette_name)
    sidecar = load_shade_map(shade_map_path, str(path)) if shade_map_path else None

    def tile_map(tx, ty):
        if sidecar is not None and (tx, ty) in sidecar:
            return strict_tile_shade_map(img, tx, ty, sidecar[(tx, ty)], str(path),
                                         anchor_color=anchor_color)
        if sidecar is not None:
            raise Png2GbError(str(path), "shade-map",
                              f"tile ({tx},{ty}) has no sidecar entry -- add it to "
                              f"generated/tiles/<set>_shades.json via make manifest")
        return None if is_auto else shade_map

    all_bytes = bytearray()
    if tile_coords:
        coords_list = []
        for item in tile_coords.strip().split():
            parts = item.split(",")
            coords_list.append((int(parts[0]), int(parts[1])))
        for tx, ty in coords_list:
            explicit = tile_map(tx, ty)
            all_bytes += encode_tile(img, tx, ty,
                                     explicit if explicit is not None else shade_map,
                                     anchor_color=anchor_color)
        tile_count = len(coords_list)
    else:
        for ty in range(tiles_y):
            for tx in range(tiles_x):
                explicit = tile_map(tx, ty)
                all_bytes += encode_tile(img, tx, ty,
                                         explicit if explicit is not None else shade_map,
                                         anchor_color=anchor_color)
        tile_count = tiles_x * tiles_y

    return all_bytes, tile_count, format_c_array(name, all_bytes, tile_count, raw_inc=raw_inc)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("png", type=Path, help="source PNG")
    ap.add_argument("--name", default="tile_data", help="C array name")
    ap.add_argument("--palette", default="canonical", choices=["canonical", "gb_green", "auto"], help="palette mapping")
    ap.add_argument("--anchor-color", default=None, metavar="HEX",
                     help="pin this hex color (e.g. '#7bb660') to shade index 0 "
                          "in every tile (requires --palette auto). Used to enforce "
                          "harmonized CGB Color 0 across a tileset.")
    ap.add_argument("--tile-coords", default=None, help="space-separated x,y tile coordinates (e.g. '1,2 8,1 8,2 0,5')")
    ap.add_argument("--shade-map", default=None, metavar="JSON",
                     help="strict per-tile shade sidecar {\"tx,ty\": {\"#hex\": shade}} "
                          "(generated/tiles/<set>_shades.json via make manifest). "
                          "Covered tiles resolve colors by exact value -- no luminance "
                          "guessing; uncovered tiles fail loudly.")
    ap.add_argument("--raw", action="store_true", help="output raw comma-separated byte lines suitable for #include inside an array initializer")
    ap.add_argument("-o", "--out", type=Path, help="write generated C snippet here (default: stdout)")
    args = ap.parse_args()

    anchor_color = None
    if args.anchor_color:
        if args.palette != "auto":
            print("png2gb: --anchor-color requires --palette auto", file=sys.stderr)
            sys.exit(1)
        try:
            anchor_color = parse_hex_color(args.anchor_color)
        except ValueError as e:
            print(f"png2gb: --anchor-color: {e}", file=sys.stderr)
            sys.exit(1)

    try:
        all_bytes, tile_count, c_src = convert(
            args.png, args.name,
            palette_name=args.palette,
            tile_coords=args.tile_coords,
            raw_inc=args.raw,
            anchor_color=anchor_color,
            shade_map_path=args.shade_map
        )
    except Png2GbError as e:
        print(f"png2gb: {e.asset}: [{e.rule}] {e.detail}", file=sys.stderr)
        sys.exit(1)

    header = (
        f"/* Generated by tools/png2gb.py from {args.png.name} "
        f"({tile_count} tile{'s' if tile_count != 1 else ''}). */\n"
    )
    output = header + c_src + "\n"

    if args.out:
        args.out.write_text(output)
        print(f"png2gb: wrote {args.out} ({len(all_bytes)} bytes, {tile_count} tile(s))", file=sys.stderr)
    else:
        print(output)


if __name__ == "__main__":
    main()
