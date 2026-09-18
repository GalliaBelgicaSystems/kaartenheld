#!/usr/bin/env python3
"""png2gb.py -- PNG -> Game Boy 2bpp tile data converter.

Two modes, no guessing in either:

1. --shade-map <json>: per-tile ordered shade lists from
   generated/tiles/shades/<sheet>.json (palette_compiler decides the ramp
   per tile; ramps always win there). Each pixel maps to its exact shade
   index, else the nearest shade WITHIN that tile's ramp. A tile missing
   from the map is a hard pipeline error. This is the mode every CGB art
   rule uses.

2. --palette canonical|gb_green: strict byte-exact match against a fixed
   4-shade palette (font art). Any off-palette pixel fails loudly.

There is no luminance sorting, no anchor pinning, no color-distance
matching across palettes, no quantization. Those lived here before and
are gone: ramp choice belongs to palette_compiler, which reports it.
"""

import sys
import argparse
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("png2gb.py: requires Pillow (`pip install Pillow`)")

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


def load_image(path):
    """Load a PNG, validate 8x8 tile alignment. Returns (img, tx, ty)."""
    asset = str(path)
    try:
        img = Image.open(path).convert("RGB")
    except Exception as e:
        raise Png2GbError(asset, "unreadable", str(e))
    w, h = img.size
    if w % TILE_SIZE != 0 or h % TILE_SIZE != 0:
        raise Png2GbError(
            asset, "tile-alignment",
            f"{w}x{h} is not a multiple of {TILE_SIZE}x{TILE_SIZE} "
            f"(GB tiles are {TILE_SIZE}x{TILE_SIZE} pixels)"
        )
    return img, w // TILE_SIZE, h // TILE_SIZE


def parse_hex(s):
    s = s.lstrip('#')
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def load_shade_map(path, asset):
    """Load a palette_compiler shades file: {"tiles": {"x,y": [hex x4]}}."""
    import json
    try:
        data = json.loads(Path(path).read_text())
    except Exception as e:
        raise Png2GbError(asset, "shade-map-unreadable", f"{path}: {e}")
    tiles = data.get("tiles")
    if not isinstance(tiles, dict):
        raise Png2GbError(asset, "shade-map-malformed", f"{path}: want 'tiles' object")
    out = {}
    for key, hexes in tiles.items():
        try:
            tx, ty = (int(v) for v in key.split(","))
            shades = tuple(parse_hex(h) for h in hexes)
        except Exception:
            raise Png2GbError(asset, "shade-map-malformed", f"{path}: bad entry '{key}'")
        if len(shades) != 4:
            raise Png2GbError(asset, "shade-map-malformed",
                              f"{path}: tile '{key}' wants 4 shades, got {len(shades)}")
        out[(tx, ty)] = shades
    return out


def nearest_shade(color, shades):
    """Map a pixel to a shade index.

    shades as an ORDERED 4-tuple/list: position is the shade index;
    exact match wins, else nearest within the ramp (ramps-win encoding).
    shades as a DICT {color: index}: exact table lookup, missing pixel
    is a loud error (fixed art like the splash logo).
    """
    if isinstance(shades, dict):
        try:
            return shades[color]
        except KeyError:
            raise Png2GbError("", "unsupported-color",
                              f"pixel RGB{color} not in the fixed shade table")
    for i, s in enumerate(shades):
        if color == s:
            return i
    best, best_d = 0, None
    for i, s in enumerate(shades):
        d = (color[0] - s[0]) ** 2 + (color[1] - s[1]) ** 2 + (color[2] - s[2]) ** 2
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


def encode_tile(img, tile_x, tile_y, shades):
    """Encode one 8x8 tile block into 16 bytes of GB 2bpp tile data."""
    px = img.load()
    out = bytearray()
    ox, oy = tile_x * TILE_SIZE, tile_y * TILE_SIZE
    for row in range(TILE_SIZE):
        lo = 0
        hi = 0
        for col in range(TILE_SIZE):
            shade = nearest_shade(px[ox + col, oy + row], shades)
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


def convert(path, name, palette_name="canonical", tile_coords=None, raw_inc=False,
            shade_map=None):
    img, tiles_x, tiles_y = load_image(path)
    asset = str(path)

    if shade_map is not None:
        per_tile = load_shade_map(shade_map, asset)
    else:
        palette = PALETTES.get(palette_name)
        if palette is None:
            raise Png2GbError(asset, "unknown-palette", palette_name)
        colors = img.getcolors(maxcolors=256) or []
        if len(colors) > 4:
            raise Png2GbError(asset, "palette-limit",
                              f"image uses {len(colors)} distinct colors; max is 4 (2bpp)")
        per_tile = None

    def shades_for(tx, ty):
        if per_tile is not None:
            try:
                return per_tile[(tx, ty)]
            except KeyError:
                raise Png2GbError(asset, "shade-map-missing",
                                  f"tile ({tx},{ty}) has no entry in {shade_map} "
                                  f"(palette_compiler must cover every encoded tile)")
        shades = PALETTES[palette_name]
        px = img.load()
        ox, oy = tx * TILE_SIZE, ty * TILE_SIZE
        for row in range(TILE_SIZE):
            for col in range(TILE_SIZE):
                if px[ox + col, oy + row] not in shades:
                    raise Png2GbError(asset, "unsupported-color",
                                      f"tile ({tx},{ty}) pixel RGB{px[ox + col, oy + row]} "
                                      f"not in the {palette_name} palette")
        return shades

    if tile_coords:
        coords_list = []
        for item in tile_coords.strip().split():
            parts = item.split(",")
            coords_list.append((int(parts[0]), int(parts[1])))
    else:
        coords_list = [(tx, ty) for ty in range(tiles_y) for tx in range(tiles_x)]

    all_bytes = bytearray()
    for tx, ty in coords_list:
        if not (0 <= tx < tiles_x and 0 <= ty < tiles_y):
            raise Png2GbError(asset, "tile-coords",
                              f"tile ({tx},{ty}) outside sheet {tiles_x}x{tiles_y}")
        all_bytes += encode_tile(img, tx, ty, shades_for(tx, ty))
    tile_count = len(coords_list)

    return all_bytes, tile_count, format_c_array(name, all_bytes, tile_count, raw_inc=raw_inc)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("png", type=Path, help="source PNG")
    ap.add_argument("--name", default="tile_data", help="C array name")
    ap.add_argument("--palette", default="canonical", choices=["canonical", "gb_green"],
                    help="strict fixed palette (default: canonical)")
    ap.add_argument("--shade-map", default=None, metavar="JSON",
                    help="palette_compiler shades file; every encoded tile "
                         "must have an entry (CGB art mode)")
    ap.add_argument("--tile-coords", default=None, help="space-separated x,y tile coordinates (e.g. '1,2 8,1 8,2 0,5')")
    ap.add_argument("--raw", action="store_true", help="output raw comma-separated byte lines suitable for #include inside an array initializer")
    ap.add_argument("-o", "--out", type=Path, help="write generated C snippet here (default: stdout)")
    args = ap.parse_args()

    try:
        all_bytes, tile_count, c_src = convert(
            args.png, args.name,
            palette_name=args.palette,
            tile_coords=args.tile_coords,
            raw_inc=args.raw,
            shade_map=args.shade_map,
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
