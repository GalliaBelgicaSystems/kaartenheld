"""Shared dialogue id assignment (single source of truth).

screens/dialogue/*.json sorted by filename -> DIALOGUE_ID_FIRST_GAME + i.
Nothing persists numeric dialogue ids (DialogueState is runtime-only;
events and actor props reference DIALOGUE_ID_* names resolved at compile
time; the harness asserts names), so renumbering on add/rename is safe.
The compiler, emulator, test-runner maps, and validator all derive from
here — never hardcode dialogue ids elsewhere.
"""
import json
import re
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent

DIALOGUE_DIRNAME = "dialogue"
DIALOGUE_FIRST_GAME = 0x80


def dialogue_dir():
    return REPO_ROOT / "screens" / DIALOGUE_DIRNAME


def dialogue_files():
    """Sorted dialogue JSON paths."""
    d = dialogue_dir()
    if not d.is_dir():
        return []
    return sorted(p for p in d.glob("*.json") if p.is_file())


def load_dialogue_json(path):
    """Parse one dialogue file; id falls back to the filename stem
    (same convention as enemy_types)."""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    data["_id"] = data.get("id") or Path(path).stem
    data["_path"] = str(path)
    return data


def dialogue_id_map():
    """{DIALOGUE_ID_NAME: numeric id} in sorted-filename order."""
    out = {}
    for i, path in enumerate(dialogue_files()):
        data = load_dialogue_json(path)
        name = "DIALOGUE_ID_" + data["_id"].upper()
        if name in out:
            raise SystemExit(
                f"ERROR: duplicate dialogue id '{data['_id']}' "
                f"({path} and {out[name][1]}).")
        out[name] = (DIALOGUE_FIRST_GAME + i, str(path))
    return {k: v[0] for k, v in out.items()}


def load_story_flags():
    """{STORY_FLAG_ID_* name: value} parsed from src/game/game_ids.h
    (hand-managed game content; the compiler only resolves references)."""
    ids = set()
    path = REPO_ROOT / "src" / "game" / "game_ids.h"
    try:
        text = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        print("WARNING: game_ids.h not found; skipping story-flag validation")
        return ids
    for m in re.finditer(r"#define\s+(STORY_FLAG_ID_[A-Z0-9_]+)\b", text):
        ids.add(m.group(1))
    return ids
