#!/usr/bin/env python3
"""
validate.py - Level validator for Game Boy RPG Level Editor

Validates level JSON files against schemas and semantic game rules:
- Schema validation
- Tileset and semantic tile validity
- Map bounds and dimensions (<= 40x24)
- Player spawn validity and collision
- Exit placement and target scene validity
- Object IDs uniqueness and placement
- Region bounds

Usage:
    python3 tools/level_compiler/validate.py levels/forest.json
    python3 tools/level_compiler/validate.py levels/*.json
"""

import sys
import os
import json
import glob
from pathlib import Path

from scene_registry import (
    TEST_SCENE_ORDER, load_registry, is_level_file,
)
from collision import edge_link_report, neighbor_pairing_issues, point_exit_issues

MAX_WORLD_WIDTH = 40
MAX_WORLD_HEIGHT = 24


def known_scene_names(registry=None):
    """Every sid an exit may legally target: registry scenes + TEST fixtures."""
    registry = registry or load_registry()
    return set(registry["scenes"]) | set(TEST_SCENE_ORDER)


def dialogue_id_names():
    """DIALOGUE_ID_* names from screens/dialogue/*.json (same assignment
    dialogue_compile.py uses).  Imported lazily: screen_compiler is a
    sibling package, and validate.py must stay importable without it."""
    tools_dir = Path(__file__).resolve().parent.parent
    if str(tools_dir / "screen_compiler") not in sys.path:
        sys.path.insert(0, str(tools_dir / "screen_compiler"))
    from dialogue_ids import dialogue_files, load_dialogue_json
    names = {}
    for path in dialogue_files():
        try:
            data = load_dialogue_json(path)
        except (OSError, ValueError) as exc:
            raise SystemExit(f"ERROR: cannot read {path}: {exc}")
        name = "DIALOGUE_ID_" + data["_id"].upper()
        if name in names:
            raise SystemExit(
                f"ERROR: duplicate dialogue id '{data['_id']}' "
                f"({path} and {names[name]}).")
        names[name] = path.name
    return names


def validate_dialogue_refs(levels_dir=None):
    """Cross-references between dialogue content and its users.

    Errors (loud, with the fix): actor `dialogue` props and
    EVENT_ACTION_DIALOGUE args in src/game/events.c that name no dialogue
    JSON.  Warnings: dialogues no actor or event references (write-before-
    wire is normal authoring; the warning keeps dead content visible).
    Flags/events/scenarios stay LLM-driven — this checks references only.
    """
    import re
    errors, warnings = [], []
    known = dialogue_id_names()
    repo_root = Path(__file__).resolve().parent.parent.parent
    levels_dir = Path(levels_dir) if levels_dir else (repo_root / "levels")

    referenced = set()
    for path in sorted(levels_dir.glob("*.json")):
        if not is_level_file(path):
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            errors.append(f"Cannot read {path}: {exc}")
            continue
        for obj in data.get("objects", []):
            props = (obj.get("properties", {}) or {})
            dlg = props.get("dialogue", "")
            if not dlg:
                continue
            if not props.get("entity_id"):
                # Decoration object: the compiler emits no actor row, so
                # this text can never fire.  Warn, don't error (e.g. the
                # south campfire's flavor line predates entity wiring).
                warnings.append(
                    f"{path.name}/{obj.get('id')}: dialogue text on an "
                    f"entity-less object is unreachable (no actor row).")
                continue
            referenced.add(dlg)
            if dlg not in known:
                if not dlg.startswith("DIALOGUE_ID_"):
                    errors.append(
                        f"{path.name}/{obj.get('id')}: dialogue prop is raw "
                        f"text, not a dialogue id — the compiler emits it "
                        f"verbatim into C. Use a DIALOGUE_ID_* id (pick one "
                        f"in the editor's dialogue dropdown).")
                else:
                    errors.append(
                        f"{path.name}/{obj.get('id')}: unknown dialogue "
                        f"'{dlg}' — pick one in the editor's dialogue "
                        f"dropdown or add screens/dialogue/<id>.json.")

    events_c = repo_root / "src" / "game" / "events_content.c"
    try:
        events_text = events_c.read_text(encoding="utf-8")
    except OSError as exc:
        errors.append(f"Cannot read {events_c}: {exc}")
        events_text = ""
    for m in re.finditer(r"\b(DIALOGUE_ID_[A-Z0-9_]+)\b", events_text):
        name = m.group(1)
        referenced.add(name)
        if name not in known:
            errors.append(
                f"src/game/events.c references unknown dialogue '{name}' "
                f"— add screens/dialogue/<id>.json (LLM-driven).")

    for name in sorted(set(known) - referenced):
        warnings.append(
            f"Dialogue '{name}' is referenced by no actor or event.")
    return errors, warnings


def validate_shop_refs(levels_dir=None):
    """Actor `shop` props must name a real shop (screens/shops/<id>.json).

    A dangling id compiles fine but the ROM then stocks nothing at that
    NPC (game_shop_for_id returns NULL) — a silent content bug.  Errors
    only; unreferenced shops are allowed (write-before-wire)."""
    errors = []
    repo_root = Path(__file__).resolve().parent.parent.parent
    shops_dir = repo_root / "screens" / "shops"
    known = set()
    for p in shops_dir.glob("*.json"):
        try:
            known.add(int(p.stem))
        except ValueError:
            errors.append(f"{p.name}: shop filename must be a numeric id")
    levels_dir = Path(levels_dir) if levels_dir else (repo_root / "levels")
    for path in sorted(levels_dir.glob("*.json")):
        if not is_level_file(path):
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            errors.append(f"Cannot read {path}: {exc}")
            continue
        for obj in data.get("objects", []):
            props = (obj.get("properties", {}) or {})
            if "shop" not in props:
                continue
            try:
                sid = int(props["shop"])
            except (TypeError, ValueError):
                errors.append(
                    f"{path.name}/{obj.get('id')}: shop prop "
                    f"'{props['shop']}' is not a numeric shop id")
                continue
            if sid not in known:
                errors.append(
                    f"{path.name}/{obj.get('id')}: unknown shop id {sid} "
                    f"— add screens/shops/{sid}.json in the editor's Shop view")
    return errors


def validate_registry_consistency(levels_dir=None):
    """The registry and the levels/ directory must agree: every level file
    (minus registry.json) needs a registry entry, and every live entry
    needs its file.  Returns a list of error strings (empty = consistent).
    This is the loud failure that replaces the old lcc error for
    unregistered levels: fix = open the level in the editor and save it
    (the editor assigns the next scene id on first save)."""
    from pathlib import Path as _Path
    errors = []
    try:
        registry = load_registry()
    except SystemExit as exc:
        return [str(exc)]
    levels_dir = _Path(levels_dir) if levels_dir else (
        _Path(__file__).resolve().parent.parent.parent / "levels")
    files = {p.stem for p in levels_dir.glob("*.json") if is_level_file(p)}
    try:
        disk_ids = set()
        for stem in files:
            try:
                data = json.loads((levels_dir / f"{stem}.json").read_text(
                    encoding="utf-8"))
            except (OSError, ValueError) as exc:
                errors.append(f"Cannot read levels/{stem}.json: {exc}")
                continue
            if data.get("id", stem) != stem:
                errors.append(
                    f"levels/{stem}.json has id '{data.get('id')}' but the "
                    f"registry keys scenes by filename — rename the file or "
                    f"fix the id so they match.")
            else:
                disk_ids.add(stem)
    except OSError as exc:
        return [f"Cannot list {levels_dir}: {exc}"]
    retired = registry.get("retired", {})
    for sid in sorted(disk_ids - set(registry["scenes"])):
        if sid in retired:
            errors.append(
                f"Level '{sid}' has a RETIRED scene id (tombstoned, never "
                f"reused), but levels/{sid}.json still exists. Delete the "
                f"stale file — the editor's Map Info 'Clean retired orphans' "
                f"button removes every such file — then recompile.")
        else:
            errors.append(
                f"Level '{sid}' has no registry entry (no scene id assigned). "
                f"Open it in the editor and save it — the editor assigns the "
                f"next scene id on first save — then recompile.")
    for sid in sorted(set(registry["scenes"]) - disk_ids):
        errors.append(
            f"Registry lists '{sid}' but levels/{sid}.json is missing. "
            f"Restore the file from git, or retire the id properly.")
    return errors


def registry_warnings(levels_dir=None):
    """Non-fatal registry hygiene warnings (reserved for future checks; a
    retired id whose file still exists is now a hard error in
    validate_registry_consistency)."""
    return []


def load_tilesets(tilesets_dir=None):
    """Load all available tilesets from standard directories."""
    tilesets = {}
    search_dirs = []
    if tilesets_dir:
        search_dirs.append(Path(tilesets_dir))
    
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent
    search_dirs.extend([
        repo_root / "tools" / "level_editor" / "tilesets",
        repo_root / "levels" / "tilesets",
        repo_root / "levels" / "schema"
    ])

    for sdir in search_dirs:
        if sdir.is_dir():
            for p in sdir.glob("*.json"):
                if "schema" in p.name:
                    continue
                try:
                    with open(p, "r", encoding="utf-8") as f:
                        data = json.load(f)
                        if "id" in data and "tiles" in data:
                            tilesets[data["id"]] = data
                except Exception:
                    pass
    return tilesets


_KNOWN_ENTITY_IDS = None


def known_entity_ids():
    """ENTITY_ID_* names, derived from the entity-type registries
    (screens/enemy_types/*.json + screens/entity_types/*.json) — the same
    source entity_compile.py uses to generate the C header.  Adding a type
    in the editor makes it valid here with no C edit."""
    global _KNOWN_ENTITY_IDS
    if _KNOWN_ENTITY_IDS is None:
        tools_dir = Path(__file__).resolve().parent.parent
        if str(tools_dir / "screen_compiler") not in sys.path:
            sys.path.insert(0, str(tools_dir / "screen_compiler"))
        from entity_ids import entity_type_ids
        _KNOWN_ENTITY_IDS = {"ENTITY_ID_" + t.upper() for t in entity_type_ids()}
        _KNOWN_ENTITY_IDS.add("ENTITY_ID_PLAYER")
    return _KNOWN_ENTITY_IDS


def validate_level(level_data, tilesets=None, all_level_ids=None):
    """Validate a loaded level dict. Returns (is_valid, list_of_errors, list_of_warnings, checks_passed)."""
    errors = []
    warnings = []
    passed = []

    if tilesets is None:
        tilesets = load_tilesets()

    # 1. Schema / Structure Check
    schema_ok = True
    required_top = ["id", "name", "map", "layers", "player"]
    for field in required_top:
        if field not in level_data:
            errors.append(f"Missing required field: '{field}'")
            schema_ok = False

    map_info = level_data.get("map", {})
    if not isinstance(map_info, dict):
        errors.append("'map' must be an object")
        schema_ok = False
    else:
        for f in ["width", "height", "tileset"]:
            if f not in map_info:
                errors.append(f"Missing map property: '{f}'")
                schema_ok = False

    if schema_ok:
        passed.append("Schema valid")

    level_id = level_data.get("id", "unknown")
    width = map_info.get("width", 0)
    height = map_info.get("height", 0)

    # Bounds check
    if width <= 0 or width > MAX_WORLD_WIDTH:
        errors.append(f"Map width {width} is out of bounds (1..{MAX_WORLD_WIDTH})")
    if height <= 0 or height > MAX_WORLD_HEIGHT:
        errors.append(f"Map height {height} is out of bounds (1..{MAX_WORLD_HEIGHT})")

    # 2. Tileset & Tiles Check
    tileset_id = map_info.get("tileset", "")
    tileset = tilesets.get(tileset_id)
    tiles_ok = True

    if not tileset:
        errors.append(f"Unknown tileset: '{tileset_id}' (available: {list(tilesets.keys())})")
        tiles_ok = False
    else:
        tile_map = {t["id"]: t for t in tileset.get("tiles", [])}
        layers = level_data.get("layers", {})
        terrain = layers.get("terrain", [])

        # Check 2D grid or blocks
        if isinstance(terrain, list):
            if len(terrain) > 0 and isinstance(terrain[0], list):
                # 2D Grid
                if len(terrain) != height:
                    errors.append(f"Terrain grid row count ({len(terrain)}) does not match height ({height})")
                    tiles_ok = False
                for r_idx, row in enumerate(terrain):
                    if len(row) != width:
                        errors.append(f"Terrain grid row {r_idx} column count ({len(row)}) does not match width ({width})")
                        tiles_ok = False
                    for c_idx, cell in enumerate(row):
                        # cell can be "forest.tree" or "tree"
                        tile_name = cell.split(".")[-1] if "." in cell else cell
                        if tile_name not in tile_map:
                            errors.append(f"Unknown tile '{cell}' at ({c_idx}, {r_idx})")
                            tiles_ok = False
            elif len(terrain) > 0 and isinstance(terrain[0], dict):
                # Block list
                for b_idx, block in enumerate(terrain):
                    bx = block.get("x", 0)
                    by = block.get("y", 0)
                    bw = block.get("width", 0)
                    bh = block.get("height", 0)
                    bt = block.get("tile", "")
                    t_name = bt.split(".")[-1] if "." in bt else bt
                    if t_name not in tile_map:
                        errors.append(f"Unknown tile '{bt}' in terrain block {b_idx}")
                        tiles_ok = False
                    if bx < 0 or bx + bw > width or by < 0 or by + bh > height:
                        errors.append(f"Terrain block {b_idx} at ({bx},{by},{bw},{bh}) exceeds map bounds ({width}x{height})")
                        tiles_ok = False

    if tiles_ok and tileset:
        passed.append("Tiles valid")

    # 2b. Default ground check (single source of truth for unpainted cells)
    default_walkable = level_data.get("default_walkable", "")
    if not default_walkable:
        # Implicit rule: first tile with 'plain' in manifest order.
        for t in (tileset or {}).get("tiles", []):
            if "plain" in t.get("id", ""):
                default_walkable = "%s.%s" % (tileset_id, t["id"])
                break
    if not default_walkable:
        errors.append("No default_walkable field and no 'plain' tile in tileset '%s'" % tileset_id)
    else:
        short = default_walkable.split(".")[-1]
        tile_map = {t["id"]: t for t in (tileset or {}).get("tiles", [])}
        if short not in tile_map:
            errors.append("default_walkable tile '%s' is not in tileset '%s'" % (default_walkable, tileset_id))
        elif not tile_map[short].get("walkable", True):
            errors.append("default_walkable tile '%s' is not walkable" % default_walkable)
        else:
            passed.append("Default ground valid")

    # 3. Collision & Player Spawn Check
    collision_ok = True
    spawn_info = level_data.get("player", {}).get("spawn", {})
    sp_x = spawn_info.get("x", -1)
    sp_y = spawn_info.get("y", -1)

    # The spawn is compiled into SceneDefinition (game_new_game reads the
    # FIELD row), so the facing spelling must map to a Direction enum.
    spawn_facing = str(spawn_info.get("facing", "DOWN")).upper()
    if spawn_facing not in ("UP", "DOWN", "LEFT", "RIGHT",
                            "NORTH", "SOUTH", "WEST", "EAST"):
        errors.append(f"Player spawn facing '{spawn_info.get('facing')}' is unknown "
                      f"(want UP/DOWN/LEFT/RIGHT or a compass alias)")
        collision_ok = False

    if sp_x < 0 or sp_x >= width or sp_y < 0 or sp_y >= height:
        errors.append(f"Player spawn ({sp_x}, {sp_y}) is outside map bounds ({width}x{height})")
        collision_ok = False
    else:
        # Check if spawn is on map boundary wall
        if sp_x == 0 or sp_x == width - 1 or sp_y == 0 or sp_y == height - 1:
            warnings.append(f"Player spawn ({sp_x}, {sp_y}) is on perimeter boundary wall")

        # Check if spawn tile is walkable
        if tileset:
            tile_map = {t["id"]: t for t in tileset.get("tiles", [])}
            layers = level_data.get("layers", {})
            terrain = layers.get("terrain", [])
            spawn_blocked = False
            if isinstance(terrain, list) and len(terrain) > 0 and isinstance(terrain[0], list):
                if sp_y < len(terrain) and sp_x < len(terrain[sp_y]):
                    cell = terrain[sp_y][sp_x]
                    t_name = cell.split(".")[-1] if "." in cell else cell
                    if t_name in tile_map and not tile_map[t_name].get("walkable", True):
                        spawn_blocked = True
            elif isinstance(terrain, list) and len(terrain) > 0 and isinstance(terrain[0], dict):
                for block in terrain:
                    bx = block.get("x", 0)
                    by = block.get("y", 0)
                    bw = block.get("width", 0)
                    bh = block.get("height", 0)
                    bt = block.get("tile", "")
                    if bx <= sp_x < bx + bw and by <= sp_y < by + bh:
                        t_name = bt.split(".")[-1] if "." in bt else bt
                        if t_name in tile_map and not tile_map[t_name].get("walkable", True):
                            spawn_blocked = True
            if spawn_blocked:
                errors.append(f"Player spawn at ({sp_x}, {sp_y}) is blocked by non-walkable tile")
                collision_ok = False

    if collision_ok:
        passed.append("Collision valid")
        passed.append("Player spawn valid")

    # 4. Exits Check
    exits_ok = True
    exits = level_data.get("exits", [])
    for e_idx, exit_obj in enumerate(exits):
        ex = exit_obj.get("x", -1)
        ey = exit_obj.get("y", -1)
        target = exit_obj.get("target_scene", "")
        tx = exit_obj.get("target_x", -1)
        ty = exit_obj.get("target_y", -1)

        if ex < 0 or ex >= width or ey < 0 or ey >= height:
            errors.append(f"Exit {e_idx} gate position ({ex}, {ey}) is outside map bounds ({width}x{height})")
            exits_ok = False

        if target not in known_scene_names() and (all_level_ids is None or target not in all_level_ids):
            warnings.append(f"Exit {e_idx} target '{target}' is not in known scenes list")

    if exits_ok:
        passed.append("Exits valid")

    # 4b. Edge neighbors (whole-edge links). Unknown targets are errors
    # (the compiler would abort on them); one-sided links only warn
    # (walk in works, walk back is a wall until the return is set).
    neighbors_ok = True
    neighbors = level_data.get("neighbors", {}) or {}
    if neighbors and not isinstance(neighbors, dict):
        errors.append("'neighbors' must be an object with north/south/east/west scene names")
        neighbors_ok = False
    else:
        for direction in ("north", "south", "east", "west"):
            target = (neighbors.get(direction) or "").strip() if isinstance(neighbors, dict) else ""
            if not target:
                continue
            if target not in known_scene_names() and (all_level_ids is None or target not in all_level_ids):
                errors.append(
                    f"Neighbor '{direction}' target '{target}' is not a known scene "
                    f"(point it at a registered scene)")
                neighbors_ok = False
    if neighbors_ok:
        passed.append("Neighbors valid")

    # Point-exit visibility + landing walkability are cross-file (the
    # landing cell lives in the target scene); see collision.py
    # point_exit_issues(), reported by main() and enforced by compile.py.

    # 5. Objects Check (full-fidelity actor slots: JSON must be able to
    # roundtrip the C WorldActorDefinition rows -- see decompile.py)
    objects_ok = True
    objects = level_data.get("objects", [])
    seen_obj_ids = set()
    for obj in objects:
        oid = obj.get("id")
        if not oid:
            errors.append("Object missing 'id'")
            objects_ok = False
        elif oid in seen_obj_ids:
            errors.append(f"Duplicate object ID: '{oid}'")
            objects_ok = False
        else:
            seen_obj_ids.add(oid)

        pos = obj.get("position", {})
        ox = pos.get("x", -1)
        oy = pos.get("y", -1)
        if ox < 0 or ox >= width or oy < 0 or oy >= height:
            errors.append(f"Object '{oid}' position ({ox}, {oy}) is outside map bounds")
            objects_ok = False

        otype = obj.get("type", "")
        props = obj.get("properties", {})
        if not isinstance(props, dict):
            errors.append(f"Object '{oid}' properties must be an object")
            objects_ok = False
            continue
        if not props.get("entity_id"):
            # Decorative objects (e.g. the south_field campfire) have no
            # engine actor row: decompile.py preserves them verbatim and
            # compile.py ignores them. Not an error, but they cannot
            # carry actor slots.
            warnings.append(f"Object '{oid}' has no entity_id: kept verbatim, ignored by compile")
            for key in ("actor_id", "hp", "max_hp", "battle", "ai"):
                if key in props:
                    errors.append(f"Object '{oid}' has no entity_id but carries actor slot '{key}'")
                    objects_ok = False
            continue
        # Entity ids must exist in the game layer (src/game/game_ids.h
        # defines; single source of truth, parsed so the list never
        # drifts).  Unknown ids would raise at C-compile time anyway —
        # surface them here with the fix instead.
        known_entities = known_entity_ids()
        ent_id = props.get("entity_id")
        if ent_id not in known_entities:
            errors.append(
                f"Object '{oid}': unknown entity_id '{ent_id}' — add "
                f"#define {ent_id} (ENTITY_ID_FIRST_GAME + N) to src/game/game_ids.h")
            objects_ok = False
        aid = props.get("actor_id", 0)
        if not isinstance(aid, int) or aid < 0 or aid > 65535:
            errors.append(f"Object '{oid}' has invalid actor_id '{aid}' (0..65535)")
            objects_ok = False
        facing = props.get("facing", "DOWN")
        if facing not in ("UP", "DOWN", "LEFT", "RIGHT"):
            errors.append(f"Object '{oid}' has invalid facing '{facing}'")
            objects_ok = False
        for flag in props.get("flags", []):
            if flag not in ("HOSTILE", "BLOCKING", "INTERACTABLE"):
                errors.append(f"Object '{oid}' has unknown flag '{flag}'")
                objects_ok = False
        visual = props.get("visual", "?")
        if not isinstance(visual, str) or len(visual) != 1:
            errors.append(f"Object '{oid}' visual must be a single character")
            objects_ok = False
        if otype == "enemy":
            for key in ("hp", "max_hp", "ai"):
                if key not in props:
                    errors.append(f"Enemy object '{oid}' is missing properties.{key}")
                    objects_ok = False
            if "battle" not in props:
                errors.append(f"Enemy object '{oid}' is missing properties.battle")
                objects_ok = False
        for key in ("hp", "max_hp", "gold_reward"):
            if key in props and (not isinstance(props[key], int) or props[key] < 0):
                errors.append(f"Object '{oid}' has invalid {key} '{props[key]}'")
                objects_ok = False

    # Engine actor-slot caps: actor_load_banked() spawns hostile rows into
    # World.actors[MAX_WORLD_ACTORS=4] and friendly rows into
    # g_static_actors (MAX_STATIC_ACTORS, parsed from src/world/actor.h —
    # single source of truth).  Rows beyond the cap are SILENTLY DROPPED
    # at runtime -- catch the overflow at validation time instead.
    def _engine_cap(name):
        import re
        src = (Path(__file__).resolve().parent.parent.parent
               / "src" / "world" / "actor.h").read_text()
        m = re.search(r"#define %s\s+(\d+)" % name, src)
        return int(m.group(1)) if m else None

    hostile_cap = _engine_cap("MAX_WORLD_ACTORS")
    static_cap = _engine_cap("MAX_STATIC_ACTORS")
    hostile_rows = [o.get("id") for o in objects
                    if (o.get("properties") or {}).get("entity_id")
                    and "HOSTILE" in ((o.get("properties") or {}).get("flags") or [])]
    static_rows = [o.get("id") for o in objects
                   if (o.get("properties") or {}).get("entity_id")
                   and "HOSTILE" not in ((o.get("properties") or {}).get("flags") or [])]
    if hostile_cap and len(hostile_rows) > hostile_cap:
        errors.append(
            f"Level has {len(hostile_rows)} hostile actors but the engine spawns at most "
            f"MAX_WORLD_ACTORS={hostile_cap}; extra would be silently dropped: {hostile_rows[hostile_cap:]}")
        objects_ok = False
    if static_cap and len(static_rows) > static_cap:
        errors.append(
            f"Level has {len(static_rows)} friendly actors but the engine loads at most "
            f"{static_cap} static rows (MAX_STATIC_ACTORS, src/world/actor.h); extra would be "
            f"silently dropped: {static_rows[static_cap:]}")
        objects_ok = False

    if objects_ok:
        passed.append("Objects valid")

    is_valid = (len(errors) == 0)
    return is_valid, errors, warnings, passed


def neighbor_check_cli():
    """`validate.py --neighbor-check [override.json]`: JSON edge-pairing
    report for the editor. Reads all real levels from disk; an optional
    JSON document ({from_id, data}) from the override file (or stdin)
    replaces one level (unsaved editor state). Prints {success, links}
    for the overridden level, else {success, errors, warnings}."""
    tilesets = load_tilesets()
    repo_root = Path(__file__).resolve().parent.parent.parent
    levels_dir = repo_root / "levels"
    levels_by_id = {}
    for p in sorted(levels_dir.glob("*.json")):
        if not is_level_file(p):
            continue
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        if isinstance(data, dict) and data.get("id"):
            levels_by_id[data["id"]] = data
    override = None
    if len(sys.argv) >= 3 and sys.argv[2]:
        try:
            override = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            override = None
    if override is None:
        try:
            if not sys.stdin.isatty():
                raw = sys.stdin.read().strip()
                if raw:
                    override = json.loads(raw)
        except (OSError, ValueError):
            override = None
    from_id = None
    if isinstance(override, dict):
        fid = override.get("from_id")
        data = override.get("data")
        if isinstance(fid, str) and isinstance(data, dict):
            from_id = fid
            levels_by_id[fid] = data
    try:
        if from_id and from_id in levels_by_id:
            links = edge_link_report(from_id, levels_by_id[from_id],
                                     levels_by_id, tilesets)
            print(json.dumps({"success": True, "links": links}))
        else:
            errors, warnings = neighbor_pairing_issues(levels_by_id, tilesets)
            print(json.dumps({"success": True, "errors": errors,
                              "warnings": warnings}))
    except SystemExit as exc:
        print(json.dumps({"success": False, "error": str(exc)}))


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--neighbor-check":
        neighbor_check_cli()
        return
    if len(sys.argv) >= 2 and sys.argv[1] == "--dialogue-refs":
        errors, warnings = validate_dialogue_refs()
        for w in warnings:
            print(f"WARNING: {w}")
        for e in errors:
            print(f"ERROR: {e}")
        if errors:
            sys.exit(1)
        print(f"dialogue refs OK ({len(warnings)} warning(s))")
        return
    if len(sys.argv) >= 2 and sys.argv[1] == "--shop-refs":
        errors = validate_shop_refs()
        for e in errors:
            print(f"ERROR: {e}")
        if errors:
            sys.exit(1)
        print("shop refs OK")
        return
    if len(sys.argv) < 2:
        print("Usage: validate.py <level1.json> [level2.json ...]")
        print("       validate.py --dialogue-refs")
        print("       validate.py --shop-refs")
        sys.exit(1)

    tilesets = load_tilesets()
    file_paths = []
    for arg in sys.argv[1:]:
        matches = glob.glob(arg)
        if matches:
            file_paths.extend(matches)
        else:
            file_paths.append(arg)

    # The scene id registry is tooling state, not a level — never validate
    # it as one (the shell glob levels/*.json matches it).
    file_paths = [p for p in file_paths
                  if os.path.basename(p) != "registry.json"]

    # Collect all level IDs first
    all_level_ids = set()
    loaded_levels = []
    for p in file_paths:
        try:
            with open(p, "r", encoding="utf-8") as f:
                data = json.load(f)
                loaded_levels.append((p, data))
                if "id" in data:
                    all_level_ids.add(data["id"])
        except Exception as e:
            print(f"Error loading {p}: {e}")
            sys.exit(1)

    overall_success = True

    # Registry agreement (runs with real levels in the set — fixture
    # runs validate the frozen TEST set, which intentionally has no
    # registry entries): every level file needs a registry id and every
    # live registry entry needs its file.  This is the loud failure for
    # unregistered levels (fix: save the level in the editor).
    if any(not sid.startswith("test_") for sid in all_level_ids):
        for warn in registry_warnings():
            print(f"WARNING: {warn}")
        for err in validate_registry_consistency():
            print(f"ERROR: {err}")
            overall_success = False

    # Cross-file check: nonzero actor_id values drive persistent defeat
    # tracking in GameState.world and must be unique across scenes.
    seen_actor_ids = {}
    for p, data in loaded_levels:
        for obj in data.get("objects", []):
            props = obj.get("properties", {}) or {}
            aid = props.get("actor_id", 0)
            if isinstance(aid, int) and aid != 0:
                if aid in seen_actor_ids:
                    print(f"ERROR: actor_id {aid} used by both "
                          f"'{seen_actor_ids[aid]}' and '{obj.get('id')}' (must be unique)")
                    overall_success = False
                else:
                    seen_actor_ids[aid] = obj.get("id")

    # Cross-file check: neighbor links should be reciprocal (walk back).
    # One-sided links still compile (walk in works); warn so the missing
    # return is a conscious choice, not an oversight.
    _OPPOSITE = {"north": "south", "south": "north", "east": "west", "west": "east"}
    _by_id = {}
    for _, data in loaded_levels:
        if isinstance(data, dict) and data.get("id"):
            _by_id[data["id"]] = data
    for p, data in loaded_levels:
        if not isinstance(data, dict):
            continue
        for direction in ("north", "south", "east", "west"):
            target = ((data.get("neighbors", {}) or {}).get(direction) or "").strip()
            if not target or target not in _by_id:
                continue
            back = ((_by_id[target].get("neighbors", {}) or {}).get(_OPPOSITE[direction]) or "").strip()
            if back != data.get("id"):
                # A point-exit trigger back counts as a return path (e.g. a
                # door into a map whose whole edge belongs elsewhere).
                returns = [e for e in (_by_id[target].get("exits", []) or [])
                           if e.get("target_scene") == data.get("id")]
                if returns:
                    continue
                print(f"WARNING: {os.path.basename(p)} edge '{direction}' -> '{target}' "
                      f"has no return link ({target} {_OPPOSITE[direction]} is '{back or 'unset'}')")

    # Cross-file gate: whole-edge links must pair up geometrically so the
    # player can never land inside a wall or get trapped (collision.py is
    # the one implementation; compile.py aborts on the same errors).
    if any(not sid.startswith("test_") for sid in all_level_ids):
        _pair_errors, _pair_warnings = neighbor_pairing_issues(_by_id, tilesets)
        for warn in _pair_warnings:
            print(f"WARNING: {warn}")
        for err in _pair_errors:
            print(f"ERROR: {err}")
            overall_success = False
        # Point exits: landing cell must be walkable (stuck spawn) and the
        # gate art must read as a portal (invisible triggers).
        _px_errors, _px_warnings = point_exit_issues(_by_id, tilesets)
        for warn in _px_warnings:
            print(f"WARNING: {warn}")
        for err in _px_errors:
            print(f"ERROR: {err}")
            overall_success = False

    for p, data in loaded_levels:
        basename = os.path.basename(p)
        print(f"\n{basename}\n")
        valid, errors, warnings, passed = validate_level(data, tilesets, all_level_ids)

        for p_item in passed:
            print(f"✓ {p_item}")

        for w_item in warnings:
            print(f"WARNING: {w_item}")

        if errors:
            overall_success = False
            for e_item in errors:
                print(f"ERROR: {e_item}")
            print("\nLEVEL INVALID")
        else:
            print("\nLEVEL VALID")

    sys.exit(0 if overall_success else 1)


if __name__ == "__main__":
    main()
