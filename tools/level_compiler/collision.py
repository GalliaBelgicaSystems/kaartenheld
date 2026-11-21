#!/usr/bin/env python3
"""Shared level collision + whole-edge-neighbor helpers.

Single source of truth for the compiler (`compile.py`), the validator
(`validate.py`), and the walkthrough planner (`tools/walkthrough/route.py`).
Kept out of `compile.py` so `validate.py` can use them without the
compile <-> validate import cycle; `compile.py` re-exports every name so
existing importers (decompile.py, route.py) keep working unchanged.

The neighbor model (see docs/level-editor.md Phase 20): stepping onto a
walkable cell of a linked border crosses to the neighbor scene.  The
player lands on the neighbor's side entry for the arrival side
(player.entry.<side>, per-level Spawn-layer property), falling back to
the main spawn (player.spawn) when that entry is unset -- no row
mirroring, so linked levels need no edge alignment.
`resolve_entry_spawn()` is the one Python mirror of the ROM's edge-entry
rule (world_update_move's EDGE path); `edge_link_report()` checks that
every crossing has a walkable source cell and every used entry has a
walkable landing, so a trap can never be built.  Point exits (stairs,
tunnels) keep explicit target_x/target_y, and may name a destination
entry via `target_entry` instead of duplicating its coordinates.
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


# ── Actor flags ──────────────────────────────────────────────────────
# The compiler/ROM split actors into hostile (World.actors / MAX_WORLD_
# ACTORS) vs static (g_static_actors / MAX_STATIC_ACTORS) by these flags,
# defaulting by object type when `flags` is absent.  This is the single
# source: compile.py emits from it and validate.py classifies from it.

def default_actor_flags(otype):
    if otype == "enemy":
        return ["HOSTILE", "BLOCKING", "INTERACTABLE"]
    return ["BLOCKING", "INTERACTABLE"]


def effective_actor_flags(otype, props):
    """Flags as the ROM will see them: explicit `flags` wins (an explicit
    `[]` stays empty), else the type default.  Matches emit_actor_row."""
    flags = (props or {}).get("flags")
    return default_actor_flags(otype) if flags is None else flags


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

# ── Named entry spawns ─────────────────────────────────────────────
# player.entry holds optional per-arrival spawn points, each {x, y,
# facing}.  Edge entries (north/south/east/west) are selected by the side
# of the destination the player arrives on: crossing X's east edge into Y
# lands on Y's WEST entry (opposite of the crossed direction).  Stair
# entries (stairs_up/stairs_down) are selected explicitly by point exits
# via target_entry.  A missing entry falls back to the main spawn
# (player.spawn), which stays the new-game/initial spawn and the single
# source of truth for unset arrivals.
EDGE_ENTRY_KEYS = ("north", "south", "east", "west")
STAIR_ENTRY_KEYS = ("stairs_up", "stairs_down")
ENTRY_KEYS = EDGE_ENTRY_KEYS + STAIR_ENTRY_KEYS

# Absent-entry facing byte in the compiled SceneDefinition row, mirroring
# SCENE_ENTRY_ABSENT (src/world/scene.h).  Never a valid Direction value.
ENTRY_ABSENT_FACING = 0xFF


def entry_side_for_link(direction):
    """Entry key used on the destination for a link crossed via
    `direction`: the arrival side (opposite of the crossed edge)."""
    return _OPPOSITE[direction]


def get_entry(level_data, key):
    """The named entry dict for `key`, or None when unset/invalid."""
    if key not in ENTRY_KEYS:
        return None
    entry = ((level_data.get("player", {}) or {}).get("entry", {}) or {}).get(key)
    if not isinstance(entry, dict):
        return None
    try:
        x, y = int(entry.get("x", -1)), int(entry.get("y", -1))
    except (TypeError, ValueError):
        return None
    if x < 0 or y < 0:
        return None
    return entry


def main_spawn(level_data):
    """The level's main spawn dict (player.spawn, may be empty)."""
    sp = (level_data.get("player", {}) or {}).get("spawn", {}) or {}
    return sp if isinstance(sp, dict) else {}


def resolve_entry_spawn(level_data, key):
    """(x, y) landing for arrival `key`: the named entry when set,
    else the main spawn.  Single source for edge landings (key = arrival
    side) and exit target_entry landings."""
    entry = get_entry(level_data, key)
    if entry is not None:
        return (int(entry.get("x", 0)), int(entry.get("y", 0)))
    sp = main_spawn(level_data)
    return (int(sp.get("x", 1)), int(sp.get("y", 1)))


def resolve_exit_landing(exit, levels_by_id):
    """(x, y, via_entry) landing for a point exit.

    An exit naming a valid `target_entry` on a known destination lands on
    that entry (or the destination spawn when the entry is unset); the
    raw target_x/target_y are ignored.  Anything else (no key, unknown
    key, unknown destination) lands on the raw coordinates.
    """
    key = (exit.get("target_entry") or "").strip()
    target = exit.get("target_scene")
    if key and key in ENTRY_KEYS and target in levels_by_id:
        x, y = resolve_entry_spawn(levels_by_id[target], key)
        return (x, y, True)
    return (exit.get("target_x", -1), exit.get("target_y", -1), False)


def used_entry_keys(levels_by_id):
    """{level_name: set(keys)} entries consumed by links + target_entry
    exits.  Used entry sides come from the SOURCE link direction (A.east
    -> B consumes B.west); stair keys come from exit target_entry."""
    used = {name: set() for name in levels_by_id}
    for name, lvl in levels_by_id.items():
        try:
            targets = neighbor_targets(lvl)
        except SystemExit:
            continue
        for direction in NEIGHBOR_DIRS:
            target = targets.get(direction)
            if target and target in used:
                used[target].add(entry_side_for_link(direction))
        for e in lvl.get("exits", []) or []:
            key = (e.get("target_entry") or "").strip()
            target = e.get("target_scene")
            if key and key in ENTRY_KEYS and target in used:
                used[target].add(key)
    return used


def unused_entry_warnings(levels_by_id):
    """Warnings for defined entries nothing consumes (dead config)."""
    warnings = []
    used = used_entry_keys(levels_by_id)
    for name in sorted(levels_by_id):
        defined = [k for k in ENTRY_KEYS
                   if get_entry(levels_by_id[name], k) is not None]
        for key in defined:
            if key not in used.get(name, set()):
                warnings.append(
                    f"{name}: entry '{key}' is defined but nothing lands on it "
                    f"(no link arrives on that side and no exit names it) -- "
                    f"remove it or wire an arrival.")
    return warnings


def edge_cells(direction, w, h):
    """Border cells along `direction`, in scan order."""
    if direction == "north":
        return [(x, 0) for x in range(w)]
    if direction == "south":
        return [(x, h - 1) for x in range(w)]
    if direction == "west":
        return [(0, y) for y in range(h)]
    return [(w - 1, y) for y in range(h)]


def _grid(name, lvl, tilesets, cache):
    if cache is None:
        return derive_collision(lvl, tilesets.get(lvl["map"]["tileset"], {}))
    if name not in cache:
        cache[name] = derive_collision(lvl, tilesets.get(lvl["map"]["tileset"], {}))
    return cache[name]


def edge_link_report(name, lvl, levels_by_id, tilesets, grid_cache=None):
    """Per-direction diagnostics for one level's whole-edge links.

    Edge entry is the destination's arrival-side entry (falling back to
    its main spawn): every crossing on one edge lands on the same cell,
    so the check is per-link, not per-cell -- the source edge needs at
    least one walkable crossing and the used entry must be walkable.
    Returns a list of dicts {direction, target, entry_side, ok, errors,
    warnings}; every message is fully self-describing (level +
    coordinates).  This is the one implementation used by the validator,
    the compiler gate, and the editor's /api/neighbor-check."""
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
        side = entry_side_for_link(direction)
        entry = {"direction": direction, "target": target,
                 "entry_side": side, "ok": True,
                 "errors": [], "warnings": []}
        if target not in levels_by_id:
            entry["warnings"].append(
                f"{name}: edge '{direction}' -> '{target}': target not in this "
                f"compile set (cannot verify the {side} entry)")
            report.append(entry)
            continue
        tlvl = levels_by_id[target]
        ag = _grid(name, lvl, tilesets, grid_cache)
        bg = _grid(target, tlvl, tilesets, grid_cache)
        crossings = [c for c in edge_cells(direction, w, h)
                     if ag[c[1]][c[0]]]
        if not crossings:
            entry["warnings"].append(
                f"{name}: edge '{direction}' -> '{target}' never fires: "
                f"{name}'s {direction} edge has no walkable cell "
                f"(paint an opening or remove the link)")
        ex, ey = resolve_entry_spawn(tlvl, side)
        tw, th = tlvl["map"]["width"], tlvl["map"]["height"]
        if get_entry(tlvl, side) is None:
            entry["warnings"].append(
                f"{name}: edge '{direction}' -> '{target}' lands on the main "
                f"spawn ({ex},{ey}): '{target}' defines no '{side}' entry.")
        if not (0 <= ex < tw and 0 <= ey < th and bg[ey][ex]):
            entry["ok"] = False
            entry["errors"].append(
                f"{name}: edge '{direction}' -> '{target}' lands on the "
                f"'{side}' entry ({ex},{ey}), which is not walkable -- move "
                f"'{target}''s {side} entry (Spawn layer) onto walkable "
                f"ground.")
        try:
            back = neighbor_targets(tlvl).get(_OPPOSITE[direction])
        except SystemExit:
            back = None
        if back == name:
            rdir = _OPPOSITE[direction]
            rcross = [c for c in edge_cells(rdir, tw, th)
                      if bg[c[1]][c[0]]]
            if not rcross:
                entry["ok"] = False
                entry["errors"].append(
                    f"{name}: edge '{direction}' -> '{target}': '{target}' declares "
                    f"the return link ({rdir} -> {name}) but its {rdir} edge has no "
                    f"walkable cell -- the player cannot return.")
            else:
                sx, sy = resolve_entry_spawn(lvl, direction)
                if not (0 <= sx < w and 0 <= sy < h and ag[sy][sx]):
                    entry["ok"] = False
                    entry["errors"].append(
                        f"{name}: edge '{direction}' -> '{target}' return lands "
                        f"on '{name}''s '{direction}' entry ({sx},{sy}), which "
                        f"is not walkable -- move the entry (Spawn layer) "
                        f"onto walkable ground.")
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


def edge_geometry_issues(levels_by_id, tilesets):
    """(errors, warnings) for border-cell geometry the pairing gate does
    not cover.

    Walkable corner cells are rejected: the engine resolves N/S before
    E/W, so a walkable corner on two linked edges silently routes one
    way (e.g. (19,0) with north+east linked always exits north), while
    a walkable corner on unlinked edges is a dead alcove that reads as
    a gate.  Either way the map lies about where its holes are.
    No alignment check: edge entry is the neighbor spawn, so linked
    edges need no matched openings.
    Scoped by the caller to real levels (test fixtures have no links)."""
    errors, warnings = [], []
    cache = {}
    for name, lvl in levels_by_id.items():
        w, h = lvl["map"]["width"], lvl["map"]["height"]
        g = _grid(name, lvl, tilesets, cache)
        for cx, cy in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)):
            if g[cy][cx]:
                errors.append(
                    f"{name}: corner cell ({cx},{cy}) is walkable -- "
                    f"corners resolve N/S-before-E/W and dead-end alcoves "
                    f"read as gates; wall it or move the opening inward.")
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

    Returns a list of dicts {index, target, landing, ok, errors,
    warnings}; every message is fully self-describing (level +
    coordinates).  Mirrors the engine: a point exit fires before terrain
    walkability, so a solid gate is not blocked -- but it must look like
    a portal, and the destination must be walkable or the player spawns
    stuck.  An exit naming `target_entry` lands on that destination entry
    (or its spawn); otherwise it lands on target_x/target_y."""
    out = []
    tset = tilesets.get(lvl["map"]["tileset"], {})
    for i, e in enumerate(lvl.get("exits", [])):
        target = e.get("target_scene")
        tx, ty, via_entry = (resolve_exit_landing(e, levels_by_id)
                             if target in levels_by_id
                             else (e.get("target_x", -1),
                                   e.get("target_y", -1), False))
        entry = {"index": i, "target": target,
                 "landing": [tx, ty], "via_entry": via_entry,
                 "ok": True, "errors": [], "warnings": []}
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
            if not (0 <= tx < tlvl["map"]["width"]
                    and 0 <= ty < tlvl["map"]["height"] and g[ty][tx]):
                entry["ok"] = False
                where = (f"entry '{e.get('target_entry')}'"
                         if via_entry else f"({tx},{ty})")
                entry["errors"].append(
                    f"{name}: exit #{i} at ({e.get('x')},{e.get('y')}) -> "
                    f"'{target}' lands on {where}, which is not walkable "
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


# ── Two-way tunnels (linked exit pairs) ──────────────────────────────
# A tunnel is two point exits sharing a `tunnel` id, one in each of two
# levels, with mutual targets.  The ROM format is unchanged (two plain
# SceneExit rows); the id lives only in the JSON + tooling, which keeps
# the pair in sync.  Spawn-tracks-gate invariant: stepping into mouth A
# lands ON mouth B (A.target == B.gate and vice versa), so moving a gate
# has exactly one consistent partner update.

import re as _re

TUNNEL_ID_RE = _re.compile(r"^[a-z][a-z0-9_]*$")


def tunnel_id_valid(tunnel):
    """True when a tunnel id matches the project convention (same shape
    as scene ids: lowercase, starting with a letter)."""
    return bool(tunnel) and bool(TUNNEL_ID_RE.match(tunnel))


def collect_tunnels(levels_by_id):
    """{tunnel_id: [(level_name, exit_index, exit), ...]} for every exit
    carrying a truthy `tunnel` field, in deterministic (level, index)
    order."""
    out = {}
    for name in sorted(levels_by_id):
        lvl = levels_by_id[name]
        for i, e in enumerate(lvl.get("exits", [])):
            t = (e.get("tunnel") or "")
            if not t:
                continue
            out.setdefault(str(t), []).append((name, i, e))
    return out


def tunnel_report(levels_by_id):
    """Per-tunnel diagnostics for linked exit pairs.

    Returns a list of dicts {tunnel, ok, errors, warnings}; every
    message is fully self-describing (levels + coordinates).  Used by
    the compiler gate, the validator, and the editor's /api/exit-status.

    Contract mirror: tools/level_editor/vite.config.ts tunnelStatus()
    checks the same invariants (count == 2, different levels, mutual
    targets, no target_entry, spawn-tracks-gate).  Keep both in sync;
    the parity corpus
    in tools/level_compiler/tests/test_tunnel_parity.py guards drift."""
    out = []
    tunnels = collect_tunnels(levels_by_id)
    for tunnel in sorted(tunnels):
        ends = tunnels[tunnel]
        entry = {"tunnel": tunnel, "ok": True, "errors": [], "warnings": []}
        if len(ends) == 1:
            name, i, e = ends[0]
            entry["ok"] = False
            entry["errors"].append(
                f"tunnel '{tunnel}' has only one end ({name}: exit #{i} at "
                f"({e.get('x')},{e.get('y')}) -> '{e.get('target_scene')}'): "
                f"create the return exit with the same tunnel id, or remove "
                f"the tunnel field to keep it a one-way exit.")
            out.append(entry)
            continue
        if len(ends) > 2:
            where = ", ".join(f"{n} exit #{i}" for n, i, _ in ends)
            entry["ok"] = False
            entry["errors"].append(
                f"tunnel '{tunnel}' has {len(ends)} ends ({where}): a tunnel "
                f"is exactly two mouths — split the extras into their own "
                f"tunnel ids.")
            out.append(entry)
            continue
        (an, ai, a), (bn, bi, b) = ends
        if an == bn:
            entry["ok"] = False
            entry["errors"].append(
                f"tunnel '{tunnel}' has both ends in '{an}' (exits #{ai} and "
                f"#{bi}): the two mouths must live in different levels.")
        if a.get("target_scene") != bn or b.get("target_scene") != an:
            entry["ok"] = False
            entry["errors"].append(
                f"tunnel '{tunnel}' targets are not mutual: {an} exit #{ai} "
                f"-> '{a.get('target_scene')}' but {bn} exit #{bi} -> "
                f"'{b.get('target_scene')}' (want {an} -> {bn} and {bn} -> "
                f"{an}). Retarget the mouths so they point at each other.")
        agate = (a.get("x"), a.get("y"))
        bgate = (b.get("x"), b.get("y"))
        # Named landings cannot ride a tunnel: a tunnel mouth must land ON
        # the counterpart gate (spawn-tracks-gate), while target_entry
        # resolves to a named arrival point.  The raw coordinates such a
        # mouth carries are ignored at compile time, so the pairing check
        # fails it here instead of comparing dead numbers.
        a_named = (a.get("target_entry") or "").strip()
        b_named = (b.get("target_entry") or "").strip()
        if a_named or b_named:
            entry["ok"] = False
            entry["errors"].append(
                f"tunnel '{tunnel}' cannot use target_entry "
                f"({an} exit #{ai}: '{a_named or b_named}'): tunnel mouths "
                f"must land on the counterpart gate (spawn-tracks-gate) -- "
                f"remove target_entry or unlink the tunnel.")
            out.append(entry)
            continue
        if (a.get("target_x"), a.get("target_y")) != bgate or \
                (b.get("target_x"), b.get("target_y")) != agate:
            entry["ok"] = False
            entry["errors"].append(
                f"tunnel '{tunnel}' spawns do not track the counterpart gates "
                f"(spawn-tracks-gate): {an} exit #{ai} lands on "
                f"({a.get('target_x')},{a.get('target_y')}) but {bn}'s mouth "
                f"is at {bgate}; {bn} exit #{bi} lands on "
                f"({b.get('target_x')},{b.get('target_y')}) but {an}'s mouth "
                f"is at {agate}. Point each landing at the other mouth so a "
                f"moved gate has one consistent partner update.")
        out.append(entry)
    return out


def tunnel_issues(levels_by_id):
    """(errors, warnings) for every tunnel id in the set."""
    errors, warnings = [], []
    for entry in tunnel_report(levels_by_id):
        errors.extend(entry["errors"])
        warnings.extend(entry["warnings"])
    return errors, warnings
