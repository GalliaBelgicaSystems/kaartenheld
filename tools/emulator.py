#!/usr/bin/env python3
"""
mgba CLI debugger transport for Game Boy RPG development harness.
Uses mgba's command-line debugger (-d) via PTY with raw TTY mode.
Authoritative bridge for Game Boy snapshot, telemetry, and screen inspection.
"""
import subprocess, pty, os, select, time, tty, termios, fcntl, re, signal, json, sys
from pathlib import Path

_TOOLS_DIR = Path(__file__).resolve().parent
if str(_TOOLS_DIR / "screen_compiler") not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR / "screen_compiler"))

_REPOROOT = Path(__file__).resolve().parent.parent


def _scene_id_maps():
    """SCENE_MAP / MAP_NAME_MAP derived from levels/registry.json (+ fixed
    TEST block): display names for snapshot bytes.  Unknown ids fall back
    to UNKNOWN_<n> at the use sites, so a stale map degrades loudly."""
    try:
        reg = json.loads((_REPOROOT / "levels" / "registry.json").read_text(
            encoding="utf-8"))
        scenes = reg.get("scenes", {})
        base = reg.get("_test_base", 240)
    except (OSError, ValueError):
        scenes, base = {}, 240
    test_names = ["test_field", "test_town", "test_forest",
                  "test_mountain_pass", "test_castle", "test_south_field"]
    real = {num: sid.upper() for sid, num in scenes.items()
            if isinstance(num, int)}
    test = {base + i: sid.upper() for i, sid in enumerate(test_names)}
    return {**real, **test}

DEBUG_PROTOCOL_VERSION = 1

TELEMETRY_CAPACITY = 32
TELEMETRY_EVENT_SIZE = 13

GAME_STATE_MAP = {0: "OVERWORLD", 1: "BATTLE", 2: "GAME_OVER", 3: "THANKS"}
SCREEN_MAP = {0: "OVERWORLD", 1: "DIALOGUE", 2: "BATTLE", 3: "GAME_OVER", 4: "THANKS",
              5: "SHOP", 6: "ITEM", 7: "ENDING", 8: "SAVE_LOAD", 9: "TITLE", 10: "INTRO",
              11: "TUTORIAL", 12: "SPLASH"}
SCENE_MAP = _scene_id_maps()
MUSIC_TRACK_MAP = {0: "NONE", 1: "OVERWORLD", 2: "BATTLE", 3: "VICTORY",
                   4: "TITLE", 5: "TOWN", 6: "DUNGEON", 7: "BOSS", 8: "MIMIC",
                   9: "DESOLATE", 10: "FOREST"}
BATTLE_TURN_MAP = {0: "PLAYER", 1: "ENEMY_DELAY", 2: "ENEMY", 3: "RESULT"}
BATTLE_RESULT_MAP = {0: "NONE", 1: "VICTORY", 2: "DEFEAT", 3: "FLED"}
MAP_NAME_MAP = dict(SCENE_MAP)
STORY_FLAG_ID_MAP = {1: "ARRIVED_TOWN", 2: "MET_MAYOR"}
# Per-game content range base (mirrors *_FIRST_GAME in the engine headers).
GAME_ID_BASE = 0x80


def _entity_id_map():
    """ENTITY_ID_MAP derived from the entity-type registries
    (tools/screen_compiler/entity_ids.py), the same source the ROM's
    generated header uses.  Falls back to the engine sentinels if the
    content is unreadable."""
    try:
        from entity_ids import entity_id_map
        return entity_id_map()
    except Exception:
        return {0: "NONE", 1: "PLAYER"}


ENTITY_ID_MAP = _entity_id_map()
INTERACTION_ID_MAP = {0: "NONE", 1: "DIALOGUE", 2: "COMBAT", 3: "SHOP", 4: "SAVE"}


def _dialogue_id_map():
    """DIALOGUE_ID_MAP derived from screens/dialogue/*.json (same
    sorted-filename assignment as dialogue_compile.py).  Falls back to
    {} (plus NONE) if the content is unreadable — callers render
    UNKNOWN_<n> for unmapped ids."""
    from dialogue_ids import dialogue_files, load_dialogue_json
    out = {0: "NONE"}
    try:
        files = dialogue_files()
    except OSError:
        return out
    for i, path in enumerate(files):
        try:
            data = load_dialogue_json(path)
        except (OSError, ValueError):
            continue
        out[0x80 + i] = data["_id"].upper()
    return out


DIALOGUE_ID_MAP = _dialogue_id_map()
BATTLE_ID_MAP = {0: "NONE", 1: "SLIME", 2: "BAT"}
EVENT_TYPE_MAP = {
    0: "PLAYER_MOVED", 1: "COLLISION", 2: "ENCOUNTER_STARTED",
    3: "BATTLE_STARTED", 4: "BATTLE_ACTION", 5: "DAMAGE_DEALT",
    6: "DAMAGE_RECEIVED", 7: "ENTITY_DEFEATED", 8: "BATTLE_WON",
    9: "BATTLE_LOST", 10: "GAME_STATE_CHANGED", 11: "MUSIC_CHANGED",
    12: "MAP_CHANGED", 13: "STORY_FLAG_SET", 14: "STORY_FLAG_CLEARED",
    15: "DIALOGUE_STARTED", 16: "DIALOGUE_NEXT", 17: "DIALOGUE_ENDED",
    18: "INTERACTION_ATTEMPT", 19: "RENDER_SCREEN", 20: "RENDER_DIALOGUE",
    21: "SCREEN_CHANGED", 22: "SCENE_CHANGED",
    23: "ACTOR_COLLISION", 24: "ACTOR_INTERACTION", 25: "ACTOR_COMBAT_START",
    26: "VARIABLE_SET", 27: "CARD_ADDED_TO_DECK", 28: "CARD_REMOVED_FROM_DECK",
    29: "ACTOR_STATE_CHANGE", 30: "SCRIPT_TRIGGERED", 31: "HEALED",
    32: "ITEM_USED", 33: "ITEM_USE_FAILED", 34: "CARD_PURCHASED",
    35: "CARD_PURCHASE_FAILED", 36: "CURRENCY_ADDED", 37: "CURRENCY_SPENT",
    38: "PROGRESSION_GAINED", 39: "LEVEL_UP", 40: "ITEM_EQUIPPED",
    41: "BATTLE_FLED", 42: "GAME_SAVED", 43: "GAME_LOADED",
    44: "CARD_ADDED_TO_COLLECTION", 45: "CARD_REMOVED_FROM_COLLECTION",
    46: "CARD_PLAYED", 47: "ENEMY_CARD_PLAYED", 48: "DECK_RESHUFFLED",
    49: "COMBO_RESOLVED", 50: "EFFECT_RESOLVED",
    51: "STATUS_APPLIED", 52: "STATUS_TICKED", 53: "STATUS_EXPIRED",
    54: "LOOT_CARD_ADDED", 55: "LOOT_GENERATED", 56: "CARD_SOLD",
    57: "TURN_SKIPPED", 58: "STATUS_RESISTED",
    59: "NEW_GAME_STARTED", 60: "GAME_CONTINUED", 61: "SOUND_TOGGLED",
    62: "TARGET_CHANGED",
    63: "CARDS_GREYED", 64: "CARDS_UNGREYED"
}
EVENT_ID_MAP = {GAME_ID_BASE + 0: "TOWN_ARRIVAL",
                GAME_ID_BASE + 1: "QUEST_START",
                GAME_ID_BASE + 2: "QUEST_ACTIVE",
                GAME_ID_BASE + 3: "QUEST_COMPLETE",
                GAME_ID_BASE + 4: "QUEST_DONE",
                GAME_ID_BASE + 5: "GUARD_AFTER_MAYOR",
                GAME_ID_BASE + 6: "GUARD_GREETING",
                GAME_ID_BASE + 7: "MONSTER_DEFEATED",
                GAME_ID_BASE + 8: "BOSS_DEFEATED",
                GAME_ID_BASE + 9: "MERCHANT_INTRO",
                GAME_ID_BASE + 10: "MERCHANT_DELIVER",
                GAME_ID_BASE + 11: "AMULET_PICKUP"}

DIRECTION_MAP = {0: "UP", 1: "DOWN", 2: "LEFT", 3: "RIGHT"}

# Static per-actor semantics resolved from the snapshot's actor ids.
# Visuals mirror levels/*.json properties.visual (see docs/glyphs.md).
ACTOR_INFO_MAP = {
    "PLAYER":      {"visual": "@", "hostile": False, "interaction": "NONE", "dialogue": "NONE", "battle": "NONE"},
    "SLIME":       {"visual": "E", "hostile": True,  "interaction": "COMBAT", "dialogue": "NONE", "battle": "SLIME"},
    "MAYOR":       {"visual": "M", "hostile": False, "interaction": "DIALOGUE", "dialogue": "MAYOR_GREETING", "battle": "NONE"},
    "GUARD":       {"visual": "G", "hostile": False, "interaction": "DIALOGUE", "dialogue": "GUARD_GREETING", "battle": "NONE"},
    "SHOPKEEPER":  {"visual": "S", "hostile": False, "interaction": "DIALOGUE", "dialogue": "SHOPKEEPER_GREETING", "battle": "NONE"},
    "BAT":         {"visual": "V", "hostile": True,  "interaction": "COMBAT", "dialogue": "NONE", "battle": "BAT"},
    "SLIME_LORD":  {"visual": "L", "hostile": True,  "interaction": "COMBAT", "dialogue": "NONE", "battle": "NONE"},
    "MERCHANT":    {"visual": "C", "hostile": False, "interaction": "SHOP", "dialogue": "NONE", "battle": "NONE"},
    "AMULET":      {"visual": "A", "hostile": False, "interaction": "DIALOGUE", "dialogue": "AMULET_NOTHING", "battle": "NONE"},
    "WIZARD":      {"visual": "W", "hostile": False, "interaction": "SAVE", "dialogue": "NONE", "battle": "NONE"},
    "SIGNPOST":    {"visual": "?", "hostile": False, "interaction": "DIALOGUE", "dialogue": "SIGNPOST", "battle": "NONE"},
}

# Fallback button masks.  At connect() these are overridden by the ROM's
# g_input_button_bits table (so injected input always matches the compiled
# InputButton layout, which a compile-time check ties to GBDK's joypad()).
BUTTON_NAMES = ["RIGHT", "LEFT", "UP", "DOWN", "A", "B", "SELECT", "START"]
BUTTON_MASKS = {
    "RIGHT": 0x01, "LEFT": 0x02, "UP": 0x04, "DOWN": 0x08,
    "A": 0x10, "B": 0x20, "SELECT": 0x40, "START": 0x80
}

SCENARIO_IDS = {
    "NEW_GAME": 1, "FIRST_ENCOUNTER": 2, "TOWN_ARRIVAL": 3,
    "TOWN_DEPARTURE": 4, "TOWN_REENTRY": 5, "MAYOR_ENCOUNTER": 6,
    "MAYOR_DIALOGUE": 7, "MAYOR_DIALOGUE_MOVEMENT_BLOCKED": 8,
    "GUARD_DIALOGUE": 9, "FONT_TEST": 10, "DIALOGUE_RENDER_TEST": 11,
    "BATTLE_ATTACK": 12, "GUARD_INTERACTION_DISTANCE": 13, "GAME_OVER": 14,
    "OVERWORLD_BOOT": 15, "DIALOGUE_BOOT": 16, "BATTLE_BOOT": 17,
    "GAME_OVER_BOOT": 18, "THANKS_BOOT": 19, "FOREST_BOOT": 20,
    "MOUNTAIN_PASS_BOOT": 21, "CASTLE_BOOT": 22, "TOWN_BOOT": 23,
    "ACTOR_COLLISION_BLOCKING": 24, "ACTOR_SHOPKEEPER": 25, "ACTOR_BAT": 26,
    "LARGE_MAP_SCROLL": 27, "FIELD_EAST_SCROLL": 28, "CAMERA_BOUNDARY_CLAMP": 29,
    "SCROLL_RENDER_ALIGNMENT": 30, "START_SWALLOWS_MAP_COMMIT": 31,
    "TITLE_BOOT": 32, "TOWN_GATE_STEP": 33
}

# ── Declarative initial-state descriptor ─────────────────────────────
# Mirrors STATE_LOAD_DESC_* in src/debug/telemetry.h.  The host serializes
# a scenario's initial_state JSON into this fixed-size byte descriptor and
# writes it to g_scen_state_buf, then sets g_scen_load_state.
STATE_LOAD_DESC_SCREEN_OFF = 1
STATE_LOAD_DESC_SCENE_OFF = 2
STATE_LOAD_DESC_PLAYER_X_OFF = 3
STATE_LOAD_DESC_PLAYER_Y_OFF = 4
STATE_LOAD_DESC_PLAYER_FACING_OFF = 5
STATE_LOAD_DESC_SEED_OFF = 6
STATE_LOAD_DESC_FLAGS_OFF = 10
STATE_LOAD_DESC_FLAGS_SIZE = 8
STATE_LOAD_DESC_VARIABLES_COUNT_OFF = 18
STATE_LOAD_DESC_VARIABLES_ENTRY_OFF = 19
STATE_LOAD_DESC_VARIABLES_ENTRY_SIZE = 3
STATE_LOAD_DESC_CURRENCY_COUNT_OFF = 67
STATE_LOAD_DESC_CURRENCY_ENTRY_OFF = 68
STATE_LOAD_DESC_CURRENCY_ENTRY_SIZE = 3
STATE_LOAD_DESC_PARTY_COUNT_OFF = 80
STATE_LOAD_DESC_PARTY_ENTRY_OFF = 81
STATE_LOAD_DESC_PARTY_ENTRY_SIZE = 3
STATE_LOAD_DESC_INVENTORY_COUNT_OFF = 93
STATE_LOAD_DESC_INVENTORY_ENTRY_OFF = 94
STATE_LOAD_DESC_INVENTORY_ENTRY_SIZE = 2
STATE_LOAD_DESC_WORLD_COUNT_OFF = 126
STATE_LOAD_DESC_WORLD_ENTRY_OFF = 127
STATE_LOAD_DESC_WORLD_ENTRY_SIZE = 3
STATE_LOAD_DESC_PROGRESSION_COUNT_OFF = 175
STATE_LOAD_DESC_PROGRESSION_ENTRY_OFF = 176
STATE_LOAD_DESC_PROGRESSION_ENTRY_SIZE = 6
STATE_LOAD_DESC_DIALOGUE_ID_OFF = 224
STATE_LOAD_DESC_START_BATTLE_OFF = 225
STATE_LOAD_DESC_GAME_OVER_CHOICE_OFF = 226
STATE_LOAD_DESC_FONT_TEST_OFF = 227
STATE_LOAD_DESC_EQUIPMENT_OFF = 228
# Deck section (v4): only applied by the ROM when present=1; count may be 0
# for an intentionally empty deck (battle fallback-deck coverage).
STATE_LOAD_DESC_DECK_PRESENT_OFF = 229
STATE_LOAD_DESC_DECK_COUNT_OFF = 230
STATE_LOAD_DESC_DECK_ENTRY_OFF = 231
MAX_DESC_DECK_CARDS = 20
# Status effects (v4): byte 251 = frozen bitmask (bit 0 = player, 1..3 = enemies)
STATE_LOAD_DESC_STATUS_OFF = 251
STATE_LOAD_DESC_SIZE = STATE_LOAD_DESC_STATUS_OFF + 1
STATE_LOAD_DESC_VERSION = 0x04

SCENE_NAME_TO_ID = {v: k for k, v in SCENE_MAP.items()}
SCREEN_NAME_TO_ID = {v: k for k, v in SCREEN_MAP.items()}
DIRECTION_NAME_TO_ID = {v: k for k, v in DIRECTION_MAP.items()}
STATE_FLAG_ID_MAP = {"ARRIVED_TOWN": 1, "MET_MAYOR": 2}
VARIABLE_ID_MAP = {"CHAPTER": 1, "MONSTERS_DEFEATED": 2,
                   "QUEST_MONSTER_HUNT": 3, "ENDING_SHOWN": 4,
                   "MERCHANT_QUEST": 5}
CHARACTER_ID_MAP = {"HERO": 1}
ITEM_ID_MAP = {"NONE": 0,
               "IRON_SWORD": 0x40, "WOODEN_SHIELD": 0x41,
               "WOOD_RING": 0x42, "FIRE_SWORD": 0x43,
               "POISON_DAGGER": 0x44, "AMULET": 0x45,
               "BOW_9": 0x46}
ACTOR_ID_MAP = {"SLIME_FIELD": 1, "SLIME_FOREST": 2, "BAT_FOREST": 3,
                "SPIDER_FIELD": 12,
                "SLIME_MOUNTAIN_PASS": 4, "BAT_CASTLE": 5}
ACTOR_STATE_NAME_MAP = {"ALIVE": 0, "DEFEATED": 1}
# Status IDs (mirrors src/rpg/status.h StatusId enum)
STATUS_ID_MAP = {"NONE": 0, "POISON": 1, "BURN": 2, "FREEZE": 3}
# Card type names -> CardType enum (src/battle/card.h):
# SW=SWORD 0, SH=SHIELD 1, BO=BOW 2, HE=HEAL 3, DA=DAGGER 4.
CARD_TYPE_MAP = {"SW": 0, "SH": 1, "BO": 2, "HE": 3, "DA": 4}
# Deck-builder filter names -> CardType (src/rpg/cards.h), plus ALL.
FILTER_TYPE_MAP = {"ALL": 0xFF, "ATTACK": 0, "DEFENSE": 1, "HEAL": 2,
                   "STATUS": 3, "UTILITY": 4}
# Deck-builder sort modes (src/screens/item_screen.c).
SORT_MODE_MAP = {"OFF": 0, "TYPE": 1, "POWER": 2, "COST": 3,
                 "POWER_DESC": 4, "COST_DESC": 5}
DIALOGUE_NAME_TO_ID = {v: k for k, v in DIALOGUE_ID_MAP.items()}
CURRENCY_ID_MAP = {"GOLD": 1}

# Progression target names -> (target_type, target_id).  The engine never
# branches on the type; the type only tags the owner of the progression.
PROG_TYPE_HERO = 1
PROG_TYPE_WEAPON = 2
PROG_TYPE_COMPANION = 3
PROGRESSION_TARGET_MAP = {
    "HERO_1": (PROG_TYPE_HERO, 1),
    "IRON_SWORD": (PROG_TYPE_WEAPON, 1),
    "COMPANION_1": (PROG_TYPE_COMPANION, 1),
}

# Inverted maps for turning parsed snapshot state back into names.
CHARACTER_ID_TO_NAME = {v: k for k, v in CHARACTER_ID_MAP.items()}
ITEM_ID_TO_NAME = {v: k for k, v in ITEM_ID_MAP.items()}
ACTOR_ID_TO_NAME = {v: k for k, v in ACTOR_ID_MAP.items()}
CURRENCY_ID_TO_NAME = {v: k for k, v in CURRENCY_ID_MAP.items()}
PROGRESSION_KEY_TO_NAME = {v: k for k, v in PROGRESSION_TARGET_MAP.items()}


def serialize_initial_state(initial_state):
    """Serialize a scenario initial_state dict into the fixed-size byte
    descriptor written into g_scen_state_buf."""
    buf = [0] * STATE_LOAD_DESC_SIZE
    buf[0] = STATE_LOAD_DESC_VERSION
    if not initial_state:
        return buf

    buf[STATE_LOAD_DESC_SCENE_OFF] = SCENE_NAME_TO_ID[initial_state.get("scene", "FIELD")]
    buf[STATE_LOAD_DESC_SEED_OFF:STATE_LOAD_DESC_SEED_OFF + 4] = list(
        (initial_state.get("seed", 42) & 0xFFFFFFFF).to_bytes(4, "little"))

    player = initial_state.get("player", {})
    buf[STATE_LOAD_DESC_PLAYER_X_OFF] = player.get("x", 4)
    buf[STATE_LOAD_DESC_PLAYER_Y_OFF] = player.get("y", 4)
    buf[STATE_LOAD_DESC_PLAYER_FACING_OFF] = DIRECTION_NAME_TO_ID.get(player.get("facing", "DOWN"), 1)

    screen = initial_state.get("screen")
    if screen:
        buf[STATE_LOAD_DESC_SCREEN_OFF] = SCREEN_NAME_TO_ID[screen]

    for name, val in (initial_state.get("flags") or {}).items():
        flag_id = STATE_FLAG_ID_MAP[name]
        byte = STATE_LOAD_DESC_FLAGS_OFF + (flag_id - 1) // 8
        bit = 1 << ((flag_id - 1) % 8)
        if val:
            buf[byte] |= bit

    for name, val in (initial_state.get("variables") or {}).items():
        var_id = VARIABLE_ID_MAP[name]
        count_off = STATE_LOAD_DESC_VARIABLES_COUNT_OFF
        entry_idx = buf[count_off]
        off = STATE_LOAD_DESC_VARIABLES_ENTRY_OFF + entry_idx * STATE_LOAD_DESC_VARIABLES_ENTRY_SIZE
        buf[off] = var_id
        buf[off + 1] = val & 0xFF
        buf[off + 2] = (val >> 8) & 0xFF
        buf[count_off] = entry_idx + 1

    currency = initial_state.get("currency") or {}
    currency_items = list(currency.items())
    buf[STATE_LOAD_DESC_CURRENCY_COUNT_OFF] = len(currency_items)
    for i, (name, amt) in enumerate(currency_items):
        off = STATE_LOAD_DESC_CURRENCY_ENTRY_OFF + i * STATE_LOAD_DESC_CURRENCY_ENTRY_SIZE
        buf[off] = CURRENCY_ID_MAP[name]
        buf[off + 1] = amt & 0xFF
        buf[off + 2] = (amt >> 8) & 0xFF

    party = initial_state.get("party") or {}
    members = list(party.items())
    buf[STATE_LOAD_DESC_PARTY_COUNT_OFF] = len(members)
    for i, (name, stats) in enumerate(members):
        off = STATE_LOAD_DESC_PARTY_ENTRY_OFF + i * STATE_LOAD_DESC_PARTY_ENTRY_SIZE
        buf[off] = CHARACTER_ID_MAP[name]
        buf[off + 1] = stats.get("hp", 10)
        buf[off + 2] = stats.get("max_hp", 10)

    inventory = initial_state.get("inventory") or {}
    items = list(inventory.items())
    buf[STATE_LOAD_DESC_INVENTORY_COUNT_OFF] = len(items)
    for i, (name, qty) in enumerate(items):
        off = STATE_LOAD_DESC_INVENTORY_ENTRY_OFF + i * STATE_LOAD_DESC_INVENTORY_ENTRY_SIZE
        buf[off] = ITEM_ID_MAP[name]
        buf[off + 1] = qty

    if "deck" in initial_state:
        # Explicit deck construction: replaces the starter deck entirely.
        # An empty dict is meaningful (empty deck -> battle fallback path).
        # {name: copies} expands to COUNT sequential card ids.
        flat = []
        for name, qty in ((initial_state.get("deck") or {}).items()):
            flat.extend([ITEM_ID_MAP[name]] * int(qty))
        if len(flat) > MAX_DESC_DECK_CARDS:
            raise ValueError(
                f"deck expands to {len(flat)} cards; max is {MAX_DESC_DECK_CARDS}")
        buf[STATE_LOAD_DESC_DECK_PRESENT_OFF] = 1
        buf[STATE_LOAD_DESC_DECK_COUNT_OFF] = len(flat)
        for i, card_id in enumerate(flat):
            buf[STATE_LOAD_DESC_DECK_ENTRY_OFF + i] = card_id

    world = initial_state.get("world") or {}
    actors = list(world.items())
    buf[STATE_LOAD_DESC_WORLD_COUNT_OFF] = len(actors)
    for i, (name, actor_state) in enumerate(actors):
        off = STATE_LOAD_DESC_WORLD_ENTRY_OFF + i * STATE_LOAD_DESC_WORLD_ENTRY_SIZE
        actor_id = ACTOR_ID_MAP[name]
        buf[off] = actor_id & 0xFF
        buf[off + 1] = (actor_id >> 8) & 0xFF
        buf[off + 2] = ACTOR_STATE_NAME_MAP.get(actor_state, 0)

    progression = initial_state.get("progression") or {}
    prog_items = list(progression.items())
    buf[STATE_LOAD_DESC_PROGRESSION_COUNT_OFF] = len(prog_items)
    for i, (name, stats) in enumerate(prog_items):
        off = STATE_LOAD_DESC_PROGRESSION_ENTRY_OFF + i * STATE_LOAD_DESC_PROGRESSION_ENTRY_SIZE
        ttype, tid = PROGRESSION_TARGET_MAP[name]
        buf[off] = ttype
        buf[off + 1] = tid & 0xFF
        buf[off + 2] = (tid >> 8) & 0xFF
        buf[off + 3] = stats.get("level", 1)
        progress = stats.get("progress", 0)
        buf[off + 4] = progress & 0xFF
        buf[off + 5] = (progress >> 8) & 0xFF

    dialogue = initial_state.get("dialogue")
    if dialogue:
        buf[STATE_LOAD_DESC_DIALOGUE_ID_OFF] = DIALOGUE_NAME_TO_ID.get(dialogue, 0)
    if initial_state.get("start_battle"):
        buf[STATE_LOAD_DESC_START_BATTLE_OFF] = 1
    buf[STATE_LOAD_DESC_GAME_OVER_CHOICE_OFF] = initial_state.get("game_over_choice", 0)
    if initial_state.get("font_test"):
        buf[STATE_LOAD_DESC_FONT_TEST_OFF] = 1
    # Status effects (v4): frozen bitmask (bit 0 = player, 1..3 = enemies)
    status = initial_state.get("status")
    if status:
        frozen_mask = 0
        if isinstance(status, dict):
            # Support {"frozen_mask": int} or {"frozen": ["PLAYER", "ENEMY1", ...]}
            if "frozen_mask" in status:
                frozen_mask = status["frozen_mask"] & 0xFF
            elif "frozen" in status:
                for target in status["frozen"]:
                    if target == "PLAYER":
                        frozen_mask |= 0x01
                    elif target.startswith("ENEMY"):
                        idx = int(target.replace("ENEMY", ""))
                        if 1 <= idx <= 3:
                            frozen_mask |= (1 << idx)
        elif isinstance(status, int):
            frozen_mask = status & 0xFF
        buf[STATE_LOAD_DESC_STATUS_OFF] = frozen_mask
    return buf

def decode_story_flags(flags_mask):
    active = []
    for flag_id, name in STORY_FLAG_ID_MAP.items():
        if (flags_mask & (1 << (flag_id - 1))) != 0:
            active.append(name)
    return active

def load_sym_map(sym_path):
    syms = {}
    if os.path.exists(sym_path):
        with open(sym_path, 'r') as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) == 2 and ':' in parts[0]:
                    addr_str = parts[0].split(':')[1]
                    name = parts[1]
                    try:
                        syms[name] = int(addr_str, 16)
                    except ValueError:
                        pass
    return syms


class TelemetryEventList(list):
    def __init__(self, events, events_lost=False, oldest_available_sequence=0):
        super().__init__(events)
        self.events_lost = events_lost
        self.oldest_available_sequence = oldest_available_sequence


class EmulatorSession:
    def __init__(self, rom_path="build/kaartenheld_debug.gb"):
        self.rom_path = rom_path
        self.sym_path = rom_path.replace(".gb", ".sym")
        self.master = None
        self.proc = None
        self.symbols = {}
        self.current_snapshot = {}
        self.button_masks = dict(BUTTON_MASKS)

    def get_symbol(self, sym_name):
        for candidate in [sym_name, f"_{sym_name}"]:
            if candidate in self.symbols:
                return self.symbols[candidate]
        raise KeyError(f"HARNESS ERROR: Required symbol '{sym_name}' not found")

    # ── PTY I/O helpers ─────────────────────────────────────────────

    def _drain(self):
        while True:
            try:
                os.read(self.master, 4096)
            except (BlockingIOError, OSError):
                break

    def _send(self, cmd):
        self._drain()
        os.write(self.master, (cmd + '\n').encode())

    def _read_until(self, timeout=5.0, marker=b'> '):
        """Read PTY output until marker is found (returns full buffered data)."""
        buf = b''
        start = time.time()
        while time.time() - start < timeout:
            try:
                data = os.read(self.master, 4096)
                if data:
                    buf += data
                    if marker in buf:
                        return buf
            except (BlockingIOError, OSError):
                time.sleep(0.001)
        return buf

    def _cmd(self, cmd_str, timeout=3.0):
        """Send command and wait for prompt response."""
        self._send(cmd_str)
        return self._read_until(timeout)

    def _memread(self, addr):
        """Read single byte from GB memory address."""
        out = self._cmd(f'r/1 0x{addr:04X}').decode(errors='ignore')
        vals = re.findall(r'0x([0-9A-Fa-f]+)', out)
        return int(vals[-1], 16) if len(vals) >= 2 else None

    def _memwrite(self, addr, val):
        """Write single byte to GB memory address."""
        self._cmd(f'w/1 0x{addr:04X} 0x{val:02X}')

    def _set_pc(self, addr):
        """Set the CPU program counter."""
        self._cmd(f'w/r pc 0x{addr:04X}')

    def _step_until(self, target_pc, max_steps=500):
        """Step with 'next' until PC reaches target_pc."""
        for i in range(max_steps):
            out = self._cmd('next').decode(errors='ignore')
            m = re.search(r'PC:\s*([0-9A-F]+)', out)
            pc = int(m.group(1), 16) if m else 0
            if pc == target_pc:
                return pc
            if pc == 0:
                break
        return 0

    # ── Connection management ───────────────────────────────────────

    def _wait_for_prompt(self, timeout=30.0):
        """Wait until the mGBA debugger prompt is ready.

        Returns True once the prompt marker (``> ``) is observed, False on
        timeout.  CI runners can be slow to start xvfb + mGBA; sending
        debugger commands before the prompt is up silently drops them (the
        harness-mode write, set_pc, and break are all lost), which makes
        connect() fail unrecoverably.
        """
        deadline = time.time() + timeout
        buf = b''
        while time.time() < deadline:
            try:
                data = os.read(self.master, 4096)
                if data:
                    buf += data
                    if b'> ' in buf:
                        return True
            except (BlockingIOError, OSError):
                time.sleep(0.05)
        return False

    def _read_registers(self):
        """Send 'status' and return the decoded register dump ('' if the
        debugger did not answer).  mGBA's CLI prints registers on `status`
        (and on breakpoint/step output), not on a standalone `r`."""
        return self._cmd('status', timeout=3.0).decode(errors='ignore')

    def _read_pc(self):
        """Return the current PC as an int, or None."""
        m = re.search(r'PC:\s*([0-9A-Fa-f]+)', self._read_registers())
        return int(m.group(1), 16) if m else None

    def _debugger_responsive(self, attempts=5):
        """Probe that the debugger answers commands (a register dump with a
        PC line).  CI runners can be slow; retry briefly before giving up."""
        for _ in range(attempts):
            if 'PC:' in self._read_registers():
                return True
            time.sleep(0.2)
        return False

    def connect(self):
        if not os.path.exists(self.rom_path):
            raise FileNotFoundError(f"ROM not found: {self.rom_path}")

        # Private ROM copy per session (below) keeps battery SRAM isolated;
        # no stale-save cleanup needed here anymore.

        self.symbols = load_sym_map(self.sym_path)

        self.master, slave = pty.openpty()
        tty.setraw(self.master, termios.TCSANOW)
        flags = fcntl.fcntl(self.master, fcntl.F_GETFL)
        fcntl.fcntl(self.master, fcntl.F_SETFL, flags | os.O_NONBLOCK)

        # Headless/CI-safe launch: disable audio/video clock throttling.  With
        # audioSync on, mGBA paces the core to the audio clock; on a headless
        # runner with no sound device the audio callback never fires at the
        # expected rate, so after `c` the core stalls before game_render and
        # the frame-sync breakpoint never hits.
        #
        # Private ROM copy per session: mGBA writes battery SRAM next to the
        # ROM (<rom>.sav).  Parallel test workers share the same ROM path, so
        # a save written by one worker leaks into every other worker's
        # session (a stray SAVE_LOAD A-press loads foreign state and breaks
        # scenario determinism, AGENTS.md 20).  Each session runs on its own
        # copy so its .sav is private.
        import shutil, tempfile
        session_dir = tempfile.mkdtemp(prefix="gbharness_")
        self._session_dir = session_dir
        rom_copy = os.path.join(session_dir, "rom.gb")
        shutil.copyfile(self.rom_path, rom_copy)
        auth_file = os.path.join(session_dir, "Xauthority")
        cmd = ['xvfb-run', '-d', '-f', auth_file, 'mgba',
               '-C', 'audioSync=false', '-C', 'videoSync=false',
               '-d', rom_copy]
        self.proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave,
                                     close_fds=True, start_new_session=True)
        os.close(slave)

        # Readiness handshake: never send debugger commands until the prompt
        # is observed (see _wait_for_prompt).  A fixed sleep races CI boot.
        if not self._wait_for_prompt(timeout=30.0):
            raise RuntimeError("connect: mGBA debugger prompt not seen")
        self._drain()

        # Confirm the debugger actually answers commands before relying on it.
        responsive = self._debugger_responsive()
        if not responsive:
            raise RuntimeError("connect: mGBA debugger not responsive (no register dump)")

        game_render_addr = self.get_symbol("game_render")
        main_addr = self.get_symbol("main")
        hm_addr = self.get_symbol("g_harness_mode")

        # Break at main entry (allows CRT0 init to run cleanly: zeroing WRAM, setting stack, IE).
        self._cmd(f'b 0x{main_addr:04X}')
        self._send('c')
        out_main = self._read_until(timeout=10.0)
        if b'Hit breakpoint' not in out_main and f'{main_addr:04X}'.encode() not in out_main:
            self._set_pc(main_addr)
            self._cmd('w/r sp 0xE000')

        # Enable harness mode after CRT0 WRAM clear has executed.
        harness_ok = False
        for _ in range(5):
            self._memwrite(hm_addr, 0x01)
            if self._memread(hm_addr) == 0x01:
                harness_ok = True
                break
            time.sleep(0.1)
        if not harness_ok:
            raise RuntimeError("connect: could not set g_harness_mode=1 (readback failed)")

        # Set frame-sync breakpoint and run to first frame.
        hit = False
        any_hit = False
        self.attempts_log = attempts_log = []
        for attempt in range(8):
            brk = self._cmd(f'break 0x{game_render_addr:04X}')
            attempts_log.append(b'break: ' + brk)
            self._send('c')
            out = self._read_until(timeout=10.0)
            attempts_log.append(out)
            if b'Hit breakpoint' in out:
                any_hit = True
                if f'{game_render_addr:04X}'.encode() in out:
                    hit = True
                    break
            time.sleep(0.1)
        if not hit:
            tail = attempts_log[-1].decode(errors='replace')[-200:] if attempts_log else "(no output)"
            main_canary = (attempts_log and
                           f'{main_addr:04X}'.encode() in attempts_log[1])
            raise RuntimeError(
                f"connect: game_render breakpoint not hit "
                f"(responsive={responsive}, harness_mode={self._memread(hm_addr)}, "
                f"any_breakpoint_hit={any_hit}, "
                f"main_canary_hit={main_canary}, last_output_tail={tail!r})"
            )

        # First breakpoint may fire inside game_init() inner game_render().
        # Advance one frame to reach the main-loop game_render().
        boot_phase_addr = self.get_symbol("g_boot_phase")
        boot_phase = self._memread(boot_phase_addr)
        if boot_phase is None or boot_phase < 2:
            raise RuntimeError(
                f"connect: g_boot_phase={boot_phase}, expected >= 2. "
                "main() or ui_init() may not have executed"
            )

        if boot_phase < 4:
            # Advance to next game_render (from main loop, after game_init returns)
            self._send('next')
            time.sleep(0.005)
            self._drain()
            self._send('c')
            out = self._read_until(timeout=10.0)
            boot_phase = self._memread(boot_phase_addr)

        if boot_phase != 4:
            raise RuntimeError(
                f"connect: g_boot_phase={boot_phase}, expected 4. "
                "Boot sequence incomplete: main→ui_init→game_init→first_render"
            )

        self._load_button_masks()

    def disconnect(self):
        if self.proc:
            try:
                os.killpg(self.proc.pid, signal.SIGTERM)
            except Exception:
                pass
            try:
                self.proc.wait(timeout=1)
            except Exception:
                pass
            if self.proc.poll() is None:
                try:
                    os.killpg(self.proc.pid, signal.SIGKILL)
                except Exception:
                    pass
            try:
                self.proc.wait(timeout=1)
            except Exception:
                pass
            self.proc = None
        if self.master:
            try:
                os.close(self.master)
            except Exception:
                pass
            self.master = None
        if getattr(self, "_session_dir", None):
            try:
                import shutil as _shutil
                _shutil.rmtree(self._session_dir, ignore_errors=True)
            except Exception:
                pass
            self._session_dir = None

    # ── Frame control ───────────────────────────────────────────────

    def step(self, frames=1):
        """Advance N frames using breakpoint at _game_render."""
        for _ in range(frames):
            self._send('next')
            self._wait_for_prompt(timeout=5.0)
            self._drain()
            self._send('c')
            out = self._read_until(timeout=10.0)
            if b'Hit breakpoint' not in out:
                raise RuntimeError(f"Frame step failed: breakpoint not hit (out={out!r})")

    def wait(self, frames):
        self.step(frames)

    # ── Input injection ─────────────────────────────────────────────

    def _load_button_masks(self):
        """Read the ROM's g_input_button_bits table so injected input always
        matches the compiled InputButton layout (tied to GBDK's joypad() by a
        compile-time check in input.c).  Falls back to BUTTON_MASKS."""
        try:
            addr = self.get_symbol("g_input_button_bits")
        except KeyError:
            self.button_masks = dict(BUTTON_MASKS)
            return
        bits = []
        for i in range(8):
            b = self._memread(addr + i)
            if b is None:
                break
            bits.append(b)
        if len(bits) == 8:
            self.button_masks = dict(zip(BUTTON_NAMES, bits))
        else:
            self.button_masks = dict(BUTTON_MASKS)

    def press(self, button):
        btn_upper = button.upper()
        if btn_upper not in self.button_masks:
            raise ValueError(f"Unknown button '{button}'")
        mask = self.button_masks[btn_upper]
        addr = self.get_symbol("g_inp_mask")
        self._memwrite(addr, mask)
        time.sleep(0.005)
        self.step(1)

    def hold(self, button, frames):
        """Hold a button down for N frames.

        The ROM consumes g_inp_mask once per frame (edge-triggered, see
        AGENTS.md 52.10), so a single press() only registers for one frame.
        A hold rewrites g_inp_mask before every stepped frame, keeping
        input_held() true throughout -- the overworld's hold-to-move path.
        The mask is cleared on the last frame, so the button releases after
        exactly ``frames`` frames of held input.
        """
        btn_upper = button.upper()
        if btn_upper not in self.button_masks:
            raise ValueError(f"Unknown button '{button}'")
        mask = self.button_masks[btn_upper]
        addr = self.get_symbol("g_inp_mask")
        for i in range(frames):
            self._memwrite(addr, mask)
            time.sleep(0.005)
            self.step(1)

    # ── Scenario loading ────────────────────────────────────────────

    def load_scenario(self, scenario):
        """Load a scenario from its initial_state descriptor.

        ``scenario`` may be a scenario dict (with an ``initial_state`` key)
        or a bare dict of initial_state fields.  The descriptor is written
        into g_scen_state_buf and g_scen_load_state is set; the ROM applies
        it through the general declarative loader on the next frame.
        """
        if isinstance(scenario, dict) and "initial_state" in scenario:
            initial = scenario.get("initial_state", {})
        elif isinstance(scenario, dict):
            initial = scenario
        else:
            raise ValueError("load_scenario requires an initial_state dict")
        payload = serialize_initial_state(initial)
        addr = self.get_symbol("g_scen_state_buf")
        for i, val in enumerate(payload):
            self._memwrite(addr + i, val)
        flag_addr = self.get_symbol("g_scen_load_state")
        self._memwrite(flag_addr, 1)
        time.sleep(0.005)
        self.step(2)

    # ── State inspection ────────────────────────────────────────────

    SNAPSHOT_BASE_SIZE = 20
    SNAPSHOT_ACTOR_ENTRY_SIZE = 4
    MAX_SNAPSHOT_ACTORS = 4
    SNAPSHOT_BATTLE_ENERGY_OFF = SNAPSHOT_BASE_SIZE + (MAX_SNAPSHOT_ACTORS * SNAPSHOT_ACTOR_ENTRY_SIZE)
    SNAPSHOT_BATTLE_DRAW_OFF = SNAPSHOT_BATTLE_ENERGY_OFF + 1
    SNAPSHOT_BATTLE_DISCARD_OFF = SNAPSHOT_BATTLE_DRAW_OFF + 1
    SNAPSHOT_TOTAL_SIZE = SNAPSHOT_BATTLE_DISCARD_OFF + 1

    def snapshot(self):
        """Read SNAPSHOT_TOTAL_SIZE bytes from g_snap_buf."""
        addr = self.get_symbol("g_snap_buf")
        snap_bytes = []
        for i in range(self.SNAPSHOT_TOTAL_SIZE):
            b = self._memread(addr + i)
            if b is not None:
                snap_bytes.append(b)
        
        if len(snap_bytes) >= 13:
            parsed = {
                "game_state": GAME_STATE_MAP.get(snap_bytes[0], f"UNKNOWN_{snap_bytes[0]}"),
                "screen": SCREEN_MAP.get(snap_bytes[18], f"UNKNOWN_{snap_bytes[18]}") if len(snap_bytes) >= 19 else "UNKNOWN",
                "screen_id": snap_bytes[18] if len(snap_bytes) >= 19 else None,
                "scene": SCENE_MAP.get(snap_bytes[19], f"UNKNOWN_{snap_bytes[19]}") if len(snap_bytes) >= 20 else "UNKNOWN",
                "scene_id": snap_bytes[19] if len(snap_bytes) >= 20 else None,
                "player_x": snap_bytes[1],
                "player_y": snap_bytes[2],
                "player_hp": snap_bytes[3],
                "enemy_hp": snap_bytes[4],
                "enemy_active": snap_bytes[5],
                "music_track": MUSIC_TRACK_MAP.get(snap_bytes[6], f"UNKNOWN_{snap_bytes[6]}"),
                "battle_turn": BATTLE_TURN_MAP.get(snap_bytes[7], f"UNKNOWN_{snap_bytes[7]}"),
                "battle_result": BATTLE_RESULT_MAP.get(snap_bytes[8], f"UNKNOWN_{snap_bytes[8]}"),
                "battle_player_hp": snap_bytes[9],
                "battle_enemy_hp": snap_bytes[10],
                "map_id": MAP_NAME_MAP.get(snap_bytes[11], f"UNKNOWN_{snap_bytes[11]}"),
                "story_flags": snap_bytes[12],
                "story_flags_active": decode_story_flags(snap_bytes[12]),
                "dialogue_active": bool(snap_bytes[13]) if len(snap_bytes) >= 14 else False,
                "dialogue_line": snap_bytes[14] if len(snap_bytes) >= 15 else 0,
                "dialogue_id": snap_bytes[15] if len(snap_bytes) >= 16 else 0,
                "dialogue_id_name": DIALOGUE_ID_MAP.get(snap_bytes[15], f"UNKNOWN_{snap_bytes[15]}") if len(snap_bytes) >= 16 else "NONE",
                "player_facing": DIRECTION_MAP.get(snap_bytes[16], f"UNKNOWN_{snap_bytes[16]}") if len(snap_bytes) >= 17 else "UNKNOWN",
                "game_over_choice": snap_bytes[17] if len(snap_bytes) >= 18 else 0,
                "battle_energy": snap_bytes[self.SNAPSHOT_BATTLE_ENERGY_OFF] if len(snap_bytes) > self.SNAPSHOT_BATTLE_ENERGY_OFF else None,
                "battle_draw_remaining": snap_bytes[self.SNAPSHOT_BATTLE_DRAW_OFF] if len(snap_bytes) > self.SNAPSHOT_BATTLE_DRAW_OFF else None,
                "battle_discard_count": snap_bytes[self.SNAPSHOT_BATTLE_DISCARD_OFF] if len(snap_bytes) > self.SNAPSHOT_BATTLE_DISCARD_OFF else None,
                "actors": self._parse_actors(snap_bytes)
            }
            self.current_snapshot = parsed
            return parsed
        return self.current_snapshot

    # Extended RPG state snapshot: parses g_state_snap_buf (210 bytes,
    # layout in src/debug/telemetry.h STATE_SNAP_*).
    STATE_SNAP_SIZE = 210
    STATE_SNAP_VERSION = 6
    STATE_SNAP_FLAGS_OFF = 1
    STATE_SNAP_FLAGS_SIZE = 8
    STATE_SNAP_VARIABLES_OFF = 9
    STATE_SNAP_CURRENCY_COUNT_OFF = 25
    STATE_SNAP_CURRENCY_ENTRY_OFF = 26
    STATE_SNAP_CURRENCY_ENTRY_SIZE = 3
    STATE_SNAP_PARTY_OFF = 38
    STATE_SNAP_PARTY_ENTRY_SIZE = 3
    STATE_SNAP_INVENTORY_OFF = 51
    STATE_SNAP_INVENTORY_ENTRY_SIZE = 2
    STATE_SNAP_WORLD_OFF = 84
    STATE_SNAP_WORLD_ENTRY_SIZE = 3
    STATE_SNAP_PROGRESSION_COUNT_OFF = 133
    STATE_SNAP_PROGRESSION_ENTRY_OFF = 134
    STATE_SNAP_PROGRESSION_ENTRY_SIZE = 6
    STATE_SNAP_EQUIPMENT_OFF = 182
    STATE_SNAP_SCROLL_X_OFF = 183
    STATE_SNAP_SCROLL_Y_OFF = 184
    STATE_SNAP_WORLD_WIDTH_OFF = 185
    STATE_SNAP_WORLD_HEIGHT_OFF = 186
    STATE_SNAP_CAMERA_PX_X_OFF = 187
    STATE_SNAP_CAMERA_PX_Y_OFF = 188
    STATE_SNAP_DECK_COUNT_OFF = 189

    def state_snapshot(self):
        """Read g_state_snap_buf and return the canonical GameState as a dict."""
        addr = self.get_symbol("g_state_snap_buf")
        buf = []
        for i in range(self.STATE_SNAP_SIZE):
            b = self._memread(addr + i)
            if b is None:
                b = 0
            buf.append(b)

        if not buf or buf[0] != self.STATE_SNAP_VERSION:
            return None

        flags = []
        for name, fid in STATE_FLAG_ID_MAP.items():
            byte = buf[self.STATE_SNAP_FLAGS_OFF + (fid - 1) // 8]
            if byte & (1 << ((fid - 1) % 8)):
                flags.append(name)

        variables = {}
        for name, vid in VARIABLE_ID_MAP.items():
            off = self.STATE_SNAP_VARIABLES_OFF + (vid - 1) * 2
            variables[name] = buf[off] | (buf[off + 1] << 8)
            if variables[name] >= 0x8000:
                variables[name] -= 0x10000

        currency = {}
        currency_count = buf[self.STATE_SNAP_CURRENCY_COUNT_OFF]
        for i in range(currency_count):
            off = self.STATE_SNAP_CURRENCY_ENTRY_OFF + i * self.STATE_SNAP_CURRENCY_ENTRY_SIZE
            cid = buf[off]
            amt = buf[off + 1] | (buf[off + 2] << 8)
            if amt >= 0x8000:
                amt -= 0x10000
            cname = CURRENCY_ID_TO_NAME.get(cid)
            if cname:
                currency[cname] = amt

        party = []
        party_count = buf[self.STATE_SNAP_PARTY_OFF]
        for i in range(party_count):
            off = self.STATE_SNAP_PARTY_OFF + 1 + i * self.STATE_SNAP_PARTY_ENTRY_SIZE
            party.append({
                "id": buf[off],
                "hp": buf[off + 1],
                "max_hp": buf[off + 2],
            })

        inventory = []
        inv_count = buf[self.STATE_SNAP_INVENTORY_OFF]
        for i in range(inv_count):
            off = self.STATE_SNAP_INVENTORY_OFF + 1 + i * self.STATE_SNAP_INVENTORY_ENTRY_SIZE
            inventory.append({
                "item_id": buf[off],
                "quantity": buf[off + 1],
            })

        world = []
        world_count = buf[self.STATE_SNAP_WORLD_OFF]
        for i in range(world_count):
            off = self.STATE_SNAP_WORLD_OFF + 1 + i * self.STATE_SNAP_WORLD_ENTRY_SIZE
            actor_id = buf[off] | (buf[off + 1] << 8)
            world.append({
                "actor_id": actor_id,
                "state": "DEFEATED" if buf[off + 2] == 1 else "ALIVE",
            })

        progression = []
        prog_count = buf[self.STATE_SNAP_PROGRESSION_COUNT_OFF]
        for i in range(prog_count):
            off = self.STATE_SNAP_PROGRESSION_ENTRY_OFF + i * self.STATE_SNAP_PROGRESSION_ENTRY_SIZE
            ttype = buf[off]
            tid = buf[off + 1] | (buf[off + 2] << 8)
            level = buf[off + 3]
            progress = buf[off + 4] | (buf[off + 5] << 8)
            name = PROGRESSION_KEY_TO_NAME.get((ttype, tid),
                                               f"UNKNOWN_{ttype}_{tid}")
            progression.append({
                "name": name,
                "type": ttype,
                "id": tid,
                "level": level,
                "progress": progress,
            })

        deck = []
        deck_count = buf[self.STATE_SNAP_DECK_COUNT_OFF]
        for i in range(deck_count):
            off = self.STATE_SNAP_DECK_COUNT_OFF + 1 + i
            if off >= len(buf):
                break
            deck.append(buf[off])

        return {
            "flags": flags,
            "variables": variables,
            "currency": currency,
            "party": party,
            "inventory": inventory,
            "world": world,
            "progression": progression,
            "deck": deck,
            "scroll_x": buf[self.STATE_SNAP_SCROLL_X_OFF],
            "scroll_y": buf[self.STATE_SNAP_SCROLL_Y_OFF],
            "world_width": buf[self.STATE_SNAP_WORLD_WIDTH_OFF],
            "world_height": buf[self.STATE_SNAP_WORLD_HEIGHT_OFF],
            "camera_px_x": buf[self.STATE_SNAP_CAMERA_PX_X_OFF],
            "camera_px_y": buf[self.STATE_SNAP_CAMERA_PX_Y_OFF],
        }

    # ── Debug actions (semantic harness operations) ──────────────────

    DBG_ACT_NONE = 0
    DBG_ACT_ADD_ITEM = 1
    DBG_ACT_REMOVE_ITEM = 2
    DBG_ACT_ADD_CURRENCY = 3
    DBG_ACT_ADD_PROGRESS = 4
    DBG_ACT_BUY_ITEM = 5
    DBG_ACT_USE_ITEM = 6
    DBG_ACT_EQUIP_ITEM = 7
    DBG_ACT_SAVE = 8
    DBG_ACT_LOAD = 9
    DBG_ACT_SET_HAND_CARD = 10
    DBG_ACT_SET_FILTER = 11
    DBG_ACT_SET_SORT = 12
    DBG_ACT_DECK_ADD = 13
    DBG_ACT_SET_HAND_CARD_META = 14
    DBG_ACT_START_BATTLE = 15
    DBG_ACT_DECK_REMOVE = 16
    DBG_ACT_SET_HAND_CARD_STATUS = 17
    DBG_ACT_SET_ENEMY_HP = 18
    DBG_ACT_APPLY_STATUS = 19
    DBG_ACT_SET_HAND_RING = 20

    def debug_action(self, action, a0=0, a1=0, a2=0):
        """Run a debug action through the ROM's real mechanic functions."""
        addr = self.get_symbol("g_debug_action")
        payload = [action & 0xFF, a0 & 0xFF, a1 & 0xFF, (a1 >> 8) & 0xFF, a2 & 0xFF, 0]
        for i, val in enumerate(payload):
            self._memwrite(addr + i, val)
        flag_addr = self.get_symbol("g_debug_action_pending")
        self._memwrite(flag_addr, 1)
        time.sleep(0.005)
        self.step(2)

    def _parse_actors(self, snap_bytes):
        """Parse scene actors from bytes 20+ as (id, x, y, facing) entries."""
        actors = []
        if len(snap_bytes) < self.SNAPSHOT_BASE_SIZE:
            return actors
        entry = self.SNAPSHOT_ACTOR_ENTRY_SIZE
        base = self.SNAPSHOT_BASE_SIZE
        for i in range(self.MAX_SNAPSHOT_ACTORS):
            off = base + i * entry
            if off + entry > len(snap_bytes):
                break
            eid = snap_bytes[off]
            if eid == 0:
                continue
            id_name = ENTITY_ID_MAP.get(eid, f"UNKNOWN_{eid}")
            info = ACTOR_INFO_MAP.get(id_name, {})
            actors.append({
                "id": eid,
                "id_name": id_name,
                "x": snap_bytes[off + 1],
                "y": snap_bytes[off + 2],
                "facing": DIRECTION_MAP.get(snap_bytes[off + 3], f"UNKNOWN_{snap_bytes[off + 3]}"),
                "visual": info.get("visual", "?"),
                "hostile": info.get("hostile", False),
                "interaction": info.get("interaction", "NONE"),
                "dialogue": info.get("dialogue", "NONE"),
                "battle": info.get("battle", "NONE"),
            })
        return actors

    def get_tilemap_mirror(self):
        """Read the g_tilemap_mirror ring (DEBUG build; mGBA cannot read VRAM,
        so the ROM mirrors each background-tilemap write into WRAM).  Indexed
        by (world_row & 31) * 32 + (world_col & 31); values are ASCII
        console-font tile indices (ui_font_tile_base + (ch - ' '))."""
        addr = self.get_symbol("g_tilemap_mirror")
        return [self._memread(addr + i) for i in range(32 * 32)]

    def get_tilemap_attr_mirror(self):
        """Read the g_tilemap_attr_mirror ring (DEBUG build). Indexed
        by (world_row & 31) * 32 + (world_col & 31); values are CGB palette
        attribute indices (0..7)."""
        addr = self.get_symbol("g_tilemap_attr_mirror")
        return [self._memread(addr + i) for i in range(32 * 32)]

    def get_sfx_state(self):
        """Read the SFX trigger log: (total count, last SFX id).
        Per-trigger SFX telemetry would flood the 32-entry gameplay ring
        and evict gameplay events, so triggers land in these WRAM globals
        instead (see audio.c)."""
        count_addr = self.get_symbol("g_sfx_played_count")
        last_addr = self.get_symbol("g_sfx_last_id")
        lo = self._memread(count_addr)
        hi = self._memread(count_addr + 1)
        count = (lo or 0) | ((hi or 0) << 8)
        return count, self._memread(last_addr)

    def get_screen_buf(self):
        """Read 18×20 characters from g_ui_screen_buf."""
        addr = self.get_symbol("g_ui_screen_buf")
        rows = []
        for y in range(18):
            row_chars = []
            for x in range(20):
                b = self._memread(addr + y * 21 + x)
                if b is not None:
                    row_chars.append(chr(b) if 32 <= b <= 126 else ' ')
                else:
                    row_chars.append(' ')
            rows.append(''.join(row_chars))
        return '\n'.join(rows)

    def get_screen_dump(self):
        """Read 20x18 VRAM background tilemap from 0x9800.
        
        Assumes GBDK font_ibm loaded at tile base 0 (identity tile-to-ASCII
        mapping).  For custom fonts the tile-base offset from the font handle
        would need to be subtracted first.
        """
        rows = []
        for y in range(18):
            # VRAM tilemap stride is 32 tiles (20 visible + 12 off-screen)
            row_bytes = []
            for x in range(20):
                b = self._memread(0x9800 + y * 32 + x)
                if b is not None:
                    row_bytes.append(chr(b) if 32 <= b <= 126 else '.')
                else:
                    row_bytes.append('.')
            rows.append(''.join(row_bytes))
        return '\n'.join(rows)

    def get_telemetry(self, since_seq=None):
        """Read telemetry ring buffer from ROM memory."""
        count_addr = self.get_symbol("g_telemetry_count")
        head_addr = self.get_symbol("g_telemetry_head")
        buf_addr = self.get_symbol("g_telemetry_buffer")

        count_lo = self._memread(count_addr)
        if count_lo is None:
            count_lo = 0
        count = count_lo

        head = self._memread(head_addr)
        if head is None:
            head = 0

        num_slots = TELEMETRY_CAPACITY if count >= TELEMETRY_CAPACITY else count
        oldest_slot = head if count >= TELEMETRY_CAPACITY else 0

        all_events = []
        for i in range(num_slots):
            slot = (oldest_slot + i) % TELEMETRY_CAPACITY
            off = slot * TELEMETRY_EVENT_SIZE
            b = []
            for j in range(TELEMETRY_EVENT_SIZE):
                v = self._memread(buf_addr + off + j)
                if v is not None:
                    b.append(v)
            if len(b) < TELEMETRY_EVENT_SIZE:
                break

            seq = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24)
            frame = b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24)
            ev_type_id = b[8]
            ev_type_str = EVENT_TYPE_MAP.get(ev_type_id, f"UNKNOWN_{ev_type_id}")
            data = b[9:13]

            ev_obj = {
                "seq": seq, "frame": frame, "type": ev_type_str,
                "type_id": ev_type_id, "data": data
            }
            if ev_type_str in ("STORY_FLAG_SET", "STORY_FLAG_CLEARED"):
                ev_obj["flag_name"] = STORY_FLAG_ID_MAP.get(data[0], f"FLAG_{data[0]}")
            elif ev_type_str in ("DIALOGUE_STARTED", "DIALOGUE_NEXT", "DIALOGUE_ENDED"):
                ev_obj["dialogue_id_name"] = DIALOGUE_ID_MAP.get(data[0], f"UNKNOWN_{data[0]}")
            elif ev_type_str == "INTERACTION_ATTEMPT":
                ev_obj["target_entity"] = ENTITY_ID_MAP.get(data[3], f"UNKNOWN_{data[3]}")
            elif ev_type_str == "ACTOR_COLLISION":
                ev_obj["actor"] = ENTITY_ID_MAP.get(data[2], f"UNKNOWN_{data[2]}")
            elif ev_type_str == "ACTOR_INTERACTION":
                ev_obj["actor"] = ENTITY_ID_MAP.get(data[2], f"UNKNOWN_{data[2]}")
                ev_obj["interaction"] = INTERACTION_ID_MAP.get(data[3], f"UNKNOWN_{data[3]}")
            elif ev_type_str == "ACTOR_COMBAT_START":
                ev_obj["actor"] = ENTITY_ID_MAP.get(data[0], f"UNKNOWN_{data[0]}")
            elif ev_type_str == "SCRIPT_TRIGGERED":
                ev_obj["event_name"] = EVENT_ID_MAP.get(data[0], f"UNKNOWN_{data[0]}")
            
            all_events.append(ev_obj)

        events_lost = False
        oldest_avail_seq = all_events[0]["seq"] if all_events else 0
        if since_seq is not None and oldest_avail_seq > 0:
            if since_seq < oldest_avail_seq - 1:
                events_lost = True

        filtered = [ev for ev in all_events if since_seq is None or ev["seq"] > since_seq]
        return TelemetryEventList(filtered, events_lost=events_lost, oldest_available_sequence=oldest_avail_seq)
