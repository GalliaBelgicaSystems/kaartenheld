#!/usr/bin/env python3
"""Shared level collision + whole-edge-neighbor helpers.

Single source of truth for the compiler (`compile.py`), the validator
(`validate.py`), and the walkthrough planner (`tools/walkthrough/route.py`).
Kept out of `compile.py` so `validate.py` can use them without the
compile <-> validate import cycle; `compile.py` re-exports every name so
existing importers (decompile.py, route.py) keep working unchanged.

The neighbor model (see docs/level-editor.md Phase 20): stepping onto a
walkable cell of a linked border crosses to the neighbor scene.  The
player lands one cell INSIDE the neighbor's opposite edge at the mirrored
coordinate.  `mirrored_entry()` is the one Python mirror of the ROM's
`world_edge_spawn_banked`; `edge_link_report()` checks that every crossing
has a walkable landing cell, so a trap can never be built.
"""

from scene_registry import load_registry

# Whole-edge map links (the "ocean"): neighbors JSON -> {dir: sid or None}.
# Unknown non-empty targets fail LOUDLY with the human fix (same contract
# as exit targets) instead of emitting guessed C identifiers.
NEIGHBOR_DIRS = ("north", "south", "east", "west")

_OPPOSITE = {"north": "south", "south": "north", "east": "west", "west": "east"}


def opposite(direction):
    return _OPPOSITE[direction]


def load_level(path):
    """Load and parse level JSON."""
    import json
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def first_plain_tile(tileset_id, tileset):
    """First tile id with 'plain' in the name (manifest order)."""
    for t in tileset.get("tiles", []):
        if "plain" in t.get("id", ""):
            return "%s.%s" % (tileset_id, t["id"])
    return None


def level_default_tile(level_data, tilesets):
    """The level's ground tile for unpainted cells: the explicit
    default_walkable field, else the first plain tile in manifest order."""
    tileset_id = level_data["map"]["tileset"]
    tileset = tilesets.get(tileset_id, {})
    explicit = level_data.get("default_walkable", "")
    if explicit:
        return explicit
    return first_plain_tile(tileset_id, tileset) or ""


def resolve_tiles(level_data, tileset):
    """Resolve semantic tile IDs to C constants."""
    tile_dict = {t["id"]: t for t in tileset.get("tiles", [])}
    return tile_dict


def neighbor_targets(level_data, registry=None):
    """Level neighbors JSON -> {dir: target sid or None}."""
    registry = registry or load_registry()
    out = {}
    raw = level_data.get("neighbors", {}) or {}
    for d in NEIGHBOR_DIRS:
        t = (raw.get(d) or "").strip()
        if not t:
            out[d] = None
            continue
        if t not in registry["scenes"] and not t.startswith("test_"):
            raise SystemExit(
                f"ERROR: neighbor '{d}' in '{level_data.get('id')}' targets unknown scene "
                f"'{t}'. Point it at a registered scene "
                f"(or save that level in the editor to register it).")
        out[d] = t
    return out


def cell_tile_info(level_data, tileset, x, y):
    """Manifest tile dict for the art painted at (x, y), or None when the
    cell is unpainted (the ROM fills default ground / perimeter wall)."""
    tile_dict = {t["id"]: t for t in tileset.get("tiles", [])}
    terrain = level_data.get("layers", {}).get("terrain", [])
    if not terrain:
        return None
    if isinstance(terrain[0], list):
        if y < len(terrain) and x < len(terrain[y]):
            return tile_dict.get(terrain[y][x].split(".")[-1])
        return None
    for block in terrain:
        bx = block.get("x", 0)
        by = block.get("y", 0)
        if bx <= x < bx + block.get("width", 0) and by <= y < by + block.get("height", 0):
            return tile_dict.get(block.get("tile", "").split(".")[-1])
    return None


def map_base_tile_const(gb_const, t_info):
    if gb_const in ("TILE_FLOOR", "TILE_WALL", "TILE_EXIT", "TILE_BUILDING",
                    "TILE_STUMP_TL", "TILE_STUMP_TR", "TILE_STUMP_BL", "TILE_STUMP_BR"):
        return gb_const
    if gb_const and gb_const.startswith("TILE_"):
        return gb_const
    ascii_char = t_info.get("ascii", "#")
    if ascii_char == ".":
        return "TILE_FLOOR"
    elif ascii_char == "#":
        return "TILE_WALL"
    elif ascii_char in (">", "<"):
        return "TILE_EXIT"
    elif ascii_char == "B" or ascii_char == "*":
        return "TILE_BUILDING"
    return "TILE_FLOOR" if t_info.get("walkable", True) else "TILE_WALL"


def derive_collision(level_data, tileset):
    """Derive 2D collision grid (True=walkable, False=blocked).

    Mirrors the ROM (scene_load_tiles_banked + the banked walkability
    mirror): unpainted interior is default ground (walkable); unpainted
    perimeter is wall, except unpainted point-exit gates and whole linked
    edges (neighbors), which compile to open-ground rows; painted cells
    follow their manifest walkability, so solid art over a gate blocks it
    (the validator warns loudly about that)."""
    width = level_data["map"]["width"]
    height = level_data["map"]["height"]
    try:
        neighbors = neighbor_targets(level_data)
    except SystemExit:
        neighbors = {}
    linked = {d for d, t in neighbors.items() if t}
    exits = {(e.get("x", -1), e.get("y", -1)) for e in level_data.get("exits", [])}
    grid = [[True for _ in range(width)] for _ in range(height)]
    for y in range(height):
        for x in range(width):
            info = cell_tile_info(level_data, tileset, x, y)
            if (x, y) in exits:
                # Triggers fire regardless of the art painted under them,
                # so every gate cell is a portal node for routing.
                grid[y][x] = True
                continue
            on_perimeter = (x == 0 or y == 0 or x == width - 1 or y == height - 1)
            if not on_perimeter:
                grid[y][x] = True if info is None else bool(info.get("walkable", True))
                continue
            edges = set()
            if x == 0:
                edges.add("west")
            if x == width - 1:
                edges.add("east")
            if y == 0:
                edges.add("north")
            if y == height - 1:
                edges.add("south")
            if info is None:
                grid[y][x] = any(e in linked for e in edges)
            else:
                grid[y][x] = bool(info.get("walkable", True))
    return grid


# ── Edge-neighbor pairing ────────────────────────────────────────────

def _clamp(v, lo, hi):
    return lo if hi < lo else max(lo, min(v, hi))


def mirrored_entry(direction, cross, target_w, target_h):
    """Entry (x, y) one cell inside the target's opposite edge for a
    crossing that leaves via `direction` at `cross` (x for N/S, y for
    E/W).  Python mirror of src/world/edge_banked.c's spawn body."""
    w, h = target_w, target_h
    if direction == "north":
        return (_clamp(cross, 1, w - 2), h - 2)
    if direction == "south":
        return (_clamp(cross, 1, w - 2), 1)
    if direction == "east":
        return (1, _clamp(cross, 1, h - 2))
    return (w - 2, _clamp(cross, 1, h - 2))


def edge_cells(direction, w, h):
    """Border cells along `direction`, in scan order."""
    if direction == "north":
        return [(x, 0) for x in range(w)]
    if direction == "south":
        return [(x, h - 1) for x in range(w)]
    if direction == "west":
        return [(0, y) for y in range(h)]
    return [(w - 1, y) for y in range(h)]


def _cross_coord(direction, cell):
    return cell[0] if direction in ("north", "south") else cell[1]


def _grid(name, lvl, tilesets, cache):
    if cache is None:
        return derive_collision(lvl, tilesets.get(lvl["map"]["tileset"], {}))
    if name not in cache:
        cache[name] = derive_collision(lvl, tilesets.get(lvl["map"]["tileset"], {}))
    return cache[name]


def _fmt_stuck(source, direction, into, stuck):
    exs = ", ".join("%s->%s" % (c, e) for c, e in stuck[:4])
    more = "" if len(stuck) <= 4 else " (+%d more)" % (len(stuck) - 4)
    return (f"crossing {source} {direction} at {exs}{more} lands in a wall in "
            f"{into}. Open {into} along that edge (whole-edge corridor) or wall "
            f"{source}'s crossing cells.")


def edge_link_report(name, lvl, levels_by_id, tilesets, grid_cache=None):
    """Per-direction diagnostics for one level's whole-edge links.

    Returns a list of dicts {direction, target, ok, errors, warnings};
    every message is fully self-describing (level + coordinates).  This is
    the one implementation used by the validator, the compiler gate, and
    the editor's /api/neighbor-check."""
    report = []
    try:
        targets = neighbor_targets(lvl)
    except SystemExit as exc:
        return [{"direction": None, "target": None, "ok": False,
                 "errors": [str(exc)], "warnings": []}]
    w, h = lvl["map"]["width"], lvl["map"]["height"]
    for direction in NEIGHBOR_DIRS:
        target = targets.get(direction)
        if not target:
            continue
        entry = {"direction": direction, "target": target, "ok": True,
                 "errors": [], "warnings": []}
        if target not in levels_by_id:
            entry["warnings"].append(
                f"{name}: edge '{direction}' -> '{target}': target not in this "
                f"compile set (cannot verify the landing cells)")
            report.append(entry)
            continue
        tlvl = levels_by_id[target]
        tw, th = tlvl["map"]["width"], tlvl["map"]["height"]
        ag = _grid(name, lvl, tilesets, grid_cache)
        bg = _grid(target, tlvl, tilesets, grid_cache)
        crossings = [(_cross_coord(direction, c), c)
                     for c in edge_cells(direction, w, h) if ag[c[1]][c[0]]]
        if not crossings:
            entry["warnings"].append(
                f"{name}: edge '{direction}' -> '{target}' never fires: "
                f"{name}'s {direction} edge has no walkable cell "
                f"(paint an opening or remove the link)")
        stuck = []
        for cross, cell in crossings:
            ex, ey = mirrored_entry(direction, cross, tw, th)
            if not bg[ey][ex]:
                stuck.append((cell, (ex, ey)))
        if stuck:
            entry["ok"] = False
            entry["errors"].append(
                f"{name}: edge '{direction}' -> '{target}': "
                + _fmt_stuck(name, direction, f"'{target}'", stuck))
        try:
            back = neighbor_targets(tlvl).get(_OPPOSITE[direction])
        except SystemExit:
            back = None
        if back == name:
            rdir = _OPPOSITE[direction]
            rcross = [(_cross_coord(rdir, c), c)
                      for c in edge_cells(rdir, tw, th) if bg[c[1]][c[0]]]
            if not rcross:
                entry["ok"] = False
                entry["errors"].append(
                    f"{name}: edge '{direction}' -> '{target}': '{target}' declares "
                    f"the return link ({rdir} -> {name}) but its {rdir} edge has no "
                    f"walkable cell -- the player cannot return.")
            else:
                rstick = []
                for cross, cell in rcross:
                    ex, ey = mirrored_entry(rdir, cross, w, h)
                    if not ag[ey][ex]:
                        rstick.append((cell, (ex, ey)))
                if rstick:
                    entry["ok"] = False
                    entry["errors"].append(
                        f"{name}: edge '{direction}' -> '{target}': "
                        + _fmt_stuck(target, rdir, f"'{name}'", rstick)
                        + f" (return)")
        report.append(entry)
    return report


def neighbor_pairing_issues(levels_by_id, tilesets):
    """(errors, warnings) for every level's whole-edge links."""
    errors, warnings = [], []
    cache = {}
    for name, lvl in levels_by_id.items():
        for entry in edge_link_report(name, lvl, levels_by_id, tilesets, cache):
            errors.extend(entry["errors"])
            warnings.extend(entry["warnings"])
    return errors, warnings


# ── Point-exit triggers ──────────────────────────────────────────────
# An exit keeps the terrain art painted on its cell; unpainted cells become
# default ground.  A trigger on plain ground is invisible to the player, so
# the toolchain warns unless the art reads as a portal.

_PORTAL_KEYWORDS = ("stair", "door", "gate", "portal", "exit", "cave",
                    "warp", "ladder")


def is_portal_art(info):
    """True when a manifest tile reads as an intentional portal marker."""
    if info is None:
        return False
    if info.get("category") == "exit":
        return True
    tid = (info.get("id") or "").lower()
    return any(k in tid for k in _PORTAL_KEYWORDS)


def point_exit_report(name, lvl, levels_by_id, tilesets, grid_cache=None):
    """Per-exit diagnostics: visibility + landing-cell walkability.

    Returns a list of dicts {index, target, ok, errors, warnings}; every
    message is fully self-describing (level + coordinates).  Mirrors the
    engine: a point exit fires before terrain walkability, so a solid gate
    is not blocked -- but it must look like a portal, and the destination
    must be walkable or the player spawns stuck."""
    out = []
    tset = tilesets.get(lvl["map"]["tileset"], {})
    for i, e in enumerate(lvl.get("exits", [])):
        entry = {"index": i, "target": e.get("target_scene"), "ok": True,
                 "errors": [], "warnings": []}
        info = cell_tile_info(lvl, tset, e.get("x", -1), e.get("y", -1))
        if not is_portal_art(info):
            art = info["id"] if info else "unpainted ground"
            entry["warnings"].append(
                f"{name}: exit #{i} at ({e.get('x')},{e.get('y')}) sits on "
                f"'{art}', which does not read as a portal -- the trigger is "
                f"invisible in-game. Paint the tileset's exit/stairs tile or "
                f"remove the exit.")
        target = e.get("target_scene")
        if target in levels_by_id:
            tlvl = levels_by_id[target]
            g = _grid(target, tlvl, tilesets, grid_cache)
            tx, ty = e.get("target_x", -1), e.get("target_y", -1)
            if not (0 <= tx < tlvl["map"]["width"]
                    and 0 <= ty < tlvl["map"]["height"] and g[ty][tx]):
                entry["ok"] = False
                entry["errors"].append(
                    f"{name}: exit #{i} at ({e.get('x')},{e.get('y')}) -> "
                    f"'{target}' lands on ({tx},{ty}), which is not walkable "
                    f"-- the player would spawn stuck.")
        out.append(entry)
    return out


def point_exit_issues(levels_by_id, tilesets):
    """(errors, warnings) for every level's point-exit triggers."""
    errors, warnings = [], []
    cache = {}
    for name, lvl in levels_by_id.items():
        for entry in point_exit_report(name, lvl, levels_by_id, tilesets, cache):
            errors.extend(entry["errors"])
            warnings.extend(entry["warnings"])
    return errors, warnings
