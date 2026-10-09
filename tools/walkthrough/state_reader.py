"""Semantic state reader for the release ROM (docs/verify-walkthrough.md §3 Phase 1).

Reads canonical gameplay state directly from WRAM through PyBoy, resolving
symbol addresses from the ROM's own `.sym` file (emitted next to the ROM by
`make release`).  Struct offsets are a documented mirror of the C headers;
every session validates the mirror with boot anchors so a layout change
fails loudly at boot instead of silently misreading mid-walk.

Only offsets BEFORE the variable-size World/Battle/Dialogue members of
`Game` are mirrored (state + its GameState fields) — those are fully
computable and stable.  Battle HP lives in its own `g_battle` symbol; audio
track in `g_audio_current_track`; card/shop prices are read from the ROM
IMAGE through banked ROM symbols (const tables).  Everything else (battle
phase/result, transient messages) stays on the established bg-text checks.
"""

import os
import re


# ── Struct offset mirror ─────────────────────────────────────────────
# Each constant cites the header it mirrors.  SDCC packs structs by
# default on the gb target (1-byte alignment), which these layouts assume.

# Game (src/core/game.h): screen(1) prev_screen(1) state(...) ...
OFF_STATE = 2

# GameState (src/rpg/state.h) — fields in declaration order.
OFF_SCENE = 0            # SceneState: SceneId, x, y, facing (4 B)
OFF_PARTY = 4            # PartyState: members[4]x(id,hp,max)=12 + count = 13
OFF_CARDS = 17           # CardState: collection + deck
OFF_CARDS_COLLECTION = OFF_CARDS         # entries[12]x(id,count)=24 + count
OFF_CARDS_DECK = OFF_CARDS + 25          # cards[20] + count = 21
OFF_FLAGS = OFF_CARDS + 46               # bytes[8]
OFF_VARIABLES = OFF_FLAGS + 8            # int16 x 16 = 32
OFF_CURRENCY = OFF_VARIABLES + 32        # int16 x 4 = 8

# CardDefinition (src/rpg/cards.h): id type power cost uses max_copies
# effect battle_type price status_id status_chance name[12] = 23 B.
CARD_DEF_SIZE = 23
CARD_DEF_PRICE = 8
CARD_DEF_ID = 0

# ShopDefinition (src/game/shops.h): id count buys items[SHOP_MAX_ITEMS]
# = 3 + 50 bytes (SDCC packs uint8_t fields, no padding).
SHOP_DEF_SIZE = 53
SHOP_DEF_ITEMS = 3
SHOP_MAX_ITEMS = 50

# CharacterState (src/rpg/state.h): id, hp, max_hp.
CHARACTER_STATE_SIZE = 3
CHARACTER_STATE_HP = 1
CHARACTER_STATE_MAX_HP = 2

# ── World (src/world/world.h) — needed to reach g_game.battle ────────
# Packed layout: width, height, map_id, encounter_actor_index,
# map_changed(bool), player Entity, actors[4], map[24][40], camera_px_x,
# camera_px_y, scroll_x, scroll_y, move_state, move_target_x,
# move_target_y, move_progress, move_outcome, tileset_kind, move_param,
# move_dir, move_exit_x, move_exit_y.
# Entity (src/world/entity.h): position(2), hp, max_hp, active(bool),
# facing, id = 7 bytes (validated at boot: it is exactly the pattern
# the old PLAYER_BOOT locate found).
ENTITY_SIZE = 7
MAX_WORLD_ACTORS = 4        # src/world/world.h
WORLD_WIDTH = 40
WORLD_HEIGHT = 24

def _world_actor_runtime_size(name_ptr_size):
    # WorldActorRuntime (src/world/world.h): actor_id(u16), id, active,
    # x, y, facing, hp, max_hp, flags, gold_reward, reward_currency,
    # display_name(ptr), visual, sprite_kind, ow_type, spawn_x, spawn_y,
    # ai_type, ai_step, ai_timer, move_state, move_target_x,
    # move_target_y, move_progress, battle_type, solo.
    return 2 + 10 + name_ptr_size + 14

def world_size(name_ptr_size):
    return (5 + ENTITY_SIZE
            + MAX_WORLD_ACTORS * _world_actor_runtime_size(name_ptr_size)
            + WORLD_WIDTH * WORLD_HEIGHT + 4 + 7 + 3)

# const char* size on this toolchain is not pinned down from headers
# (2-byte near vs 3-byte generic pointer); both candidates are probed
# once at the first battle read and the validating one is cached (see
# _battle_base).
NAME_PTR_CANDIDATES = (2, 3)

# Battle player Combatant (src/battle/combatant.h): hp, max_hp,
# name[12].  The hero's battle display name starts with 'H'.
BATTLE_PLAYER_HP = 0
BATTLE_PLAYER_MAX_HP = 1
BATTLE_ENEMIES = 14        # enemies[0] offset (player Combatant = 14 B)
BATTLE_ENEMY_STRIDE = 14   # Combatant size

# Battle hand region (src/battle/battle.h, after player + enemies[3] +
# enemy_count + target_idx + dirty): Deck deck; Card hand[5];
# selected_indices[5]; combo_count; cursor_pos; energy.
# Deck (src/battle/deck.h): cards[20]x8 + count + draw_idx +
# discard[20]x8 + discard_count = 323.
BATTLE_HAND = 14 + 3 * 14 + 3 + 323   # 382
CARD_SIZE = 8                # src/battle/card.h
CARD_TYPE = 0                # BattleCardType
CARD_VALUE = 1
CARD_USES = 2                # remaining uses (0xFF = unlimited)
CARD_COST = 3
BATTLE_SELECTED = BATTLE_HAND + 5 * CARD_SIZE
BATTLE_COMBO = BATTLE_SELECTED + 5          # selected_indices[5], then count
BATTLE_CURSOR = BATTLE_COMBO + 1
BATTLE_ENERGY = BATTLE_CURSOR + 1

# BattleCardType (src/battle/card.h)
BT_SWORD = 0
BT_SHIELD = 1
BT_BOW = 2
BT_HEAL = 3
BT_DAGGER = 4
BT_EMPTY = 0xFF
ATTACK_TYPES = (BT_SWORD, BT_BOW, BT_DAGGER)

# FlagState bit packing (src/rpg/state.h): flag N -> byte (N-1)/8,
# bit (N-1)%8.
def flag_bit(flag_id):
    return (flag_id - 1) // 8, (flag_id - 1) % 8


# ── Engine/game constants (mirrored from headers, cited) ────────────
# SceneId (src/screens/screen.h)
SCENE_FIELD = 0
SCENE_TOWN = 1
SCENE_FOREST = 2
SCENE_MOUNTAIN_PASS = 3
SCENE_CASTLE = 4
SCENE_SOUTH_FIELD = 5

# MusicTrack (src/audio/audio.h)
MUSIC_OVERWORLD = 1
MUSIC_BATTLE = 2
MUSIC_VICTORY = 3
MUSIC_TITLE = 4
MUSIC_TOWN = 5
MUSIC_DUNGEON = 6
MUSIC_BOSS = 7
MUSIC_MIMIC = 8
MUSIC_DESOLATE = 9
MUSIC_FOREST = 10

# Story flags (src/game/game_ids.h)
STORY_FLAG_ID_ARRIVED_TOWN = 1
STORY_FLAG_ID_MET_MAYOR = 2

# Variables (src/game/game_ids.h)
VARIABLE_ID_QUEST_MONSTER_HUNT = 3

# Currency (src/game/game_ids.h) — dense slots indexed by id-1.
CURRENCY_ID_GOLD = 1

# Cards (src/rpg/cards.h + src/game/game_ids.h)
CARD_FIRST_GAME = 0x40
CARD_IRON_SWORD = CARD_FIRST_GAME + 0
CARD_WOODEN_SHIELD = CARD_FIRST_GAME + 1
CARD_WOOD_RING = CARD_FIRST_GAME + 2

# Hero boot constants (src/game/content.c)
HERO_START_GOLD = 20
HERO_START_HP = 10
# Observed boot facing byte in state.scene / world.player (the compiled
# spawn-facing mapping; asserted, not derived).
BOOT_FACING = 2
ENTITY_ID_PLAYER = 1        # engine sentinel (src/world/entity.h)


class BattleProbeError(AssertionError):
    pass


class SymTable:
    """Parses an SDCC/rgbds-style .sym file ('BANK:ADDR name')."""

    _LINE = re.compile(r"^([0-9A-F]{2}):([0-9A-F]{4,6})\s+(\S+)\s*$")

    def __init__(self, sym_path):
        self.syms = {}
        with open(sym_path) as f:
            for line in f:
                m = self._LINE.match(line.strip())
                if not m:
                    continue
                bank, addr, name = (int(m.group(1), 16),
                                    int(m.group(2), 16), m.group(3))
                # A symbol can appear twice: the ROM initializer copy and
                # the WRAM variable (e.g. g_battle).  Keep both; the
                # wram()/rom() accessors pick the right one.
                self.syms.setdefault(name, []).append((bank, addr))
                if name.startswith("_"):
                    self.syms.setdefault(name[1:], []).append((bank, addr))

    def wram(self, name):
        """Address of a WRAM-resident symbol (0xC000-0xDFFF)."""
        for bank, addr in self.syms[name]:
            if bank == 0 and 0xC000 <= addr < 0xE000:
                return addr
        raise KeyError("symbol %s has no WRAM address %r"
                       % (name, self.syms.get(name)))

    def rom(self, name):
        """(bank, window_offset) of a banked ROM symbol (const tables).

        The linker encodes the bank in the high bits of the address
        (e.g. 0x24FFE = bank 2, window 0x4FFE)."""
        for bank, addr in self.syms[name]:
            if bank == 0 and addr > 0xFFFF:
                bank, addr = addr >> 16, addr & 0xFFFF
                if 0x4000 <= addr < 0x8000:
                    return bank, addr
                continue
            if bank != 0 and 0x4000 <= addr < 0x8000:
                return bank, 0x4000 + (addr & 0x3FFF)
        raise KeyError("symbol %s has no banked ROM address %r"
                       % (name, self.syms.get(name)))


def rom_offset(bank, window_offset):
    """Byte offset of a banked ROM window address inside the .gb image."""
    return bank * 0x4000 + (window_offset - 0x4000)


class StateReader:
    """Reads canonical gameplay state from the running release ROM."""

    def __init__(self, pb, rom_path):
        self.pb = pb
        self.sym = SymTable(os.path.splitext(rom_path)[0] + ".sym")
        self.g_game = self.sym.wram("g_game")
        self.g_audio = self.sym.wram("g_audio_current_track")
        self.state = self.g_game + OFF_STATE
        self.world = self.state + 209      # sizeof(GameState), see above
        self._battle_base = None           # resolved at first battle read
        self._world_ptr_size = None        # const-char* size once probed
        self._rom = open(rom_path, "rb")

    def close(self):
        self._rom.close()

    # ── raw reads ────────────────────────────────────────────────────
    def rd(self, addr, n):
        return bytes(self.pb.memory[i] for i in range(addr, addr + n))

    def rom_rd(self, sym_name, offset, n):
        bank, win = self.sym.rom(sym_name)
        self._rom.seek(rom_offset(bank, win) + offset)
        return self._rom.read(n)

    # ── canonical GameState (state.scene / party / cards / flags /
    #    variables / currency — all before the variable-size members) ──
    def scene_state(self):
        s = self.rd(self.state + OFF_SCENE, 4)
        return dict(scene_id=s[0], player_x=s[1], player_y=s[2],
                    facing=s[3])

    def hero(self):
        base = self.state + OFF_PARTY
        c = self.rd(base, CHARACTER_STATE_SIZE)
        return dict(id=c[0],
                    hp=c[CHARACTER_STATE_HP],
                    max_hp=c[CHARACTER_STATE_MAX_HP])

    def gold(self):
        slot = CURRENCY_ID_GOLD - 1
        raw = self.rd(self.state + OFF_CURRENCY + 2 * slot, 2)
        return int.from_bytes(raw, "little", signed=True)

    def flags(self):
        return self.rd(self.state + OFF_FLAGS, 8)

    def flag(self, flag_id):
        byte, bit = flag_bit(flag_id)
        return bool(self.flags()[byte] & (1 << bit))

    def variable(self, var_id):
        raw = self.rd(self.state + OFF_VARIABLES + 2 * (var_id - 1), 2)
        return int.from_bytes(raw, "little", signed=True)

    def collection(self):
        """List of (card_id, count) pairs in the collection."""
        base = self.state + OFF_CARDS_COLLECTION
        n = self.rd(base + 24, 1)[0]
        out = []
        for i in range(min(n, 12)):
            e = self.rd(base + 2 * i, 2)
            out.append((e[0], e[1]))
        return out

    def collection_count(self, card_id):
        return next((c for cid, c in self.collection() if cid == card_id), 0)

    def deck_count(self):
        return self.rd(self.state + OFF_CARDS_DECK + 20, 1)[0]

    # ── battle (g_game.battle; located via the World mirror) ─────────
    def _resolve_battle_base(self):
        """Resolve g_game.battle once: probe the two const-char* size
        candidates for the WorldActorRuntime mirror and cache whichever
        validates against the live battle (hero hp/max + 'H' name)."""
        if self._battle_base is not None:
            return self._battle_base
        state_size = 209
        for ptr in NAME_PTR_CANDIDATES:
            cand = self.g_game + OFF_STATE + state_size + world_size(ptr)
            head = self.rd(cand, 2)
            name = self.rd(cand + 2, 1)
            if (head == bytes([HERO_START_HP, HERO_START_HP])
                    and name == b"H"):
                self._battle_base = cand
                self._world_ptr_size = ptr
                return cand
        raise BattleProbeError(
            "could not locate g_game.battle with either const-char* "
            "candidate; World mirror or Battle layout changed — update "
            "tools/walkthrough/state_reader.py from src/world/world.h")

    def world_hostile_count(self, tileset_kind):
        """Number of active hostile slots in the current scene
        (World.actors[0..MAX_WORLD_ACTORS)).  Proves the generated
        actor-table count registered THIS scene's hostiles (the count used
        to be a hardcoded 6, silently dropping maps 8+).

        The const-char* size (which sets the actor stride) is not a
        separate symbol, so it is resolved from the World tail bytes
        (tileset_kind, then move_param, move_dir, move_exit_x, move_exit_y)
        against the caller's expected kind -- the battle probe cannot be
        used because g_game.battle is only initialized once a battle
        starts."""
        ptr = self._world_ptr_size
        if ptr is None:
            for cand in NAME_PTR_CANDIDATES:
                if self.rd(self.world + world_size(cand) - 5, 1)[0] == tileset_kind:
                    ptr = cand
                    break
            if ptr is None:
                return -1
            self._world_ptr_size = ptr
        base = self.world + 5 + ENTITY_SIZE
        stride = _world_actor_runtime_size(ptr)
        n = 0
        for s in range(MAX_WORLD_ACTORS):
            if self.rd(base + s * stride + 3, 1)[0]:
                n += 1
        return n

    def world_tail_candidates(self):
        """Observed World-tail byte per const-char* stride candidate.

        Returns {ptr_size: byte} read at world + world_size(ptr) - 5 for
        each candidate, without resolving or caching anything — so a
        mismatch report can show observed-vs-expected on the spot instead
        of a bare -1 (see _check_hostiles in walks.py)."""
        out = {}
        for cand in NAME_PTR_CANDIDATES:
            out[cand] = self.rd(self.world + world_size(cand) - 5, 1)[0]
        return out

    def battle_player_hp(self):
        return self.rd(self._resolve_battle_base()
                       + BATTLE_PLAYER_HP, 1)[0]
    def battle_player_max_hp(self):
        return self.rd(self._resolve_battle_base()
                       + BATTLE_PLAYER_MAX_HP, 1)[0]

    def battle_enemy_hp(self, idx):
        return self.rd(self._resolve_battle_base() + BATTLE_ENEMIES
                       + BATTLE_ENEMY_STRIDE * idx, 1)[0]

    def battle_enemy_max_hp(self, idx):
        return self.rd(self._resolve_battle_base() + BATTLE_ENEMIES
                       + BATTLE_ENEMY_STRIDE * idx + 1, 1)[0]

    def battle_hand(self):
        """The 5 Card slots as (type, value, cost) triples."""
        base = self._resolve_battle_base() + BATTLE_HAND
        out = []
        for i in range(5):
            c = self.rd(base + CARD_SIZE * i, 4)
            out.append((c[CARD_TYPE], c[CARD_VALUE], c[CARD_COST]))
        return out

    def battle_cursor(self):
        return self.rd(self._resolve_battle_base() + BATTLE_CURSOR, 1)[0]

    def battle_combo_count(self):
        return self.rd(self._resolve_battle_base() + BATTLE_COMBO, 1)[0]

    def battle_energy(self):
        return self.rd(self._resolve_battle_base() + BATTLE_ENERGY, 1)[0]

    # ── audio ────────────────────────────────────────────────────────
    def music_track(self):
        return self.rd(self.g_audio, 1)[0]

    # ── ROM-image content reads (const tables in banked ROM) ─────────
    def card_price(self, card_id):
        idx = card_id - CARD_FIRST_GAME
        raw = self.rom_rd("g_cards", CARD_DEF_SIZE * idx + CARD_DEF_PRICE, 1)
        return raw[0]

    def shop_stock(self, shop_idx):
        base = SHOP_DEF_SIZE * shop_idx
        count = self.rom_rd("g_shops", base + 1, 1)[0]
        items = self.rom_rd("g_shops", base + SHOP_DEF_ITEMS,
                            min(count, SHOP_MAX_ITEMS))
        return list(items)

    # ── boot anchors (§3 Phase 1) ────────────────────────────────────
    def assert_boot_anchors(self, spawn, label=""):
        """Validate the offset mirror against known boot state.  A
        failure means a header layout changed — update the mirror in
        this file (every constant cites its header)."""
        problems = []
        s = self.scene_state()
        if s["scene_id"] != SCENE_FIELD:
            problems.append("scene_id=%d != SCENE_FIELD(%d)"
                            % (s["scene_id"], SCENE_FIELD))
        if (s["player_x"], s["player_y"]) != (spawn["x"], spawn["y"]):
            problems.append("pos=(%d,%d) != spawn (%d,%d)"
                            % (s["player_x"], s["player_y"],
                               spawn["x"], spawn["y"]))
        h = self.hero()
        if h["hp"] != HERO_START_HP or h["max_hp"] != HERO_START_HP:
            problems.append("hero hp=%d/%d != %d"
                            % (h["hp"], h["max_hp"], HERO_START_HP))
        if self.gold() != HERO_START_GOLD:
            problems.append("gold=%d != %d" % (self.gold(), HERO_START_GOLD))
        if self.flag(STORY_FLAG_ID_ARRIVED_TOWN):
            problems.append("ARRIVED_TOWN set at boot")
        # World mirror: the player Entity must sit exactly at
        # world+5 with the boot spawn pattern.
        pe = self.rd(self.world + 5, ENTITY_SIZE)
        want = bytes([spawn["x"], spawn["y"], HERO_START_HP, HERO_START_HP,
                      1, BOOT_FACING, ENTITY_ID_PLAYER])
        if pe != want:
            problems.append("world.player=%s != %s"
                            % (pe.hex(), want.hex()))
        if problems:
            raise AssertionError(
                "boot anchors FAILED%s: struct offset mirror drifted; "
                "update the mirror in tools/walkthrough/state_reader.py "
                "from src/core/game.h / src/rpg/state.h. Problems: %s"
                % ((" [%s]" % label) if label else "", "; ".join(problems)))
