#!/usr/bin/env python3
"""emit_sheet_sidecars.py -- Exact shade sidecars for composed art sheets.

Regenerates the composed sheets (battle/enemy/hero/npc/card composers run
first -- they are deterministic and byte-identical) and then emits the
strict per-tile shade sidecars consumed by `png2gb.py --shade-map`:

  generated/tiles/combat_shades.json      (battle enemy art)
  generated/tiles/enemy_ow_shades.json    (shared overworld enemy sprites)
  generated/tiles/hero_ow_shades.json     (hero overworld frames)
  generated/tiles/npc_shades.json         (village NPC map-art overlays)
  generated/tiles/card_frames_shades.json (battle UI icon sheet)

Ramp assignment is always explicit, from the same JSON the ROM compilers
read -- never inferred from pixel colors:

  combat:   screens/combat_art/*.json `palette` (BG-stamped sets) or
            `obj_palette` (OAM sets); OAM cells flatten onto SPRITES yellow
            (shade 0 = transparent), BG cells onto white.
  enemy_ow: screens/enemy_types/*.json `overworld.palette` (OBJ ramps).
  hero_ow:  screens/hero.json `overworld.palette` (OBJ ramp).
  npc:      village overlay slots mirror src/game/tiles_content.c npc_pals
            (town1/town1/town1/town2/town1/town1).
  card:     per-cell slot ramp matching the icon's skin color
            (tools/screen_compiler/battle_compile.py DEFAULT_SKIN/HUD).

Every pixel of every covered cell must equal a color of its assigned ramp
(exact hex match) and no cell may use more than 4 colors. Anything else is
a hard error naming sheet + cell + color + ramp -- all sheets are checked
before exiting so one run reports everything. Repaint the pixels (or fix
the JSON assignment); do not adjust this tool.

Waivers (tools/known_bad.json, reviewed like code): listed cells skip
validation and encode darkest-shade with a __waived__ marker -- visible
placeholders, never guesses. KAARTENHELD_STRICT=1 (CI) ignores waivers.

Part of `make manifest`.
"""

import sys
import json
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tools"))
sys.path.insert(0, str(REPO_ROOT / "tools" / "screen_compiler"))

from palette_txt import RAMPS, RAMP_NAMES, OBJ_BY_SLOT, _NAMES  # noqa: E402
from palette_txt import BATTLE_RAMPS  # noqa: E402

import compose_battle_sprites  # noqa: E402
import compose_enemy_sprites  # noqa: E402
import compose_hero_sprites  # noqa: E402
import compose_npc_tiles  # noqa: E402
import compose_card_frames  # noqa: E402

GENERATED_DIR = REPO_ROOT / "generated" / "tiles"
ERRORS = []
WAIVED = []

# Waiver list (reviewed like code): cells whose exact validation is
# skipped; they encode as darkest-shade boxes (visible "art pending"
# markers, listed on every build). CI sets KAARTENHELD_STRICT=1 to
# ignore waivers (hard fail). Adds/removals happen here deliberately,
# never silently.
STRICT = __import__("os").environ.get("KAARTENHELD_STRICT") == "1"


def load_waivers():
    if STRICT:
        return {}
    try:
        data = json.loads((REPO_ROOT / "tools" / "known_bad.json").read_text())
    except (OSError, ValueError) as e:
        fail("cannot load tools/known_bad.json: %s" % e)
        return {}
    out = {}
    for sheet, cells in data.get("cells", {}).items():
        for key in cells:
            out.setdefault(sheet, set()).add(str(key))
    return out


WAIVERS = load_waivers()


def waived(sheet, tx, ty):
    return not STRICT and ("%d,%d" % (tx, ty)) in WAIVERS.get(sheet, set())


def fail(msg):
    ERRORS.append(msg)
    print("sidecars FAIL: %s" % msg)


def base_ramp(name):
    try:
        return ["#%02x%02x%02x" % c
                for c in RAMPS["base"][RAMP_NAMES["base"].index(name)]]
    except ValueError:
        return None


def obj_ramp(name):
    try:
        return ["#%02x%02x%02x" % c
                for c in OBJ_BY_SLOT[_NAMES["obj"].index(name)]]
    except ValueError:
        return None


def battle_ramp(name):
    """RAMPS/BATTLE values (battle-time programming, slotless)."""
    if name not in BATTLE_RAMPS:
        return None
    return ["#%02x%02x%02x" % c for c in BATTLE_RAMPS[name]]


def village_ramp(name):
    try:
        return ["#%02x%02x%02x" % c
                for c in RAMPS["village"][RAMP_NAMES["village"].index(name)]]
    except ValueError:
        return None


def cell_used(img, tx, ty):
    px = img.load()
    return sorted({"#%02x%02x%02x" % px[tx * 8 + x, ty * 8 + y]
                   for y in range(8) for x in range(8)})


def check_cell(sheet, tx, ty, ramp, where):
    """Validate one cell against its ramp; return its shade map or None.

    Waived cells (tools/known_bad.json, non-strict runs only) skip
    validation and encode every pixel at shade 3 (darkest = visible
    placeholder, never a guess about intent)."""
    img = SHEETS[sheet]
    used = cell_used(img, tx, ty)
    if waived(sheet, tx, ty):
        WAIVED.append("%s (%d,%d) %s" % (sheet, tx, ty, where))
        print("sidecars WAIVED: %s cell (%d,%d) %s renders darkest-shade "
              "-- listed in tools/known_bad.json" % (sheet, tx, ty, where))
        m = {c: 3 for c in used}
        m["__waived__"] = True
        return m
    outside = [c for c in used if c not in ramp]
    if outside:
        fail("%s cell (%d,%d) %s uses %s, outside its %s ramp %s -- "
             "repaint the pixels or fix the JSON ramp assignment"
             % (sheet, tx, ty, where, ", ".join(outside), where, ramp))
        return None
    if len(used) > 4:
        fail("%s cell (%d,%d) %s uses %d colors %s -- "
             "Game Boy tiles hold 4 max" % (sheet, tx, ty, where, len(used), used))
        return None
    return {c: ramp.index(c) for c in used}


SHEETS = {}


def load_sheet(name, rel):
    img = Image.open(REPO_ROOT / rel).convert("RGB")
    SHEETS[name] = img
    return img


def combat_assignments():
    """Cell coord -> (ramp, set id) for the battle sheet."""
    out = {}
    art_dir = REPO_ROOT / "screens" / "combat_art"
    for path in sorted(art_dir.glob('*.json')):
        data = json.loads(path.read_text())
        sid = data.get("id", path.stem)
        if data.get("oam"):
            rampname = data["obj_palette"]
            # Battle-time values first (programmed per battle entry),
            # static OBJ slots as fallback (e.g. shared bat slot).
            ramp = battle_ramp(rampname)
            if ramp is None:
                ramp = obj_ramp(rampname)
        else:
            ramp = base_ramp(data["palette"])
            rampname = data["palette"]
        if ramp is None:
            fail("combat_art %s: ramp '%s' is not defined in "
                 "assets/palette.txt (restore pending?)" % (sid, rampname))
            continue
        for frame in ("frame0", "frame1"):
            for cell in data.get(frame) or []:
                if cell is None:
                    continue
                if cell not in compose_battle_sprites.TILE_COORDS:
                    fail("combat_art %s: unknown combat tile '%s'" % (sid, cell))
                    continue
                out[compose_battle_sprites.TILE_COORDS[cell]] = (ramp, sid)
    return out


def emit_combat():
    compose_battle_sprites.main()
    load_sheet("battle_sprites.png", "assets/battle_sprites.png")
    sidecar = {}
    for coord, (ramp, sid) in sorted(combat_assignments().items()):
        tx, ty = coord
        m = check_cell("battle_sprites.png", tx, ty, ramp, "set %s" % sid)
        if m is not None:
            sidecar["%d,%d" % (tx, ty)] = m
    # Padding blank cell: must stay all-white (shade 0 anywhere).
    bx, by = compose_battle_sprites.BLANK_COORD
    used = cell_used(SHEETS["battle_sprites.png"], bx, by)
    if used != ["#ffffff"]:
        fail("battle_sprites.png blank cell (%d,%d) is not all-white: %s"
             % (bx, by, used))
    else:
        sidecar["%d,%d" % (bx, by)] = {"#ffffff": 0}
    write_sidecar("combat_shades.json", sidecar)


def ow_assignments():
    """Cell coord -> (ramp, enemy id) for the shared overworld sheet."""
    out = {}
    for path in sorted((REPO_ROOT / "screens" / "enemy_types").glob("*.json")):
        data = json.loads(path.read_text())
        ow = data.get("overworld", {})
        ramp = obj_ramp(ow.get("palette"))
        if ramp is None:
            fail("enemy %s: ramp '%s' is not defined in "
                 "assets/palette.txt (restore pending?)" % (data.get("id"), ow.get("palette")))
            continue
        for cell in ow.get("cells", []):
            if cell not in compose_enemy_sprites.TILE_COORDS:
                fail("enemy %s: unknown overworld tile '%s'"
                     % (data.get("id"), cell))
                continue
            out[compose_enemy_sprites.TILE_COORDS[cell]] = (ramp, data.get("id"))
    return out


def emit_enemy_ow():
    compose_enemy_sprites.main()
    load_sheet("enemy_sprites.png", "assets/enemy_sprites.png")
    sidecar = {}
    covered = set()
    for coord, (ramp, eid) in sorted(ow_assignments().items()):
        tx, ty = coord
        covered.add(coord)
        m = check_cell("enemy_sprites.png", tx, ty, ramp, "enemy %s" % eid)
        if m is not None:
            sidecar["%d,%d" % (tx, ty)] = m
    img = SHEETS["enemy_sprites.png"]
    w, h = img.size[0] // 8, img.size[1] // 8
    for ty in range(h):
        for tx in range(w):
            if (tx, ty) not in covered and (tx, ty) in \
                    set(compose_enemy_sprites.TILE_COORDS.values()):
                print("sidecars info: enemy_sprites.png cell (%d,%d) is "
                      "editor-preview-only (no enemy references it) -- "
                      "no sidecar entry" % (tx, ty))
    write_sidecar("enemy_ow_shades.json", sidecar)


def emit_hero_ow():
    compose_hero_sprites.main()
    load_sheet("hero_sprites.png", "assets/hero_sprites.png")
    hero = json.loads((REPO_ROOT / "screens" / "hero.json").read_text())
    ow = hero.get("overworld", {})
    ramp = obj_ramp(ow.get("palette"))
    if ramp is None:
        fail("hero.json: ramp '%s' is not defined in assets/palette.txt "
             "(restore pending?)" % ow.get("palette"))
        write_sidecar("hero_ow_shades.json", {})
        return
    sidecar = {}
    for cell in ow.get("cells", []):
        if cell not in compose_hero_sprites.TILE_COORDS:
            fail("hero.json: unknown overworld tile '%s'" % cell)
            continue
        tx, ty = compose_hero_sprites.TILE_COORDS[cell]
        m = check_cell("hero_sprites.png", tx, ty, ramp, "hero %s" % cell)
        if m is not None:
            sidecar["%d,%d" % (tx, ty)] = m
    write_sidecar("hero_ow_shades.json", sidecar)


# Village overlay slots mirror src/game/tiles_content.c npc_pals order:
# guard, wizard, merchant and dogs on town1 (slot 5), mayor on town2 (slot 6).
NPC_PALS = ["town1", "town1", "town1", "town2", "town1", "town1"]


def emit_npc():
    compose_npc_tiles.main()
    load_sheet("npc_tiles.png", "assets/npc_tiles.png")
    sidecar = {}
    for x, (name, pal) in enumerate(zip(compose_npc_tiles.LAYOUT, NPC_PALS)):
        ramp = village_ramp(pal)
        m = check_cell("npc_tiles.png", x, 0, ramp, "npc %s" % name)
        if m is not None:
            sidecar["%d,0" % x] = m
    write_sidecar("npc_shades.json", sidecar)


# Card-sheet cell -> slot ramp, matching the icon's skin color assignment
# (battle_compile.py DEFAULT_SKIN/DEFAULT_HUD -> UI_COLOR_* slots):
# sword iron(2) shield wood(5) bow gold(6) heal field(3) dagger poison(4);
# fire(1) ice iron(2) poison(4); hp fire(1) ap gold(6) deck iron(2).
# Slot ramps: 0 fight1 1 fightgoblin 2 fightspider 3 fightslime
#             4 fight5 5 fightmimic 6 fight_text 7 fightboss.
# UI chrome (frames, bar, arrows, select arrow) lives on fight1.
CARD_RAMPS = {
    (0, 0): "fight1", (1, 0): "fight1", (2, 0): "fight1",
    (0, 1): "fight1", (1, 1): "fight1", (2, 1): "fight1",
    (0, 2): "fight1", (1, 2): "fight1", (2, 2): "fight1",
    (0, 3): "fight1", (1, 3): "fight1", (2, 3): "fightgoblin",
    (0, 4): "fight_text", (1, 4): "fight1", (2, 4): "fight1",
    (0, 5): "fightgoblin", (1, 5): "fight1", (2, 5): "fight5",
    (0, 6): "fight1", (1, 6): "fightmimic", (2, 6): "fight_text",
    (0, 7): "fight5", (1, 7): "fightslime",
    (0, 8): "fight1", (1, 8): "fight1", (2, 8): "fight1",
    (0, 9): "fight1", (1, 9): "fight1",
}


def emit_card_frames():
    compose_card_frames.main()
    load_sheet("card_frames.png", "assets/card_frames.png")
    names = {}
    for y, row in enumerate(compose_card_frames.LAYOUT):
        for x, name in enumerate(row):
            if name is not None:
                names[(x, y)] = name
    sidecar = {}
    for coord, rampname in sorted(CARD_RAMPS.items()):
        if coord not in names:
            continue
        ramp = base_ramp(rampname)
        if ramp is None:
            fail("card_frames.png icon %s: ramp '%s' is not defined in "
                 "assets/palette.txt (restore pending?)" % (names[coord], rampname))
            continue
        tx, ty = coord
        m = check_cell("card_frames.png", tx, ty, ramp,
                       "icon %s" % names[coord])
        if m is not None:
            sidecar["%d,%d" % (tx, ty)] = m
    # Padding blank cells (None in LAYOUT): must stay all-white.
    for y, row in enumerate(compose_card_frames.LAYOUT):
        for x, name in enumerate(row):
            if name is None:
                used = cell_used(SHEETS["card_frames.png"], x, y)
                if used != ["#ffffff"]:
                    fail("card_frames.png blank cell (%d,%d) is not "
                         "all-white: %s" % (x, y, used))
                else:
                    sidecar["%d,%d" % (x, y)] = {"#ffffff": 0}
    write_sidecar("card_frames_shades.json", sidecar)


def write_sidecar(name, sidecar):
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    path = GENERATED_DIR / name
    path.write_text(json.dumps(sidecar, indent=2))
    print("  Wrote shade sidecar: %s (%d tiles)" % (path, len(sidecar)))


def main():
    emit_combat()
    emit_enemy_ow()
    emit_hero_ow()
    emit_npc()
    emit_card_frames()
    if ERRORS:
        print("sidecars: %d problem(s) -- fix the art or the JSON "
              "assignment, never this tool" % len(ERRORS))
        return 1
    if WAIVED:
        print("sidecars: OK with %d waived cell(s) (darkest-shade "
              "placeholders, see tools/known_bad.json)" % len(WAIVED))
    else:
        print("sidecars: OK (combat/enemy_ow/hero_ow/npc/card_frames exact)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
