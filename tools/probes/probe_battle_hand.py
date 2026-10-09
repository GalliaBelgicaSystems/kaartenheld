#!/usr/bin/env python3
"""Full-boot (CRT0) battle-hand regression probe.

The SameBoy harness jumps straight to main() and skips CRT0 (AGENTS.md
52.11), so it is structurally blind to real-boot-only manifestations of
the SDCC stale-stack-slot miscompile family.  This probe boots the ROM
through the real boot path (PyBoy), walks into the FIELD slime encounter,
and asserts that the battle HUD hand row shows the actually-dealt cards.

Usage:
    python3 tools/probes/probe_battle_hand.py [rom] [--max-frames N]

Exit codes: 0 = PASS, 1 = FAIL.
"""

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

ROM = "build/kaartenheld.gb"
MAX_WALK = 300
SETTLE_FRAMES = 120
HP_WATCH_FRAMES = 1500

# Game struct field offsets (validated against build .sym; g_game itself is
# resolved from the sym file so rebuilds that shift WRAM layout still work).
HAND_OFF = 1658   # Game.battle.hand[0]; Card stride 8: +0 type, +1 value
HP_OFF = 1292     # Game.battle.player.hp
SCR_OFF = 0       # Game.screen  (2 == BATTLE)
CARD_STRIDE = 8
HAND_SIZE = 5
CARD_TYPE_EMPTY = 255

TYPE_CODES = {0: "SW", 1: "SH", 2: "BO", 3: "HE", 4: "DA"}
PLAYER_BOOT_PATTERN = bytes([4, 4, 10, 10, 1, 1, 1])


def fail(msg):
    print("PROBE: FAIL")
    print(msg)
    sys.exit(1)


def resolve_g_game(rom_path):
    sym = rom_path.replace(".gb", ".sym")
    if not os.path.exists(sym):
        fail(f"sym file not found: {sym}\n(expected next to the ROM)")
    for line in open(sym):
        parts = line.split()
        if len(parts) == 2 and parts[1] == "_g_game":
            return int(parts[0].split(":")[-1], 16)
    fail(f"_g_game not found in {sym}")


def main():
    rom = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else ROM
    from pyboy import PyBoy

    g_game = resolve_g_game(rom)
    G_SCR = g_game + SCR_OFF
    G_HAND = g_game + HAND_OFF
    G_HP = g_game + HP_OFF

    pb = PyBoy(rom, window="null", cgb=False)
    pb.set_emulation_speed(0)

    # Boot (real CRT0 path) and locate the player entity via its
    # deterministic boot pattern.
    for _ in range(250):
        pb.tick()
    wram = bytes(pb.memory[i] for i in range(g_game, g_game + 0x2000))
    pos = g_game + wram.find(PLAYER_BOOT_PATTERN)
    if wram.find(PLAYER_BOOT_PATTERN) < 0:
        fail("player boot pattern not found in WRAM")

    def pos2():
        return (pb.memory[pos], pb.memory[pos + 1])

    def walk(btn, is_goal, budget=600):
        for _ in range(budget):
            if is_goal():
                return True
            x0, y0 = pos2()
            pb.button_press(btn)
            for _ in range(5):
                pb.tick()
            pb.button_release(btn)
            for _ in range(28):
                pb.tick()
                if pos2() != (x0, y0):
                    break
        return is_goal()

    if not walk("down", lambda: pos2()[1] == 8):
        fail(f"could not walk to y=8 (at {pos2()})")
    if not walk("right", lambda: pos2()[0] == 13):
        fail(f"could not walk to x=13 (at {pos2()})")

    # Walk right into the slime; battle entry is state-verified.
    pb.button_press("right")
    f = 0
    while pb.memory[G_SCR] != 2 and f < MAX_WALK:
        pb.tick()
        f += 1
    pb.button_release("right")
    if pb.memory[G_SCR] != 2:
        fail(f"battle never started (screen={pb.memory[G_SCR]} after {f} frames)")
    for _ in range(SETTLE_FRAMES):
        pb.tick()

    # Dealt hand (authoritative semantic state).
    hand = [(pb.memory[G_HAND + i * CARD_STRIDE],
             pb.memory[G_HAND + i * CARD_STRIDE + 1]) for i in range(HAND_SIZE)]
    expected = "".join(
        "   " if t == CARD_TYPE_EMPTY else
        (TYPE_CODES.get(t, "??") + str(v) + " ")
        for (t, v) in hand
    )

    # Rendered row 14 (BG tilemap; font tile = ascii - 32).
    lcdc = pb.memory[0xFF40]
    map_area = 0x9C00 if (lcdc & 0x08) else 0x9800
    scx, scy = pb.memory[0xFF43], pb.memory[0xFF42]
    ty, tx = scy // 8, scx // 8

    def row(r):
        out = []
        for c in range(20):
            t = pb.memory[map_area + ((ty + r) % 32) * 32 + ((tx + c) % 32)]
            out.append(chr(t + 32) if 0 < t + 32 < 127 else ".")
        return "".join(out)

    actual = row(14)

    print(f"ROM: {rom}")
    print(f"HAND (semantic): {hand}")
    print(f"EXPECTED row14: '{expected}'")
    print(f"ACTUAL   row14: '{actual}'")

    hp0 = pb.memory[G_HP]
    hp_ok = True
    for f2 in range(HP_WATCH_FRAMES):
        pb.tick()
        hp = pb.memory[G_HP]
        if hp != hp0:
            print(f"HP CHANGED at watch frame {f2}: {hp0} -> {hp}")
            hp_ok = False
            break
    print(f"HP after {HP_WATCH_FRAMES} frames: {pb.memory[G_HP]} (start {hp0})")

    problems = []
    for i, (t, v) in enumerate(hand):
        if t == CARD_TYPE_EMPTY:
            continue
        want = TYPE_CODES.get(t, "??") + str(v)
        got = actual[i * 4:i * 4 + 3].strip() if i * 4 + 3 <= len(actual) else ""
        if want not in actual[i * 4:i * 4 + 4]:
            problems.append(f"slot {i}: expected '{want}' at col {i*4}, got '{actual[i*4:i*4+4]}'")
    if not hp_ok:
        problems.append("player HP changed during no-input battle watch")

    pb.stop()
    if problems:
        print("PROBE: FAIL")
        for p in problems:
            print(f"  - {p}")
        sys.exit(1)
    print("PROBE: PASS")


if __name__ == "__main__":
    main()
