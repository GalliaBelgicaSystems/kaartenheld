#!/usr/bin/env python3
"""
decompile.py - ROM tables back to level JSON (the vice-versa half).

Reads src/game/scenes_content.c + src/game/actors_content.c (C tables, never
the .gb binary) and merges the compilable sections back into levels/*.json,
preserving editorial sections (name, regions, spawn animation_frames,
descriptions, terrain block comments, decoration objects without an
entity_id).

Lossy-by-design normalizations (all fixpoint-stable, all warned about):
- terrain blocks are emitted VERBATIM (one JSON block per C block); the
  compiler's greedy merge is never re-run here, so recompile is byte-stable;
- collapsed TILE_FLOOR/TILE_WALL map to per-tileset canonical art ids;
- actor type/string-id are synthesized when the C row cannot name them;
- exit direction is preserved when gate+target match, else nearest-edge rule;
- music/tileset spellings are preserved when they map to the same enum.

Anything unrecognised (unknown TileType value, TILE_EXIT inside terrain,
duplicate entity_id per table, unknown facing/flags) is a loud error, never
a silent invention.

Usage:
    python3 tools/level_compiler/decompile.py            # merge in place
    python3 tools/level_compiler/decompile.py --check    # report only
    python3 tools/level_compiler/decompile.py --roundtrip  # fixpoint gate
"""

import sys
import os
import json
import re
import shutil
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent
sys.path.insert(0, str(SCRIPT_DIR))

from validate import validate_level, load_tilesets
import compile as compiler
from compile import (
    TILESET_KIND_MAP, REPO_ROOT as CREPO, resolve_sprite_kind,
    load_enemy_types, scene_maps,
)

SCENES_C = CREPO / "src" / "game" / "scenes_content.c"
ACTORS_C = CREPO / "src" / "game" / "actors_content.c"
WORLD_H = CREPO / "src" / "world" / "world.h"

# Collapsed tiles roundtrip through per-tileset canonical art ids (all carry
# committed images, so the editor renders decompiled output faithfully).
CANONICAL_FLOOR = {
    "forest": "forest_plain_floor_1",
    "desolate_landscape": "desolate_landscape_plain_floor_1",
    "castle": "castle_plain_floor",
}
CANONICAL_WALL = {
    "forest": "forest_top_wall_1",
    "desolate_landscape": "desolate_landscape_top_wall_1",
    "castle": "castle_top_wall",
}

# Canonical sprite refs per tileset, keyed by SPRITE_KIND suffix. Every id
# below is asserted present in its manifest at startup.
SPRITE_FRAMES = {
    "desolate_landscape": {
        "KOBOLD": ["actors.actors_kobold_frame_1",
                   "actors.actors_kobold_frame_2"],
        "BAT": ["actors.actors_bats_frame_1",
                "actors.actors_bats_frame_2"],
        "CHEST": ["desolate_landscape.desolate_landscape_treasure_chest_desolate"],
    },
    "forest": {
        "KOBOLD": ["actors.actors_kobold_frame_1",
                   "actors.actors_kobold_frame_2"],
        "BAT": ["actors.actors_bats_frame_1",
                "actors.actors_bats_frame_2"],
        "CHEST": ["forest.forest_treasure_chest_forest"],
    },
    "castle": {
        "KOBOLD": ["castle.castle_enemy_kobold_frame_1",
                   "castle.castle_enemy_kobold_frame_2"],
        "BAT": ["actors.actors_bats_frame_1",
                "actors.actors_bats_frame_2"],
        "BOSS": ["actors.actors_boss_top_left_corner",
                 "actors.actors_boss_top_right_corner",
                 "actors.actors_boss_bottom_left_corner",
                 "actors.actors_boss_bottom_right_corner"],
    },
}

FACING_REVERSE = {
    "DIRECTION_UP": "UP",
    "DIRECTION_DOWN": "DOWN",
    "DIRECTION_LEFT": "LEFT",
    "DIRECTION_RIGHT": "RIGHT",
}

class DecompileError(Exception):
    pass


# ── C text parsing ────────────────────────────────────────────────────

def extract_array_body(text, decl_pattern):
    """Return the brace-matched body of `... name[...] = { ... };`."""
    m = re.search(decl_pattern, text)
    if not m:
        raise DecompileError(f"array not found: {decl_pattern}")
    # patterns end at '='; the opening brace follows
    i = text.index("{", m.start())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
    raise DecompileError(f"unbalanced braces: {decl_pattern}")


def split_row_groups(body):
    """Split an array body into top-level `{ ... }` row strings."""
    rows = []
    depth = 0
    cur = None
    instr = None
    for ch in body:
        if instr:
            if ch == instr:
                instr = None
            if cur is not None:
                cur.append(ch)
            continue
        if ch in ("'", '"'):
            instr = ch
            if cur is not None:
                cur.append(ch)
            continue
        if ch == "{":
            if depth == 0:
                cur = []
            else:
                cur.append(ch)
            depth += 1
            continue
        if ch == "}":
            depth -= 1
            if depth == 0:
                rows.append("".join(cur))
                cur = None
            else:
                cur.append(ch)
            continue
        if cur is not None:
            cur.append(ch)
    return rows


def split_fields(row):
    """Split a row into top-level comma-separated fields."""
    fields = []
    depth = 0
    instr = None
    cur = []
    for ch in row:
        if instr:
            cur.append(ch)
            if ch == instr:
                instr = None
            continue
        if ch in ("'", '"'):
            instr = ch
            cur.append(ch)
            continue
        if ch in ("(", "["):
            depth += 1
            cur.append(ch)
            continue
        if ch in (")", "]"):
            depth -= 1
            cur.append(ch)
            continue
        if ch == "," and depth == 0:
            fields.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    tail = "".join(cur).strip()
    if tail:
        fields.append(tail)
    return fields


def parse_int(text):
    text = text.strip()
    try:
        return int(text, 0)
    except ValueError:
        raise DecompileError(f"expected integer, got {text!r}")


def parse_char(text):
    m = re.fullmatch(r"'(.)'", text.strip())
    if not m:
        raise DecompileError(f"expected char literal, got {text!r}")
    return m.group(1)


def parse_string(text):
    m = re.fullmatch(r'"(.*)"', text.strip(), re.DOTALL)
    if not m:
        raise DecompileError(f"expected string literal, got {text!r}")
    return m.group(1)


def parse_flag_names(text):
    text = text.strip()
    if text == "0":
        return []
    names = []
    for part in text.split("|"):
        part = part.strip()
        prefix = "ACTOR_FLAG_"
        if not part.startswith(prefix):
            raise DecompileError(f"unknown flag expression {text!r}")
        short = part[len(prefix):]
        if short not in ("HOSTILE", "BLOCKING", "INTERACTABLE"):
            raise DecompileError(f"unknown flag {part!r}")
        names.append(short)
    return names


# ── TileType numbers (src/world/world.h) ──────────────────────────────

def load_tiletype_numbers():
    mapping = {}
    for m in re.finditer(r"^\s*(TILE_[A-Z0-9_]+)\s*=\s*(\d+)", WORLD_H.read_text(), re.M):
        mapping[int(m.group(2))] = m.group(1)
    if 0 not in mapping or 1 not in mapping or 2 not in mapping:
        raise DecompileError("TileType enum missing TILE_FLOOR/WALL/EXIT")
    return mapping


def tile_value_to_id(value, tileset_id, const_by_value, gb_to_tileid, home_tileset):
    """Reverse map_base_tile_const: ROM grid value -> 'tileset.tile_id'."""
    if value == 0:
        try:
            return f"{tileset_id}.{CANONICAL_FLOOR[tileset_id]}"
        except KeyError:
            raise DecompileError(f"no canonical floor for tileset '{tileset_id}'")
    if value == 1:
        try:
            return f"{tileset_id}.{CANONICAL_WALL[tileset_id]}"
        except KeyError:
            raise DecompileError(f"no canonical wall for tileset '{tileset_id}'")
    name = const_by_value.get(value)
    if name is None:
        raise DecompileError(f"terrain holds unknown TileType value {value}")
    if name == "TILE_EXIT":
        raise DecompileError("terrain block holds TILE_EXIT (gates belong in exits)")
    key = (home_tileset, name)
    if key in gb_to_tileid:
        return f"{home_tileset}.{gb_to_tileid[key]}"
    fallbacks = sorted((ts, tid) for (ts, nm), tid in gb_to_tileid.items() if nm == name)
    if not fallbacks:
        raise DecompileError(f"no manifest tile for {name}")
    ts, tid = fallbacks[0]
    return f"{ts}.{tid}"


# ── Actor rows ────────────────────────────────────────────────────────

def parse_actor_row(row):
    f = split_fields(row)
    if len(f) not in (19, 20, 21):
        raise DecompileError(f"actor row has {len(f)} fields (want 19-21): {row[:60]!r}")
    actor_id = parse_int(f[0])
    if actor_id < 0 or actor_id > 65535:
        raise DecompileError(f"actor_id out of range: {f[0]!r}")
    facing = f[4].strip()
    if facing not in FACING_REVERSE:
        raise DecompileError(f"unknown facing {facing!r}")
    flags = parse_flag_names(f[5])
    visual = parse_char(f[6])
    return {
        "actor_id": actor_id,
        "entity_id": f[1].strip(),
        "x": parse_int(f[2]),
        "y": parse_int(f[3]),
        "facing": FACING_REVERSE[facing],
        "flags": flags,
        "visual": visual,
        "display_name": parse_string(f[7]),
        "interaction": f[8].strip(),
        "shop_id": parse_int(f[9]),
        "dialogue_id": f[10].strip(),
        "battle_id": f[11].strip(),
        "ai_type": f[12].strip(),
        "hp": parse_int(f[13]),
        "max_hp": parse_int(f[14]),
        "gold_reward": parse_int(f[15]),
        "reward_currency": f[16].strip(),
        "spawn_variable": f[17].strip(),
        "spawn_value": parse_int(f[18]),
        "sprite_kind": f[19].strip() if len(f) >= 20 else "SPRITE_KIND_ASCII",
        "ow_type": parse_int(f[20]) if len(f) >= 21 else 0xFF,
    }


def infer_type_and_props(row, warnings, ctx):
    """Recover the JSON type (+shop/save/dialogue slots) from a C row."""
    inter = row["interaction"]
    ent = row["entity_id"]
    props = {}
    if inter == "INTERACTION_COMBAT":
        otype = "enemy"
    elif inter == "INTERACTION_SHOP":
        otype = "npc"
        props["shop"] = row["shop_id"]
    elif inter == "INTERACTION_SAVE":
        otype = "npc"
        props["save"] = row["shop_id"]
    elif inter == "INTERACTION_DIALOGUE":
        if ent == "ENTITY_ID_SIGNPOST":
            otype = "signpost"
        elif ent == "ENTITY_ID_AMULET":
            otype = "item"
        else:
            otype = "npc"
        if row["dialogue_id"] != "DIALOGUE_ID_NONE":
            props["dialogue"] = row["dialogue_id"]
    else:
        raise DecompileError(f"{ctx}: unknown interaction {inter!r}")
    return otype, props


def canonical_sprite_refs(kind, tileset_id, ow_type=0xFF):
    """SPRITE_KIND_* -> (overworld_sprite, [frames]) in this tileset.
    SPRITE_KIND_ENEMY resolves through the enemy-type row to the shared
    enemies tileset (world-independent); every other kind uses the
    per-tileset table as before."""
    if kind == "SPRITE_KIND_ASCII":
        return None, []
    if kind == "SPRITE_KIND_ENEMY":
        try:
            enemy_map = load_enemy_types()
            enemy_ids = sorted(enemy_map.keys())
            et = enemy_map[enemy_ids[ow_type]]
            cells = (et.get("overworld") or {}).get("cells", [])
            refs = ["enemies.%s" % c for c in cells]
            return (refs[0], refs) if refs else (None, [])
        except Exception:
            return None, []
    short = kind[len("SPRITE_KIND_"):]
    table = SPRITE_FRAMES.get(tileset_id, {})
    if short not in table:
        raise DecompileError(f"no {short} art for tileset '{tileset_id}'")
    frames = list(table[short])
    return frames[0], frames


# ── Scene tables ──────────────────────────────────────────────────────

def parse_scenes(scenes_text, name_to_value):
    exits_body = extract_array_body(scenes_text, r"g_all_exits\[\]\s*=")
    exits = []
    for row in split_row_groups(exits_body):
        f = split_fields(row)
        if len(f) != 6:
            raise DecompileError(f"exit row has {len(f)} fields: {row[:60]!r}")
        exits.append({
            "gate_x": parse_int(f[0]),
            "gate_y": parse_int(f[1]),
            "spawn_x": parse_int(f[2]),
            "spawn_y": parse_int(f[3]),
            "target": f[4].strip(),
            "tile_char": parse_char(f[5]),
        })
    scenes_body = extract_array_body(scenes_text, r"g_scenes\[\]\s*=")
    scenes = []
    for row in split_row_groups(scenes_body):
        f = split_fields(row)
        if len(f) != 12:
            raise DecompileError(f"scene row has {len(f)} fields: {row[:60]!r}")
        m = re.fullmatch(r"&g_all_exits\[(\d+)\]", f[4].strip())
        facing = f[10].strip()
        if facing not in FACING_REVERSE:
            raise DecompileError(f"unknown spawn facing {facing!r}")
        try:
            default_tile = parse_int(f[11].strip())
        except DecompileError:
            name = f[11].strip()
            if name not in name_to_value:
                raise DecompileError(f"unknown TileType {name!r}")
            default_tile = name_to_value[name]
        scenes.append({
            "map": f[0].strip(),
            "music": f[1].strip(),
            "width": parse_int(f[2]),
            "height": parse_int(f[3]),
            "exit_start": int(m.group(1)) if m else None,
            "exit_count": parse_int(f[5]),
            "tileset_kind": f[6].strip(),
            "terrain_sym": f[7].strip(),
            "spawn_x": parse_int(f[8]),
            "spawn_y": parse_int(f[9]),
            "spawn_facing": FACING_REVERSE[facing],
            "default_tile": default_tile,
        })
    terrain = {}
    for m in re.finditer(r"static const SceneTerrainBlock (s_\w+)\[\]", scenes_text):
        sym = m.group(1)
        body = extract_array_body(scenes_text, r"static const SceneTerrainBlock " + sym + r"\[\]\s*=")
        blocks = []
        for row in split_row_groups(body):
            f = split_fields(row)
            if len(f) != 5:
                raise DecompileError(f"terrain row has {len(f)} fields: {row[:60]!r}")
            x, y, w, h = (parse_int(f[0]), parse_int(f[1]),
                            parse_int(f[2]), parse_int(f[3]))
            tile = f[4].strip()
            try:
                tile = parse_int(tile)
            except DecompileError:
                if tile not in name_to_value:
                    raise DecompileError(f"unknown TileType {tile!r}")
                tile = name_to_value[tile]
            if w == 0:
                break
            blocks.append({"x": x, "y": y, "w": w, "h": h, "tile": tile})
        terrain[sym] = blocks
    return exits, scenes, terrain


def parse_actor_tables(actors_text):
    tables = {}
    for m in re.finditer(r"static const WorldActorDefinition (g_\w+)\[\]", actors_text):
        sym = m.group(1)
        body = extract_array_body(
            actors_text, r"static const WorldActorDefinition " + sym + r"\[\]\s*=")
        tables[sym] = [parse_actor_row(r) for r in split_row_groups(body)]
    return tables


# ── Merge ─────────────────────────────────────────────────────────────

def nearest_edge_direction(x, y, width, height):
    dists = [("WEST", x), ("EAST", width - 1 - x),
             ("NORTH", y), ("SOUTH", height - 1 - y)]
    return min(dists, key=lambda t: t[1])[0]


def decompile_levels(levels_dir, write):
    levels_dir = Path(levels_dir)
    tilesets = load_tilesets()
    const_by_value = load_tiletype_numbers()
    gb_to_tileid = {}
    for ts_id, ts in tilesets.items():
        for t in ts.get("tiles", []):
            gb_to_tileid.setdefault((ts_id, t["gb_constant"]), t["id"])
    # startup assertions: canonical ids + sprite refs must exist
    for ts_id, tile_id in list(CANONICAL_FLOOR.items()) + list(CANONICAL_WALL.items()):
        ids = {t["id"] for t in tilesets.get(ts_id, {}).get("tiles", [])}
        if tile_id not in ids:
            raise DecompileError(f"canonical tile '{ts_id}.{tile_id}' missing from manifest")
    for ts_id, kinds in SPRITE_FRAMES.items():
        ids = {t["id"] for t in tilesets.get(ts_id, {}).get("tiles", [])}
        for kind, refs in kinds.items():
            for ref in refs:
                if ref.split(".")[-1] not in ids:
                    raise DecompileError(f"sprite ref '{ref}' missing from manifest '{ts_id}'")

    scenes_text = SCENES_C.read_text()
    actors_text = ACTORS_C.read_text()
    name_to_value = {n: v for v, n in const_by_value.items()}
    exits, scenes, terrain = parse_scenes(scenes_text, name_to_value)
    actor_tables = parse_actor_tables(actors_text)

    map_enum, scene_enum = scene_maps()
    map_to_sid = {v: k for k, v in map_enum.items()}
    scene_to_sid = {v: k for k, v in scene_enum.items()}
    kind_to_tileset = {}
    for ts, kind in TILESET_KIND_MAP.items():
        kind_to_tileset.setdefault(kind, ts)

    changed = []
    warnings = []
    for sc in scenes:
        sid = map_to_sid.get(sc["map"])
        if sid is None:
            raise DecompileError(f"unknown map enum {sc['map']!r}")
        path = levels_dir / f"{sid}.json"
        if not path.exists():
            warnings.append(f"{sid}: no JSON file; skeleton emission not implemented, skipping")
            continue
        level = json.loads(path.read_text())
        tileset_id = kind_to_tileset.get(sc["tileset_kind"])
        if tileset_id is None:
            raise DecompileError(f"{sid}: unknown tileset kind {sc['tileset_kind']!r}")

        # -- terrain: verbatim, one JSON block per C block. When the
        # existing JSON already has the same rect with art that compiles
        # to the same TileType value (e.g. treetop art collapsing to
        # TILE_WALL), the author's art is kept: only contradictions with
        # the C tables are rewritten.
        from compile import map_base_tile_const, resolve_tiles
        tile_dict = resolve_tiles(level, tilesets.get(tileset_id, {}))
        old_rects = {}
        old_terrain = level.get("layers", {}).get("terrain", [])
        if isinstance(old_terrain, list) and old_terrain and isinstance(old_terrain[0], dict):
            for b in old_terrain:
                old_rects[(b.get("x"), b.get("y"), b.get("width"), b.get("height"))] = \
                    b.get("tile", "")
        terrain_blocks = []
        if sc["terrain_sym"] != "0":
            rows = terrain.get(sc["terrain_sym"])
            if rows is None:
                raise DecompileError(f"{sid}: terrain symbol {sc['terrain_sym']!r} not found")
            for b in rows:
                key = (b["x"], b["y"], b["w"], b["h"])
                tile_id = None
                if key in old_rects:
                    tinfo = tile_dict.get(old_rects[key].split(".")[-1])
                    if tinfo:
                        gb = map_base_tile_const(tinfo.get("gb_constant", "TILE_WALL"), tinfo)
                        if name_to_value.get(gb) == b["tile"]:
                            tile_id = old_rects[key]
                if tile_id is None:
                    tile_id = tile_value_to_id(b["tile"], tileset_id, const_by_value,
                                               gb_to_tileid, tileset_id)
                block_out = {
                    "x": b["x"], "y": b["y"],
                    "width": b["w"], "height": b["h"],
                    "tile": tile_id,
                }
                # Editorial comments live only in the JSON (the C tables
                # cannot hold them): keep the author's comment when the
                # rect matches, so decompile never erases documentation.
                if key in old_rects:
                    for b_old in old_terrain:
                        if (b_old.get("x"), b_old.get("y"),
                                b_old.get("width"), b_old.get("height")) == key \
                                and b_old.get("comment"):
                            block_out["comment"] = b_old["comment"]
                            break
                terrain_blocks.append(block_out)
        # Preserve design-only floor patches: old blocks resolving to
        # TILE_FLOOR compile to nothing (default background), so they have
        # no C counterpart, but dropping them would erase documented
        # intent. Re-emitting them is fixpoint-stable (they skip again).
        emitted_rects = {(b["x"], b["y"], b["width"], b["height"]) for b in terrain_blocks}
        if isinstance(old_terrain, list) and old_terrain and isinstance(old_terrain[0], dict):
            for b in old_terrain:
                key = (b.get("x"), b.get("y"), b.get("width"), b.get("height"))
                if key in emitted_rects:
                    continue
                tinfo = tile_dict.get(str(b.get("tile", "")).split(".")[-1])
                if tinfo and name_to_value.get(
                        map_base_tile_const(tinfo.get("gb_constant", "TILE_WALL"),
                                            tinfo), -1) == 0:
                    terrain_blocks.append({
                        "x": b["x"], "y": b["y"],
                        "width": b["width"], "height": b["height"],
                        "tile": b["tile"],
                    })
                else:
                    warnings.append(f"{sid}: terrain block at {key} has no C row; dropped")
        level.setdefault("layers", {})["terrain"] = terrain_blocks

        # -- scenes metadata: music is the single C enum name everywhere;
        # no aliasing of legacy spellings (compile.py passes through too).
        music = level.get("map", {}).get("music", "")
        if music != sc["music"]:
            level["map"]["music"] = sc["music"]
        ts_now = level.get("map", {}).get("tileset", "")
        if TILESET_KIND_MAP.get(ts_now, None) != sc["tileset_kind"]:
            level["map"]["tileset"] = tileset_id
        level["map"]["width"] = sc["width"]
        level["map"]["height"] = sc["height"]
        level["map"]["map_id"] = sc["map"]

        # -- player spawn: compiled into SceneDefinition, merged back so
        # the JSON stays the single source of truth.  Editorial spawn keys
        # the ROM cannot hold (animation_frames, ...) are preserved verbatim.
        spawn = level.setdefault("player", {}).setdefault("spawn", {})
        spawn["x"] = sc["spawn_x"]
        spawn["y"] = sc["spawn_y"]
        spawn["facing"] = sc["spawn_facing"]

        # -- default ground: compiled into SceneDefinition.default_tile.
        # Keep the author's spelling when it compiles to the same TileType
        # value (like music/tileset spellings); otherwise write the
        # canonical tileset.tile_id.
        if sc["default_tile"] is not None:
            keep = False
            dw = level.get("default_walkable", "")
            if dw:
                tinfo = tile_dict.get(dw.split(".")[-1])
                if tinfo:
                    gb = map_base_tile_const(tinfo.get("gb_constant", "TILE_WALL"), tinfo)
                    if name_to_value.get(gb) == sc["default_tile"]:
                        keep = True
            if not keep:
                tile_id = tile_value_to_id(sc["default_tile"], tileset_id,
                                           const_by_value, gb_to_tileid,
                                           tileset_id)
                level["default_walkable"] = tile_id

        # -- exits
        old_exits = { (e.get("x"), e.get("y"), e.get("target_scene")): e
                      for e in level.get("exits", []) }
        new_exits = []
        for e in exits[sc["exit_start"]:sc["exit_start"] + sc["exit_count"]] \
                if sc["exit_start"] is not None else []:
            target = scene_to_sid.get(e["target"])
            if target is None:
                raise DecompileError(f"{sid}: unknown scene enum {e['target']!r}")
            old = old_exits.get((e["gate_x"], e["gate_y"], target))
            if old is not None and old.get("tile_char", ">") == e["tile_char"]:
                direction = old.get("direction")
            else:
                direction = nearest_edge_direction(e["gate_x"], e["gate_y"],
                                                   sc["width"], sc["height"])
                if old is None:
                    warnings.append(f"{sid}: new exit at ({e['gate_x']},{e['gate_y']})->"
                                    f"{target}; direction '{direction}' synthesized")
            new_exits.append({
                "x": e["gate_x"], "y": e["gate_y"],
                "target_scene": target,
                "target_x": e["spawn_x"], "target_y": e["spawn_y"],
                "direction": direction,
                "tile_char": e["tile_char"],
            })
        level["exits"] = new_exits

        # -- actors
        sym = f"g_{sid}_actors"
        rows = actor_tables.get(sym, [])
        seen_ent = set()
        for r in rows:
            if r["entity_id"] in seen_ent:
                raise DecompileError(f"{sid}: duplicate entity_id {r['entity_id']!r}")
            seen_ent.add(r["entity_id"])
        by_ent = {}
        for obj in level.get("objects", []):
            ent = (obj.get("properties") or {}).get("entity_id")
            if ent and ent not in by_ent:
                by_ent[ent] = obj
        new_objects = []
        used = set()
        for r in rows:
            obj = by_ent.get(r["entity_id"])
            if obj is None:
                obj = {"id": synthesize_id(sid, r["entity_id"], used),
                       "position": {}}
                warnings.append(f"{sid}: new C row {r['entity_id']} synthesized "
                                f"as '{obj['id']}'")
            used.add(obj["id"])
            obj["position"] = {"x": r["x"], "y": r["y"]}
            otype, extra = infer_type_and_props(r, warnings, f"{sid}/{obj['id']}")
            obj["type"] = otype
            props = {"entity_id": r["entity_id"]}
            if otype == "enemy" or r["display_name"] not in ("", r["entity_id"]):
                props["display_name"] = r["display_name"]
            props.update(extra)
            props["actor_id"] = r["actor_id"]
            props["facing"] = r["facing"]
            props["flags"] = list(r["flags"])
            props["visual"] = r["visual"]
            props["hp"] = r["hp"]
            props["max_hp"] = r["max_hp"]
            props["gold_reward"] = r["gold_reward"]
            if r["reward_currency"] != "0":
                props["reward_currency"] = r["reward_currency"]
            if r["battle_id"] != "BATTLE_NONE" or otype == "enemy":
                props["battle"] = r["battle_id"]
            props["ai"] = r["ai_type"]
            if r["spawn_variable"] != "0":
                props["quest_var"] = r["spawn_variable"]
                props["quest_val"] = r["spawn_value"]
            # sprite refs: keep the author's strings when they resolve equal
            keep = False
            if obj.get("overworld_sprite") or obj.get("animation_frames"):
                try:
                    probe = {"overworld_sprite": obj.get("overworld_sprite"),
                             "animation_frames": obj.get("animation_frames") or [],
                             "properties": {"entity_id": r["entity_id"]}}
                    if resolve_sprite_kind(probe) == r["sprite_kind"]:
                        keep = True
                    elif r["sprite_kind"] == "SPRITE_KIND_ENEMY":
                        # Shared refs are the fixpoint: keep them, normalize
                        # anything else to enemies.* on this pass.
                        names = " ".join((obj.get("animation_frames") or []) + [obj.get("overworld_sprite") or ""])
                        if names.startswith("enemies.") or " enemies." in names:
                            keep = True
                except Exception:
                    keep = False
            if not keep:
                ov, frames = canonical_sprite_refs(r["sprite_kind"], tileset_id, r.get("ow_type", 0xFF))
                if ov is None:
                    obj.pop("overworld_sprite", None)
                    obj.pop("animation_frames", None)
                else:
                    obj["overworld_sprite"] = ov
                    obj["animation_frames"] = frames
                if ov is not None or r["sprite_kind"] != "SPRITE_KIND_ASCII":
                    warnings.append(f"{sid}/{obj['id']}: sprite refs normalized")
            obj["properties"] = props
            new_objects.append(obj)
        # preserve decoration objects (no entity_id) and stale rows verbatim
        for obj in level.get("objects", []):
            ent = (obj.get("properties") or {}).get("entity_id")
            if not ent:
                new_objects.append(obj)
            elif ent not in seen_ent:
                warnings.append(f"{sid}/{obj.get('id')}: no C row; kept verbatim")
                new_objects.append(obj)
        level["objects"] = new_objects

        old_text = path.read_text()
        new_text = dump_canonical(level) + "\n"
        if json.loads(old_text) != json.loads(new_text):
            changed.append(sid)
            if write:
                path.write_text(new_text)
    return changed, warnings


def dump_canonical(level):
    """Repo-style JSON: indent=2, but leaf objects/arrays that fit on one
    line stay on one line (matches the hand-written files, so decompile
    diffs show content changes, not style churn). Deterministic."""
    text = json.dumps(level, indent=2)

    def repl(m):
        inner = " ".join(m.group(2).split())
        one = m.group(1) + " " + inner + " " + m.group(3)
        return one if len(one) <= 100 else m.group(0)

    # only innermost shapes match (no braces/brackets inside); one pass
    return re.sub(r"(\{|\[)\s*([^{}\[\]]+?)\s*(\}|\])", repl, text)


def synthesize_id(sid, entity_id, used):
    base = f"{sid}_{entity_id[len('ENTITY_ID_'):].lower()}" \
        if entity_id.startswith("ENTITY_ID_") else f"{sid}_actor"
    cand, n = base, 2
    while cand in used:
        cand = f"{base}_{n}"
        n += 1
    return cand


def cmd_roundtrip():
    """Fixpoint gate on copies: compile -> decompile -> compile == same C,
    for both scenes and actor tables."""
    from compile import emit_c_code, emit_actors_code, load_level
    tmp = Path(tempfile.mkdtemp(prefix="lvl_rt_"))
    try:
        (tmp / "levels").mkdir()
        for p in (REPO_ROOT / "levels").glob("*.json"):
            if p.name == "schema":
                continue
            if p.name == compiler.REGISTRY_FILENAME:
                continue  # registry is tooling state, not a level
            shutil.copy(p, tmp / "levels" / p.name)
        changed, warnings = decompile_levels(tmp / "levels", write=True)
        for w in warnings:
            print(f"roundtrip warning: {w}")
        tilesets = load_tilesets()
        levels_by_id = {}
        for p in sorted((tmp / "levels").glob("*.json")):
            data = load_level(p)
            ok, errors, warns, _ = validate_level(data, tilesets)
            if not ok:
                print(f"roundtrip: decompiled {p.name} INVALID: {errors}")
                return 1
            levels_by_id[data["id"]] = data
        c_new = emit_c_code(levels_by_id, tilesets)
        c_old = SCENES_C.read_text()
        if c_new.strip() != c_old.strip():
            print("roundtrip FAIL: recompile differs from committed scenes_content.c")
            import difflib
            for line in list(difflib.unified_diff(
                    c_old.splitlines(), c_new.splitlines(), lineterm=""))[:40]:
                print(line)
            return 1
        a_new = emit_actors_code(levels_by_id)
        a_old = ACTORS_C.read_text()
        if not rows_equal(a_old, a_new):
            print("roundtrip FAIL: regenerated actor tables differ from "
                  "committed actors_content.c (see diff below)")
            import difflib
            for line in list(difflib.unified_diff(
                    a_old.splitlines(), a_new.splitlines(), lineterm=""))[:60]:
                print(line)
            return 1
        print(f"roundtrip OK ({len(levels_by_id)} levels,"
              f" {len(changed)} normalized: {sorted(changed)})")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def rows_equal(old_text, new_text):
    """Semantic actor-table equality: same tables in the same order with
    identical 20-field rows (formatting-insensitive). This is the
    migration-proof gate for fully generated actor content."""
    old_tables = parse_actor_tables(old_text)
    new_tables = parse_actor_tables(new_text)
    if list(old_tables) != list(new_tables):
        print(f"table order/names differ: {list(old_tables)} vs {list(new_tables)}")
        return False
    fields = ("actor_id", "entity_id", "x", "y", "facing", "flags", "visual",
              "display_name", "interaction", "shop_id", "dialogue_id",
              "battle_id", "ai_type", "hp", "max_hp", "gold_reward",
              "reward_currency", "spawn_variable", "spawn_value", "sprite_kind")
    ok = True
    for sym in old_tables:
        old_rows, new_rows = old_tables[sym], new_tables[sym]
        if len(old_rows) != len(new_rows):
            print(f"{sym}: row count {len(old_rows)} vs {len(new_rows)}")
            ok = False
            continue
        for i, (a, b) in enumerate(zip(old_rows, new_rows)):
            # 19-field legacy rows parse with sprite_kind defaulted to ASCII
            for key in fields:
                if a.get(key) != b.get(key):
                    print(f"{sym}[{i}].{key}: {a.get(key)!r} vs {b.get(key)!r}")
                    ok = False
    return ok


def main(argv):
    check = "--check" in argv
    if "--roundtrip" in argv:
        sys.exit(cmd_roundtrip())
    levels_dir = REPO_ROOT / "levels"
    changed, warnings = decompile_levels(levels_dir, write=not check)
    for w in warnings:
        print(f"warning: {w}")
    if changed:
        print(("would update" if check else "updated") + f": {sorted(changed)}")
        sys.exit(1 if check else 0)
    print("decompile: no changes")
    return 0


if __name__ == "__main__":
    main(sys.argv[1:])
