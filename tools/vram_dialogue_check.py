#!/usr/bin/env python3
"""Dialogue-box placement ground-truth check (PyBoy).

On this ASCII-only branch the dialogue box is drawn into the BACKGROUND
tilemap (0x9800) at screen rows 12-17 via the console font -- the
window-layer dialogue box (f77ec9b) is a gfx-branch change.  The dialogue
overlays the frozen, camera-scrolled overworld (SCX/SCY are kept during
SCREEN_DIALOGUE), so the box is written at ring rows (12 + scroll_y)..(17 +
scroll_y) and the PPU shows it at screen rows 12-17.  The box must be
present, clean (no map tiles inside it), and the HUD window must be
disabled while the dialogue is up.

This boots the real release ROM headlessly, walks to the town guard (camera
scrolled, SCY > 0), triggers the dialogue by bumping the guard, and decodes
the actual BACKGROUND tilemap:

  1. dialogue-bg-box: the box is present in the background at screen rows
     12-17 at the expected shape (+ border, speaker, line, [A] CONTINUE).
  2. dialogue-clean: the box contains only dialogue text -- no map tiles
     mixed in.
  3. dialogue-window-hidden: the HUD window is disabled during dialogue.
  4. dialogue-second-line: pressing A advances to the later dialogue line.

The walk is POSITION-based, not press-count based.  PyBoy's fixed button
delays are lossy (an 8-tick hold drops ~30% of presses), and hold-to-move
commits a fresh move on the same frame the previous one lands (a held
button overshoots by one tile on walkable ground).  Instead, each step is a
single 1-tick press edge followed by a wait for the tile commit; the player
position is read back from WRAM after every press, so a dropped press is
re-pressed and the route self-corrects.

Run inside the Nix dev shell:
    nix develop --command python3 tools/vram_dialogue_check.py [--rom PATH]

Exits non-zero if any check fails.
"""

import argparse
import os
import sys

ROM_DEFAULT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "build", "kaartenheld.gb")

# Player Entity layout: position{x,y}, hp, max_hp, active, facing, id.
# Located by scanning WRAM for the deterministic boot pattern (FIELD spawn
# (17,7) per levels/field.json player.spawn, hero 10/10, active, facing
# LEFT, id PLAYER) rather than hardcoding
# the g_game offset (which shifts with the _DATA layout on each build).
PLAYER_BOOT = bytes([17, 7, 10, 10, 1, 2, 1])
WRAM_BASE = 0xC000
WRAM_SIZE = 0x2000

failures = []


def check(label, ok, detail=""):
    status = "PASS" if ok else "FAIL"
    print(f"[{status}] {label}" + (f" -- {detail}" if detail and not ok else ""))
    if not ok:
        failures.append(label)


def raw_id(pb, x, y):
    return pb.memory[0x9800 + ((y & 31) * 32) + (x & 31)]


def background_row(pb, screen_y, scroll_y, ncols=20):
    """Decode a BACKGROUND row into a string."""
    row = ""
    for x in range(ncols):
        t = raw_id(pb, x, (screen_y + scroll_y) & 31)
        if 0 <= t < 96:
            row += chr(0x20 + t)
        elif 128 <= t <= 223:
            row += chr(0x20 + (t - 128))
        else:
            row += "#"
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rom", default=ROM_DEFAULT, help="ROM to boot (release build by default)")
    ap.add_argument("--frames", type=int, default=300,
                     help="frames to run before sampling (default: 300, ~5s past boot)")
    args = ap.parse_args()

    if not os.path.isfile(args.rom):
        print(f"error: ROM not found: {args.rom}", file=sys.stderr)
        print("Build it first (make release).", file=sys.stderr)
        return 1

    try:
        from pyboy import PyBoy
    except ImportError:
        print("error: pyboy not installed. `nix develop` (it is in the dev shell).", file=sys.stderr)
        return 1

    pb = PyBoy(args.rom, window="null")

    # Let the real boot run: game_init prepares the new-game state behind
    # the title screen, so the player entity pattern exists from here on.
    for _ in range(180):
        pb.tick()

    # Locate the player's position field in WRAM from the boot pattern.
    WRAM_BASE = 0xC000
    WRAM_SIZE = 0x2000
    wram = bytes(pb.memory[i] for i in range(WRAM_BASE, WRAM_BASE + WRAM_SIZE))
    idx = wram.find(PLAYER_BOOT)
    if idx < 0:
        print("error: could not locate the player entity in WRAM after boot",
              file=sys.stderr)
        return 1
    pos_addr = WRAM_BASE + idx

    def pos():
        return (pb.memory[pos_addr], pb.memory[pos_addr + 1])

    def walk(btn, is_goal, budget=2000):
        """Discrete one-tile presses until is_goal() holds.  Each press is a
        4-tick edge (short enough that the 8-frame move commits after the
        release, so exactly one tile) followed by a wait for the commit; a
        press that produced no movement (dropped by PyBoy) is retried, so
        the route converges regardless of host timing.  Returns whether the
        goal was reached within the frame budget."""
        for _ in range(budget):
            if is_goal():
                return True
            x0, y0 = pos()
            pb.button_press(btn)
            for _ in range(4):
                pb.tick()
            pb.button_release(btn)
            for _ in range(24):
                pb.tick()
                if pos() != (x0, y0):
                    break
        return is_goal()

    def press(btn, settle=12):
        """A 4-tick button press.  The game reads the physical joypad each
        frame, and PyBoy applies queued events at frame boundaries, so a
        one-tick press can miss the input_update window entirely (a stale
        previous edge is never re-seen); a 4-tick hold guarantees the edge
        lands inside a frame."""
        pb.button_press(btn)
        for _ in range(4):
            pb.tick()
        pb.button_release(btn)
        for _ in range(settle):
            pb.tick()

    def is_goal():
        return pb.memory[pos_addr] == 30

    def press_start():
        pb.button_press("start")
        for _ in range(4):
            pb.tick()
        pb.button_release("start")
        for _ in range(16):
            pb.tick()

    def wait_for_screen(screen_id, budget=300):
        for _ in range(budget):
            if pb.memory[game_addr] == screen_id:
                return True
            pb.tick()
        return False

    # Game state address (from symbol file: g_game at 0xC94C)
    game_addr = 0xC94C

    def press_a(settle=12):
        pb.button_press("a")
        for _ in range(4):
            pb.tick()
        pb.button_release("a")
        for _ in range(settle):
            pb.tick()

    def wait_for_screen(screen_id, budget=300):
        for _ in range(budget):
            if pb.memory[game_addr] == screen_id:
                return True
            pb.tick()
        return False

    # Dismiss Title Screen (SCREEN_TITLE = 9) and Intro slides (SCREEN_INTRO = 10)
    # to drop into the OVERWORLD (SCREEN_OVERWORLD = 0) at the FIELD spawn
    # (17,7) from levels/field.json player.spawn.
    # Title screen: press START to show menu, then START again to select NEW GAME -> intro slides
    # Intro slides: press A (or START) 3 times to advance through slides, then game_restart() drops to overworld
    for _ in range(20):
        if pb.memory[game_addr] == 0:
            break
        pb.button_press("start")
        for _ in range(4):
            pb.tick()
        pb.button_release("start")
        for _ in range(16):
            pb.tick()

    # Wait for intro screen (SCREEN_INTRO = 10)
    wait_for_screen(10, 60)

    # Advance through intro slides (3 slides, need A or START each time)
    for _ in range(3):
        wait_for_screen(10, 60)  # SCREEN_INTRO = 10
        pb.button_press("a")
        for _ in range(4):
            pb.tick()
        pb.button_release("a")
        for _ in range(16):
            pb.tick()

    # Wait for game_restart to drop into OVERWORLD (screen 0)
    wait_for_screen(0, 300)

    # Locate the player's position field in WRAM from the boot pattern.
    pos_addr = None
    wram = bytes(pb.memory[i] for i in range(WRAM_BASE, WRAM_BASE + WRAM_SIZE))
    idx = wram.find(PLAYER_BOOT)
    if idx < 0:
        print("error: could not locate the player entity in WRAM after boot",
              file=sys.stderr)
        return 1
    pos_addr = WRAM_BASE + idx

    def pos():
        return (pb.memory[pos_addr], pb.memory[pos_addr + 1])

    def walk(btn, is_goal, budget=2000):
        """Discrete one-tile presses until is_goal() holds.  Each press is a
        4-tick edge (short enough that the 8-frame move commits after the
        release, so exactly one tile) followed by a wait for the commit; a
        press that produced no movement (dropped by PyBoy) is retried, so
        the route converges regardless of host timing.  Returns whether the
        goal was reached within the frame budget."""
        for _ in range(budget):
            if is_goal():
                return True
            x0, y0 = pos()
            pb.button_press(btn)
            for _ in range(4):
                pb.tick()
            pb.button_release(btn)
            for _ in range(24):
                pb.tick()
                if pos() != (x0, y0):
                    break
        return is_goal()

    def press(btn, settle=12):
        """A 4-tick button press.  The game reads the physical joypad each
        frame, and PyBoy applies queued events at frame boundaries, so a
        one-tick press can miss the input_update window entirely (a stale
        previous edge is never re-seen); a 4-tick hold guarantees the edge
        lands inside a frame."""
        pb.button_press(btn)
        for _ in range(4):
            pb.tick()
        pb.button_release(btn)
        for _ in range(settle):
            pb.tick()

    x, y = pos()
    print(f"boot: player at ({x},{y}) (WRAM 0x{pos_addr:04X})")

    # FIELD spawn (17,7) -> east gate (31,7) -> TOWN (2,7) -> (2,8) ->
    # west of the guard at (9,8).  The camera scrolls
    # throughout (SCY > 0 in TOWN, y=8 keeps the ring scrolled vertically).
    ok = walk("right", lambda: pos()[0] == 2)
    ok = walk("down", lambda: pos()[1] == 8) and ok
    ok = walk("right", lambda: pos()[0] == 9) and ok

    x, y = pos()
    print(f"guard approach: player at ({x},{y})")
    if not ok:
        print("warning: walk did not reach the guard; sampling anyway")

    # Bump the guard at (10,8): a blocked RIGHT press engages the dialogue.
    press("right", settle=30)
    for _ in range(10):
        pb.tick()

    win = pb.tilemap_window
    bg = pb.tilemap_background

    # On this ASCII-only branch the dialogue box is drawn into the
    # BACKGROUND tilemap (console putchar) at screen rows 12-17, offset into
    # the 32x32 ring by the frozen camera: ring rows (12+scroll_y)..(17+scroll_y),
    # ring cols (scroll_x..scroll_x+19).  The window-layer dialogue box
    # (f77ec9b) is a gfx-branch change.  The box must still be present,
    # clean (no map tiles inside it), and the window must stay disabled
    # (HUD hidden during dialogue).  Read the scroll from the SCY/SCX
    # registers: informational scroll readout (no current map can scroll
    # vertically -- all maps are 18 tall = WORLD_VIEW_H, so world_update_scroll
    # clamps max_y to 0; the box math below still runs at the live offset).
    scroll_y = pb.memory[0xFF42] >> 3
    scroll_x = pb.memory[0xFF43] >> 3
    print(f"dialogue camera: scroll=({scroll_x},{scroll_y})")

    box = [background_row(pb, 12 + wy, scroll_y) for wy in range(6)]
    lcdc = pb.memory[0xFF40]
    win_enabled = bool(lcdc & 0x20)

    # ── 1. Dialogue box present in the BACKGROUND at the expected shape ──
    top_ok = box[0].startswith("+") and box[0].endswith("+")
    bottom_ok = box[5].startswith("+") and box[5].endswith("+")
    continue_ok = "[A] CONTINUE" in box[4]
    check("dialogue-bg-box", top_ok and bottom_ok and continue_ok,
          "expected the box (+ borders, [A] CONTINUE) in background rows "
          f"12-17 at the scrolled ring offset (scroll_y={scroll_y}), got: "
          + " / ".join(box))

    # ── 2. Box is clean: only font tiles (0..95 or 128..223) inside it ────────
    dirty = any((t >= 96 and not (128 <= t <= 223)) for wy in range(6)
                for t in (raw_id(pb, x, (12 + scroll_y + wy) & 31)
                          for x in range(20)))
    check("dialogue-clean", not dirty,
          f"map tiles leaked into the dialogue box (rows: {box[1:5]})")

    # ── 3. Window disabled during dialogue (HUD is overworld-only) ──────
    check("dialogue-window-hidden", not win_enabled,
          f"expected the HUD window disabled during dialogue, got LCDC=0x{lcdc:02X}")

    # Advance once and verify the later line too.
    press("a", settle=30)
    for _ in range(10):
        pb.tick()
    second_line = background_row(pb, 14, scroll_y)
    check("dialogue-second-line", "Watch for slimes." in second_line,
          "expected the second dialogue line, got: " + second_line)

    print()
    if failures:
        print(f"{len(failures)} check(s) failed: {', '.join(failures)}")
        return 1
    print("All dialogue-box VRAM checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
