"""Scene id registry: levels/registry.json is the single source of truth
for real-scene ids.

Humans add levels in the editor; the editor assigns the next dense id on
first save (see /api/save-level in tools/level_editor/vite.config.ts).
Every consumer — compile.py emission, validate.py, emulator.py, the
walkthrough planner — derives ids from here, so adding a level never
requires hand-editing id lists in multiple tools.

Rules (enforced by load_registry):
- ids are append-only and never reused (deleted levels tombstone into
  "_retired" as {sid: id});
- test_* names are refused (the TEST block is separate and fixed);
- no real id may reach the TEST block base.
"""
import json
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
REGISTRY_FILENAME = "registry.json"
# Current on-disk schema version.  Bump when the registry shape changes;
# load_registry accepts older versions only via an explicit migration path.
REGISTRY_VERSION = 1

# Fixed harness-test fixture names.  Values come from the registry's
# _test_base (a fixed block that never moves); only the NAMES live here.
TEST_SCENE_ORDER = ["test_field", "test_town", "test_forest",
                    "test_mountain_pass", "test_castle", "test_south_field"]


def registry_path():
    return REPO_ROOT / "levels" / REGISTRY_FILENAME


def is_level_file(path):
    """True for compilable level JSON (excludes the registry itself)."""
    p = Path(path)
    return p.suffix == ".json" and p.name != REGISTRY_FILENAME


def load_registry():
    """Load and validate levels/registry.json.

    Returns {"scenes": {sid: id}, "retired": {sid: id}, "test_base": N}.
    Fails loudly (never silently regenerates: a fresh assignment could
    renumber existing scenes and invalidate saves — restore from git)."""
    path = registry_path()
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise SystemExit(
            f"ERROR: cannot read {path}: {exc}. Restore it from git "
            f"(it holds every scene id assignment).")
    version = data.get("version", 0)
    if not isinstance(version, int) or version > REGISTRY_VERSION:
        raise SystemExit(
            f"ERROR: {path}: registry version {version!r} is not supported "
            f"by this build (understands up to {REGISTRY_VERSION}). Update "
            f"the toolchain or restore an older registry from git.")
    # version 0 (pre-version) is accepted and upgraded on the next write.
    scenes = data.get("scenes")
    if not isinstance(scenes, dict) or not scenes:
        raise SystemExit(f"ERROR: {path} has no 'scenes' mapping.")
    seen_ids = {}
    for sid, num in scenes.items():
        if not isinstance(num, int) or num < 0:
            raise SystemExit(
                f"ERROR: {path}: id for '{sid}' must be a non-negative int.")
        if sid.startswith("test_"):
            raise SystemExit(
                f"ERROR: {path}: '{sid}' is reserved for harness fixtures "
                f"(TEST block); real levels must not use test_* names.")
        if num in seen_ids:
            raise SystemExit(
                f"ERROR: {path}: duplicate id {num} on '{sid}' and "
                f"'{seen_ids[num]}'. Ids are append-only and never reused.")
        seen_ids[num] = sid
    test_base = data.get("_test_base", 240)
    if not isinstance(test_base, int) or test_base < 0:
        raise SystemExit(f"ERROR: {path}: '_test_base' must be an int.")
    retired = data.get("_retired", {})
    if not isinstance(retired, dict):
        raise SystemExit(f"ERROR: {path}: '_retired' must be a sid->id map.")
    for sid, num in retired.items():
        if not isinstance(num, int) or num < 0:
            raise SystemExit(
                f"ERROR: {path}: retired id for '{sid}' must be a "
                f"non-negative int.")
        if num in seen_ids:
            raise SystemExit(
                f"ERROR: {path}: retired id {num} ('{sid}') is still "
                f"assigned to '{seen_ids[num]}'.")
    for num in seen_ids:
        if num >= test_base:
            raise SystemExit(
                f"ERROR: {path}: real id {num} collides with the TEST block "
                f"(base {test_base}). Move the TEST block first.")
    return {"version": version if version else REGISTRY_VERSION,
            "scenes": dict(scenes),
            "retired": dict(retired), "test_base": test_base}


def test_scene_ids(registry=None):
    """{test_sid: numeric id} for the fixed TEST block."""
    registry = registry or load_registry()
    base = registry["test_base"]
    return {sid: base + i for i, sid in enumerate(TEST_SCENE_ORDER)}
