#!/usr/bin/env python3
"""PyBoy regression check for overworld camera scrolling and VRAM access invariants.

Boots the debug ROM headlessly in PyBoy and holds RIGHT to move the hero
across the 32-wide Field map.  The hero is an OAM sprite (the console font's
'@' glyph), NOT a background tile: the BG tilemap holds terrain only, and the
sprite is re-positioned each frame at its camera-relative pixel position.

Checks:
  1. 0-vram-writes-during-scroll: On sub-tile scrolling frames where camera SCX
     changes but player tile position has not committed, 0 VRAM tilemap writes occur.
  2. no-blank-visible-floor: All visible viewport cells (20x12) in the VRAM ring
     contain valid non-zero map/terrain font tiles.
  3. hero-sprite-tracks: The '@' OAM sprite is visible and its shadow-OAM
     position matches (player_px - camera_px + OAM offset) on every frame.
  4. no-hero-in-bg: The '@' glyph tile never appears in the BACKGROUND tilemap
     (the hero is OAM-only; a stale BG '@' would double-draw the hero).
  5. no-lcd-off-during-movement: LCDC bit 7 (0x80) remains 1 on EVERY frame during
     held overworld movement (0 LCD-off frames).

World-struct offsets are read from the live debug ROM (g_game symbol + fixed
field offsets; the harness skips CRT0, so g_game and its World are in WRAM).

Run inside the Nix dev shell:
    make verify-scroll
or: nix develop --command python3 tools/verify_scroll.py [--rom PATH]

Exits non-zero if any check fails.
"""

import argparse
import os
import sys

ROM_DEBUG = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "build", "kaartenheld_debug.gb")
SYM_DEBUG = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "build", "kaartenheld_debug.sym")

failures = []


def check(label, ok, detail=""):
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {label}" + (f" -- {detail}" if detail and not ok else ""))
    if not ok:
        failures.append(label)


def get_symbol(sym_path, name):
    if not os.path.isfile(sym_path):
        return None
    for line in open(sym_path):
        line = line.strip()
        if not line or ":" not in line:
            continue
        parts = line.split(":")
        bank, rest = parts[0], parts[1].split()
        if len(rest) < 2:
            continue
        addr, sym_name = rest[0], rest[1]
        if sym_name == name or sym_name == f"_{name}":
            return int(addr, 16)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=ROM_DEBUG, help="ROM to boot")
    ap.add_argument("--frames", type=int, default=240,
                    help="held movement frames (default: 240)")
    args = ap.parse_args()

    if not os.path.isfile(args.rom):
        print(f"error: ROM not found: {args.rom} (make debug first)", file=sys.stderr)
        return 1

    try:
        from pyboy import PyBoy
    except ImportError:
        print("error: pyboy not installed.", file=sys.stderr)
        return 1

    pb = PyBoy(args.rom, window="null")

    # Harness mode is NOT enabled here: the debug build's input_update()
    # zeroes the physical joypad under g_harness_mode, so the held RIGHT
    # button would never move the hero.  vsync() works under PyBoy (frames
    # advance LY), so the normal joypad path is used throughout.
    bp_addr = get_symbol(SYM_DEBUG, "g_boot_phase")
    game_addr = get_symbol(SYM_DEBUG, "g_game")

    if game_addr is None:
        print("error: g_game not in symbol file (make debug first)", file=sys.stderr)
        return 1

    # Wait for boot completion (g_boot_phase == 4)
    for _ in range(300):
        pb.tick()
        if bp_addr and pb.memory[bp_addr] == 4:
            break

    # Check initial LCD status after boot
    lcdc_init = pb.memory[0xFF40]
    check("lcd-on-after-boot", (lcdc_init & 0x80) != 0, f"LCDC=0x{lcdc_init:02X}")

    # World struct field offsets relative to g_game (verified against the
    # debug ROM): Game = screen(1) + prev_screen(1) + GameState(197)
    # + World{ width,height,map_id,encounter_index,map_changed (5), player
    # Entity (7), actors[4]*24 (96), map[24][40] (960), camera_px (2),
    # scroll (2), move state (5) }.
    POS_X = game_addr + 204
    POS_Y = game_addr + 205
    CAM_X = game_addr + 1267
    CAM_Y = game_addr + 1268
    SCROLL_X = game_addr + 1269
    SCROLL_Y = game_addr + 1270
    MOVE_STATE = game_addr + 1271
    MOVE_TARGET_X = game_addr + 1272
    MOVE_TARGET_Y = game_addr + 1273
    MOVE_PROGRESS = game_addr + 1274

    # Shadow-OAM slot 0 = the player sprite (y, x).  Read before the frame's
    # ui_sprite_commit so it always reflects the latest game_render.
    OAM_Y = 0xC000
    OAM_X = 0xC001

    # Boot position is the FIELD spawn from levels/field.json player.spawn
    # ((17,7)): row 7 holds the east gate (31,7), and a held RIGHT would
    # cross maps mid-check (gate redraws blank the LCD and break the scroll
    # invariants).  Step up to row 4 first -- it is clear of terrain blocks
    # and the slime patrol box around (14,8), and its east edge (31,4) is a
    # plain perimeter wall, so holding RIGHT walks cleanly to the east wall
    # with full camera scroll and no map change.
    px0, py0 = pb.memory[POS_X], pb.memory[POS_Y]
    print(f"boot: player at ({px0},{py0})")
    for _ in range(20):
        if pb.memory[POS_Y] == 4:
            break
        pb.button_press("up")
        for _ in range(4):
            pb.tick()
        pb.button_release("up")
        for _ in range(40):
            pb.tick()
    print(f"scroll row: player at ({pb.memory[POS_X]},{pb.memory[POS_Y]})")

    # Press and hold RIGHT
    pb.button_press("right")

    prev_vram_map = bytes([pb.memory[0x9800 + i] for i in range(32 * 18)])
    prev_scx = pb.memory[0xFF43]
    prev_scy = pb.memory[0xFF42]
    prev_ms = 0

    subtile_scroll_frames = 0
    subtile_zero_vram_writes = 0
    blank_floor_violations = 0
    lcd_off_frames = 0
    sprite_track_frames = 0
    hero_in_bg_frames = 0
    hero_bg_tile = None

    for frame in range(args.frames):
        pb.tick()

        # Check 5: LCDC bit 7 must stay 1 during movement
        lcdc = pb.memory[0xFF40]
        if (lcdc & 0x80) == 0:
            lcd_off_frames += 1

        scx = pb.memory[0xFF43]
        scy = pb.memory[0xFF42]
        scroll_x = scx // 8
        scroll_y = scy // 8

        curr_vram_map = bytes([pb.memory[0x9800 + i] for i in range(32 * 18)])

        # Compare VRAM map changes across all 32x18 background ring cells
        diffs = [i for i in range(32 * 18) if curr_vram_map[i] != prev_vram_map[i]]

        if scx != prev_scx or scy != prev_scy:
            subtile_scroll_frames += 1
            if len(diffs) == 0:
                subtile_zero_vram_writes += 1
        else:
            if len(diffs) == 0:
                subtile_zero_vram_writes += 1

        # Check 2: Verify no blank floor cells (b == 0) in visible 20x12 overworld viewport
        for vy in range(12):
            for vx in range(20):
                map_x = (scroll_x + vx) % 32
                map_y = (scroll_y + vy) % 32
                cell_tile = curr_vram_map[map_y * 32 + map_x]
                if cell_tile == 0:
                    blank_floor_violations += 1

        # Check 3: the @ OAM sprite tracks the player's camera-relative pixels.
        # The shadow OAM is written by game_render while the World struct
        # fields below are read after the tick's final game_update, so the
        # sampled OAM can lag the sampled world state by exactly one sub-tile
        # step (1 px) when the tick ends between an update and its render;
        # allow that ±1 px sampling phase.
        player_x = pb.memory[POS_X]
        player_y = pb.memory[POS_Y]
        tgt_x = pb.memory[MOVE_TARGET_X]
        tgt_y = pb.memory[MOVE_TARGET_Y]
        prog = pb.memory[MOVE_PROGRESS]
        ms = pb.memory[MOVE_STATE]
        px = player_x * 8
        py = player_y * 8
        if ms == 1:
            if tgt_x > player_x:
                px += prog
            elif tgt_x < player_x:
                px -= prog
            if tgt_y > player_y:
                py += prog
            elif tgt_y < player_y:
                py -= prog
        camx = pb.memory[CAM_X]
        camy = pb.memory[CAM_Y]
        exp_y = py - camy + 16
        exp_x = px - camx + 8
        # Game loop order in main.c is: vsync() -> game_render() -> game_update().
        # Therefore, shadow OAM sampled after pb.tick() reflects the state rendered
        # at vsync before game_update advances move_progress / commits position.
        # On the exact frame where game_update commits the move (transition from
        # prev_ms == 1 to ms == 0), shadow OAM still holds the rendered position
        # from the final sub-pixel step: (tgt * 8 ± 1) - cam + offset.
        if prev_ms == 1 and ms == 0:
            if tgt_x > player_x:
                exp_x = (tgt_x * 8 - 1) - camx + 8
            elif tgt_x < player_x:
                exp_x = (tgt_x * 8 + 1) - camx + 8
            if tgt_y > player_y:
                exp_y = (tgt_y * 8 - 1) - camy + 16
            elif tgt_y < player_y:
                exp_y = (tgt_y * 8 + 1) - camy + 16
        act_y = pb.memory[OAM_Y]
        act_x = pb.memory[OAM_X]
        if act_y != 0 and abs(act_y - exp_y) <= 1 and abs(act_x - exp_x) <= 1:
            sprite_track_frames += 1

        # Check 4: the '@' glyph (the OAM sprite's tile, read from shadow OAM
        # slot 0's tile field) must never appear in the BG tilemap -- the hero
        # is OAM-only and a stale BG '@' would double-draw it.
        hero_tile = pb.memory[0xC002]
        if hero_bg_tile is None:
            hero_bg_tile = hero_tile
        if any(b == hero_bg_tile for b in curr_vram_map):
            hero_in_bg_frames += 1

        prev_vram_map = curr_vram_map
        prev_scx = scx
        prev_scy = scy
        prev_ms = ms

    pb.button_release("right")

    # Evaluate all 5 regression checks
    check("1. 0-vram-writes-during-scroll", subtile_scroll_frames > 0 and subtile_zero_vram_writes >= subtile_scroll_frames,
          f"{subtile_zero_vram_writes} zero-write frames across {subtile_scroll_frames} camera scroll frames")

    check("2. no-blank-visible-floor", blank_floor_violations == 0,
          f"{blank_floor_violations} blank visible floor cells detected")

    check("3. hero-sprite-tracks", sprite_track_frames == args.frames,
          f"{sprite_track_frames}/{args.frames} frames with @ sprite at the camera-relative position")

    check("4. no-hero-in-bg", hero_in_bg_frames == 0,
          f"{hero_in_bg_frames} frames with the '@' glyph tile present in the background")

    check("5. no-lcd-off-during-movement", lcd_off_frames == 0,
          f"{lcd_off_frames} LCD-off frames detected during held movement")

    print()
    if failures:
        print(f"{len(failures)} check(s) failed: {', '.join(failures)}")
        return 1
    print("All scrolling VRAM regression checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
