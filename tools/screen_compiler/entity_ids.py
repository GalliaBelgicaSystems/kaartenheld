#!/usr/bin/env python3
"""
entity_ids.py - Single source of truth for entity-type ids.

Entity types live in two JSON registries:
  * screens/enemy_types/*.json  - battle-capable and/or shared-overworld-art
    types (hostile enemies plus friendly critters like dog/fire).
  * screens/entity_types/*.json - plain NPC / quest / pickup types.

The union of their ids (sorted) assigns the engine's game-range entity ids
(ENTITY_ID_FIRST_GAME + N).  Nothing persists an EntityId numerically
(GameState.world is keyed by ActorId), so renumbering is safe; every user
references the generated ENTITY_ID_* name.  Both the C header
(tools/screen_compiler/entity_compile.py) and the host telemetry map
(tools/emulator.py) derive from here, so they cannot drift.
"""
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
ENEMY_DIR = REPO_ROOT / "screens" / "enemy_types"
ENTITY_DIR = REPO_ROOT / "screens" / "entity_types"
GAME_ID_BASE = 0x80

# Append-only assignment order.  Entity ids are referenced numerically by
# the host telemetry decoder and by existing scenario assertions, so the
# committed types keep their historical values; new types append after
# them (sorted) and can never shift an existing id.  Never reorder.
ENTITY_ORDER_PINNED = [
    "slime", "mayor", "guard", "shopkeeper", "bat", "slime_lord",
    "merchant", "amulet", "wizard", "signpost", "mimic", "spider",
    "kobold", "fire", "dog",
]


def _load_dir(directory):
    out = {}
    if not directory.is_dir():
        return out
    for path in sorted(directory.glob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise SystemExit(f"ERROR: cannot read {path}: {exc}")
        tid = data.get("id", path.stem)
        if tid in out:
            raise SystemExit(
                f"ERROR: duplicate entity type id '{tid}' ({path.name} and {out[tid]['_path']}).")
        data["_path"] = path.name
        data["_dir"] = directory.name
        data["_kind"] = ("enemy" if directory == ENEMY_DIR else "entity")
        out[tid] = data
    return out


def load_entity_types():
    """All entity types keyed by id (enemy_types + entity_types)."""
    out = {}
    for d in (ENEMY_DIR, ENTITY_DIR):
        for tid, data in _load_dir(d).items():
            if tid in out:
                raise SystemExit(
                    f"ERROR: duplicate entity type id '{tid}' "
                    f"({data['_path']} and {out[tid]['_path']}).")
            out[tid] = data
    return out


def entity_type_ids():
    """Entity type ids in append-only assignment order: the pinned
    committed types first, then any new types sorted after them."""
    types = load_entity_types()
    known = [t for t in ENTITY_ORDER_PINNED if t in types]
    extra = sorted(t for t in types if t not in ENTITY_ORDER_PINNED)
    return known + extra


def entity_id_name(tid):
    return "ENTITY_ID_" + tid.upper()


def entity_id_map():
    """{numeric id: NAME} for the host telemetry decoder (mirrors the
    engine's game-range assignment: ENTITY_ID_FIRST_GAME + i)."""
    out = {0: "NONE", 1: "PLAYER"}
    for i, tid in enumerate(entity_type_ids()):
        out[GAME_ID_BASE + i] = tid.upper()
    return out
