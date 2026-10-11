#!/usr/bin/env python3
"""parity_check.py - WYSIWYG gate: the ROM must show what the level editor
shows for the same levels/*.json.

Two halves (see docs/level-editor.md Phase 17):

1. Terrain VRAM parity (live boot per map via the SameBoy harness): expand
   each level's terrain to a per-cell tile id, map it to the expected VRAM
   tile index through the tileset manifest (vram_block cross-refs, the same
   registry the editor renders from), load the map in the debug ROM, and
   compare every terrain cell against the WRAM tilemap mirror.  Actor-
   occupied cells are SKIPPED (actor overlay rules live with actor
   scenarios, not here).  Any terrain mismatch fails loudly.
2. Animation frame-set parity (static cross-check, no boot): every
   animation_frames id in every level must resolve in its tileset manifest
   AND vram_block (a typo silently degrades to a fallback today), and the
   ROM sprite-tile Makefile rules must load the sheet cells those frames
   name.  Timing/phase may differ; the frame SET and order must not.

Usage:
    python3 tools/parity_check.py            # all levels
    python3 tools/parity_check.py field town # subset by level id
"""

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))

from emulator import EmulatorSession  # noqa: E402

LEVELS_DIR = REPO_ROOT / "levels"
TILESET_DIR = REPO_ROOT / "tools" / "level_editor" / "tilesets"

# Tileset id -> CGB tileset kind is irrelevant here; what matters is the
# VRAM base (all world tiles live at 128+) and the per-tileset default
# floor art for unlisted cells (mirrors rpg_lookup_tile_id glyph fallbacks
# for '.', the only generic glyph terrain blocks can produce: every block
# tile in every level is walkable floor).
# Generic wall-glyph fallback per tileset (mirrors rpg_lookup_tile_id '#';
# the engine perimeter rule paints map borders TILE_WALL before exits).
VRAM_BASE = 128


def first_plain_tile(tileset_id, tileset):
    for t in tileset.get("tiles", []):
        if "plain" in t.get("id", ""):
            return "%s.%s" % (tileset_id, t["id"])
    return None


def level_default_index(level, tilesets):
    """Resolve the level's default_walkable to a VRAM block index."""
    tileset_id = level["map"]["tileset"]
    tileset = tilesets[tileset_id]
    dw = level.get("default_walkable") or ""
    if not dw:
        dw = first_plain_tile(tileset_id, tileset) or ""
    short = dw.split(".")[-1]
    for e in tileset.get("vram_block", {}).get("tiles", []):
        if e.get("tile") == short:
            return VRAM_BASE + e.get("index")
    raise ValueError("%s: default ground '%s' has no vram index" % (level["id"], dw))
WALL_FALLBACK_INDEX = {
    "forest": 1,
    "village": 1,
    "desolate_landscape": 1,
    "castle": 10,
}

# ROM sprite-tile rules (Makefile gfx) that must cover the JSON animation
# frames: rule name -> set of (x, y) sheet cells it loads.  Checked against
# the vram_block coordinates the frame ids resolve to.  Keep in sync with
# the Makefile --tile-coords lists.
SPRITE_RULE_CELLS = {
    "forest_kobold_sprite_tile": {(3, 2), (4, 2)},
    "forest_bat_sprite_tile": {(9, 2), (10, 2)},
    "forest_hero_sprite_tile": {(1, 2), (2, 2)},
    "hero_desolate_sprite_tile": {(1, 2), (2, 2)},
    "kobold_sprite_tile": {(3, 2), (4, 2)},
    "desolate_bat_sprite_tile": {(9, 2), (10, 2)},
    "castle_bat_sprite_tile": {(5, 2), (6, 2)},
}

# Boss overworld art (castle_top_*_boss) is world-sheet art rendered as a
# 2x2 background block, not an OAM sprite: it rides along with
# ui_load_tileset() unconditionally, so no sprite-tile rule need cover it.
BOSS_WORLD_ART_SUBSTRINGS = ("boss",)

# Combat-tileset battle art (battle_sprites.png) is covered elsewhere, not
# here: tools/compose_battle_sprites.py is byte-deterministic by
# construction, and tools/scenarios/tests/battle_slime_sprite.json asserts
# the loaded art tiles in the VRAM mirror at battle entry.

failures = []


def fail(msg):
    print("FAIL:", msg)
    failures.append(msg)


def load_tilesets():
    tilesets = {}
    for p in TILESET_DIR.glob("*.json"):
        data = json.loads(p.read_text())
        tilesets[data["id"]] = data
    return tilesets


def manifest_index(tileset, tile_id):
    """Manifest tile id -> VRAM block index via vram_block cross-refs."""
    for e in tileset.get("vram_block", {}).get("tiles", []):
        if e.get("tile") == tile_id:
            return e.get("index")
    return None


def expand_terrain(level):
    """Level JSON -> {(x, y): qualified tile id} for explicitly painted cells."""
    cells = {}
    w = level["map"]["width"]
    h = level["map"]["height"]
    terrain = level.get("layers", {}).get("terrain", [])
    if terrain and isinstance(terrain[0], list):
        for y, row in enumerate(terrain[:h]):
            for x, cell in enumerate(row[:w]):
                cells[(x, y)] = cell
    else:
        for b in terrain:
            for cy in range(b["y"], min(h, b["y"] + b["height"])):
                for cx in range(b["x"], min(w, b["x"] + b["width"])):
                    cells[(cx, cy)] = b["tile"]
    return cells


def expected_grid(level, tilesets):
    """Full-map expected VRAM tile indices.  Returns (grid, skipped).

    Mirrors the ROM loader: unpainted interior is default ground;
    unpainted perimeter is wall, except unpainted point-exit gates and
    whole linked edges (neighbors), which compile to open-ground rows.
    Gates never stamp their own art: exit cells render their painted
    art (or default ground), so they are checked, not skipped."""
    tileset_id = level["map"]["tileset"]
    tileset = tilesets[tileset_id]
    by_id = {t["id"]: t for t in tileset.get("tiles", [])}
    w = level["map"]["width"]
    h = level["map"]["height"]
    painted = expand_terrain(level)
    try:
        default_idx = level_default_index(level, tilesets)
    except ValueError as e:
        fail(str(e))
        return {}, set()
    neighbors = level.get("neighbors", {}) or {}
    linked = {d for d in ("north", "south", "east", "west")
              if (neighbors.get(d) or "").strip()}
    gates = {(e.get("x", -1), e.get("y", -1)) for e in level.get("exits", [])}
    grid = {}
    for y in range(h):
        for x in range(w):
            cell = painted.get((x, y))
            if cell is not None:
                short = cell.split(".")[-1]
            elif x == 0 or y == 0 or x == w - 1 or y == h - 1:
                # Engine perimeter rule (borders default to walls), with
                # the two ROM exceptions, both compiled to default-ground
                # rows: unpainted gates and linked edges.
                if (x, y) in gates:
                    grid[(x, y)] = default_idx
                    continue
                on_linked = ((y == 0 and "north" in linked)
                             or (y == h - 1 and "south" in linked)
                             or (x == 0 and "west" in linked)
                             or (x == w - 1 and "east" in linked))
                grid[(x, y)] = (default_idx if on_linked
                                else VRAM_BASE + WALL_FALLBACK_INDEX[tileset_id])
                continue
            else:
                grid[(x, y)] = default_idx
                continue
            info = by_id.get(short)
            if info is None:
                fail("%s: unknown tile '%s' at (%d,%d)" % (level["id"], cell, x, y))
                continue
            idx = manifest_index(tileset, short)
            if idx is None:
                fail("%s: tile '%s' has no vram_block index" % (level["id"], short))
                continue
            grid[(x, y)] = VRAM_BASE + idx
    # Actor-occupied cells are rendered by actor overlay rules, not terrain.
    skipped = set()
    for obj in level.get("objects", []):
        p = obj.get("position", {})
        skipped.add((p.get("x", -1), p.get("y", -1)))
    return grid, skipped


def check_anim_frames(levels_by_id, tilesets):
    """Every animation_frames id must resolve in manifest + vram_block,
    and the ROM sprite rules must load those sheet cells."""
    for sid, level in levels_by_id.items():
        tileset_id = level["map"]["tileset"]
        if tileset_id == "combat":
            continue
        tileset = tilesets.get(tileset_id, {})
        by_id = {t["id"]: t for t in tileset.get("tiles", [])}
        vram = {}
        for e in tileset.get("vram_block", {}).get("tiles", []):
            vram[e.get("tile")] = (e.get("x"), e.get("y"))
        refs = []

        def collect(frames, where):
            for f in frames or []:
                if "." in f:
                    ts_name, short = f.split(".", 1)
                else:
                    ts_name, short = tileset_id, f
                if ts_name == "actors":
                    actors_ts = tilesets.get("actors", {})
                    actors_by_id = {t["id"]: t for t in actors_ts.get("tiles", [])}
                    if short not in actors_by_id:
                        fail("%s %s: actor frame '%s' not in manifest 'actors'"
                             % (sid, where, f))
                    continue
                refs.append((short, where))
                if short not in by_id:
                    fail("%s %s: animation frame '%s' not in manifest '%s'"
                         % (sid, where, f, tileset_id))
                elif short not in vram:
                    fail("%s %s: animation frame '%s' has no vram_block coords"
                         % (sid, where, f))

        spawn = level.get("player", {}).get("spawn", {})
        collect(spawn.get("animation_frames"), "player.spawn")
        for obj in level.get("objects", []):
            collect(obj.get("animation_frames"), "object %s" % obj.get("id"))
        # ROM-side coverage: every referenced sheet cell must be loaded by
        # some sprite-tile Makefile rule (boss world-art excepted above).
        loaded = set()
        for cells in SPRITE_RULE_CELLS.values():
            loaded |= cells
        for short, where in refs:
            if any(s in short for s in BOSS_WORLD_ART_SUBSTRINGS):
                continue
            if short in vram and vram[short] not in loaded:
                fail("%s %s: frame '%s' at sheet %s is not loaded by any "
                     "Makefile sprite-tile rule" % (sid, where, short, vram[short]))
    print("anim frame-set check: %s" % ("OK" if not failures else "FAILED"))


def check_map(sess, level, tilesets):
    sid = level["id"]
    grid, skipped = expected_grid(level, tilesets)
    spawn = level.get("player", {}).get("spawn", {})
    sess.load_scenario({
        "initial_state": {
            "scene": sid.upper(),
            "player": {"x": spawn.get("x", 2), "y": spawn.get("y", 2),
                       "facing": spawn.get("facing", "DOWN")},
            "seed": 42,
        }
    })
    sess.step(30)  # let the full map redraw land before reading the mirror
    mirror = sess.get_tilemap_mirror()
    bad = 0
    for (x, y), want in sorted(grid.items()):
        if (x, y) in skipped:
            continue
        got = mirror[(y & 31) * 32 + (x & 31)]
        if got != want:
            if bad < 8:
                fail("%s cell (%d,%d): ROM tile %d, editor art maps to %d"
                     % (sid, x, y, got, want))
            bad += 1
    if bad == 0:
        print("PASS %s: %d terrain cells match editor art" % (sid, len(grid) - len(skipped)))
    else:
        print("FAIL %s: %d/%d terrain cells mismatch" % (sid, bad, len(grid) - len(skipped)))


def main(argv):
    only = set(argv[1:])
    tilesets = load_tilesets()
    levels_by_id = {}
    for p in sorted(LEVELS_DIR.glob("*.json")):
        if p.name == "registry.json":
            continue  # scene id registry, not a level
        data = json.loads(p.read_text())
        levels_by_id[data["id"]] = data
    if only:
        levels_by_id = {k: v for k, v in levels_by_id.items() if k in only}
        if not levels_by_id:
            print("no such levels: %s" % sorted(only))
            return 2

    check_anim_frames(levels_by_id, tilesets)
    if failures:
        return 1

    sess = EmulatorSession()
    try:
        sess.connect()
        for sid, level in levels_by_id.items():
            check_map(sess, level, tilesets)
    finally:
        sess.disconnect()
    if failures:
        print("%d parity failure(s)" % len(failures))
        return 1
    print("parity OK: ROM shows what the editor shows")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
