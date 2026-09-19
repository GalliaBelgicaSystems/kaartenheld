#!/usr/bin/env python3
"""palette_parse.py -- Dumb parser for assets/palette.txt.

The artist file holds exactly two things:

1. Color definitions:  `name: #rrggbb`   (under a SECTION header)
2. Ramp rows:          `name: SECTION/ref, SECTION/ref, SECTION/ref, SECTION/ref`
   (`UNUSED` is a valid ref meaning "repeat the previous shade").

That is all. No slot tables, no anchors, no inference, no padding, no
aliases. Set membership (which ramp drives which hardware slot) lives in
tools/palette_slots.json; per-tile ramp choice lives in the tileset JSONs.

Returns (colors, ramps):
  colors: {SECTION: {name: (r, g, b)}}
  ramps:  {rampname: [(r, g, b) x 4]}  (UNUSED already expanded)

Anything else is a hard error naming file + line number.
"""

from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PALETTE_TXT = REPO_ROOT / "assets" / "palette.txt"


def _hex_to_rgb(h, where):
    h = h.strip()
    if h.startswith("#"):
        h = h[1:]
    if len(h) != 6:
        raise ValueError(f"{where}: want 6-digit hex, got '{h}'")
    try:
        return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
    except ValueError:
        raise ValueError(f"{where}: invalid hex color '{h}'")


def parse_palette(path=PALETTE_TXT):
    """Parse palette.txt. Returns (colors, ramps). Raises ValueError."""
    colors = {}
    ramps = {}
    section = None
    try:
        lines = Path(path).read_text().splitlines()
    except OSError as e:
        raise ValueError(f"cannot read {path}: {e}")
    for lineno, raw in enumerate(lines, 1):
        where = f"{path}:{lineno}"
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            # Section header (FOREST, RAMPS/BATTLE, DEBUG, ...). Only
            # color definitions care about it; ramps are self-qualified.
            section = line
            continue
        name, _, value = line.partition(":")
        name = name.strip()
        value = value.strip()
        if not name:
            raise ValueError(f"{where}: empty name")
        if value.startswith("#") and "," not in value:
            # Color definition.
            if section is None:
                raise ValueError(f"{where}: color '{name}' outside any section")
            rgb = _hex_to_rgb(value, where)
            colors.setdefault(section, {})
            if name in colors[section]:
                raise ValueError(f"{where}: duplicate color '{section}/{name}'")
            colors[section][name] = rgb
        elif "," in value:
            # Ramp row.
            if name in ramps:
                raise ValueError(f"{where}: duplicate ramp '{name}'")
            refs = [r.strip() for r in value.split(",")]
            if len(refs) != 4:
                raise ValueError(f"{where}: ramp '{name}' wants 4 refs, got {len(refs)}")
            resolved = []
            for ref in refs:
                if ref == "UNUSED":
                    if not resolved:
                        raise ValueError(f"{where}: ramp '{name}': UNUSED in first position")
                    resolved.append(resolved[-1])
                    continue
                parts = ref.split("/")
                if len(parts) != 2 or not parts[0].strip() or not parts[1].strip():
                    raise ValueError(f"{where}: ramp '{name}': bad ref '{ref}' (want SECTION/name)")
                sec, cname = parts[0].strip(), parts[1].strip()
                try:
                    resolved.append(colors[sec][cname])
                except KeyError:
                    raise ValueError(f"{where}: ramp '{name}': unknown color '{ref}'")
            ramps[name] = resolved
        else:
            raise ValueError(f"{where}: cannot parse line '{raw.strip()}'")
    return colors, ramps


def main():
    import json
    colors, ramps = parse_palette()
    n_colors = sum(len(v) for v in colors.values())
    print(f"palette.txt: {n_colors} colors in {len(colors)} sections, {len(ramps)} ramps")
    print(json.dumps({k: len(v) for k, v in sorted(colors.items())}, indent=1))


if __name__ == "__main__":
    main()
