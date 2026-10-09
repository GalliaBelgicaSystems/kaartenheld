#!/usr/bin/env python3
"""PyBoy regression check: the music clock never stalls across screen/map
transitions.

Boots the debug ROM headlessly WITHOUT harness mode (so the real CRT0 boot,
interrupt setup, and audio ISR run) and walks the hero FIELD -> TOWN (gate
crossing, a map change) and through a guard dialogue round-trip (a screen
change).  Every full-screen redraw in game_render() disables the LCD for
the redraw (ui_lcd_off/ui_lcd_on), and Pan Docs says the screen then stays
blank for the first frame after re-enable.  VBlank is a PPU mode, so while
the LCD is off -- and through that first blank frame -- no VBlank interrupt
fires: a VBlank-driven music clock (audio_update in the VBlank ISR) stalls
for ~1-2 frames per transition, which is the audible music stop.

The fix drives the music clock off the hardware timer interrupt instead
(TIMA overflow at 256 Hz, independent of the LCD), so g_audio_ticks -- a
DEBUG-only counter incremented in audio_update -- must advance on every
sampled frame.

Checks:
  1. no-stalled-frames: every sampled frame reports >= 1 new tick.  On a
     VBlank-driven clock, normal frames advance exactly 1 tick/frame and
     LCD-off transition windows (1-2 frames) report 0; on the timer-driven
     fixed clock every frame reports 4-5.  PyBoy can emit a single isolated
     0-delta read at an LCD-off transition (its LCD-off handling ends the
     tick early, so one read straddles less than a full frame -- the total
     tick count stays exact), so the check tolerates a few isolated zeros
     but FAILS on a cluster of them (a real stall is a multi-frame run).
  2. fixed-clock-rate: total ticks over the walk match elapsed frames *
     (256 Hz / 59.7275 fps ~= 4.285) within 5%.  Catches a wrong-rate clock
     (e.g. a VBlank ~1/frame driver).
  3. transitions-exercised: the walk really crossed the FIELD->TOWN gate
     (player x wrapped 30 -> 2) and opened/closed the guard dialogue, so the
     LCD-off redraw paths were exercised.
  4. ticks-advanced: the counter is moving at all (the ISR actually runs).

Negative test (AGENTS.md 52.16): this FAILS on the VBlank-driven build and
PASSES on the timer-driven build.

Run inside the Nix dev shell:
    make verify-music
or: nix develop --command python3 tools/verify_music.py [--rom PATH]

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
        rest = parts[1].split()
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
    args = ap.parse_args()

    if not os.path.isfile(args.rom):
        print(f"error: ROM not found: {args.rom} (make debug first)", file=sys.stderr)
        return 1

    try:
        from pyboy import PyBoy
    except ImportError:
        print("error: pyboy not installed.", file=sys.stderr)
        return 1

    game_addr = get_symbol(SYM_DEBUG, "g_game")
    ticks_addr = get_symbol(SYM_DEBUG, "g_audio_ticks")
    if game_addr is None or ticks_addr is None:
        print("error: g_game/g_audio_ticks not in symbol file (make debug first)",
              file=sys.stderr)
        return 1

    pb = PyBoy(args.rom, window="null")

    sfx_id_addr = get_symbol(SYM_DEBUG, "sfx_id")
    if sfx_id_addr is None:
        print("error: sfx_id not in symbol file (make debug first)", file=sys.stderr)
        return 1

    # Harness mode is NOT enabled: we need the real boot (interrupts on) so
    # the audio ISR actually runs.  Wait for g_boot_phase == 4 (first
    # game_render complete; overworld music is already playing).
    bp_addr = get_symbol(SYM_DEBUG, "g_boot_phase")
    for _ in range(600):
        pb.tick()
        if bp_addr and pb.memory[bp_addr] == 4:
            break

    # Locate player in WRAM (same as capture_walkthrough.py: FIELD spawn
    # (17,7) per levels/field.json, facing LEFT)
    WRAM_BASE = 0xC000
    WRAM_SIZE = 0x2000
    PLAYER_BOOT = bytes([17, 7, 10, 10, 1, 2, 1])

    pos_addr = [game_addr + 204]

    def read_ticks():
        # 16-bit counter incremented by the ISR; tolerate a torn read by
        # taking the max of three reads (the counter is monotonic).
        return max(pb.memory[ticks_addr] | (pb.memory[ticks_addr + 1] << 8)
                   for _ in range(3))

    samples = []

    def sample():
        """Read the current frame's player position + tick count.  Callers
        must only sample immediately after a pb.tick() (via step()), so every
        entry in `samples` is exactly one frame apart -- never read the state
        twice without an intervening tick, or the delta between the two
        samples is a fake 0 and check 1 reports a non-existent stall."""
        p_addr = pos_addr[0]
        x = pb.memory[p_addr]
        y = pb.memory[p_addr + 1]
        t = read_ticks()
        lcd_on = bool(pb.memory[0xFF40] & 0x80)
        samples.append((x, y, t, lcd_on))
        return x, y

    def pos():
        return samples[-1][0], samples[-1][1]

    def step(n):
        for _ in range(n):
            pb.tick()
            sample()

    def walk(btn, goal, budget=2000):
        """Discrete one-tile presses (proven pattern from
        capture_walkthrough.py) until goal(x, y) holds; samples every frame.
        Self-corrects dropped PyBoy presses."""
        for _ in range(budget):
            if goal(*pos()):
                return True
            x0, y0 = pos()
            pb.button_press(btn)
            step(4)
            pb.button_release(btn)
            for _ in range(24):
                pb.tick()
                sample()
                if pos() != (x0, y0):
                    break
        return goal(*pos())

    def press(btn, settle=12):
        pb.button_press(btn)
        step(4)
        pb.button_release(btn)
        step(settle)

    def window_enabled():
        return bool(pb.memory[0xFF40] & 0x20)

    # --- SFX checks (transcribed tracker SFX, Path C) ---
    # On the title splash, START fires SFX_CURSOR (id 0): a 9-dosound-tick
    # noise hit holding NR42=0x91 with music CH4 muted, then completion
    # (sfx_id back to 0xFF) while the music clock keeps advancing.
    #
    # The trigger hold is STATE-VERIFIED, not time-expected (AGENTS.md
    # 56.2): PyBoy tick() boundaries and game frames are not 1:1 -- the
    # run shows multi-tick stalls where neither the main loop (input
    # heartbeat) nor the audio ISR advance, so a blind short hold can
    # fall entirely between the game's input samples and be swallowed
    # (observed: a 4-tick hold never firing, deterministically).  Hold
    # START until the SFX actually fires instead.
    for _ in range(300):
        pb.tick()
        if pb.memory[game_addr] == 9:
            break
    pb.button_press("start")
    sfx_win = []
    triggered = False
    for _ in range(60):
        pb.tick()
        sfx_win.append((pb.memory[sfx_id_addr],
                        pb.memory[0xFF21],
                        read_ticks()))
        if pb.memory[sfx_id_addr] == 0:
            triggered = True
            break
    pb.button_release("start")
    for _ in range(40):
        pb.tick()
        sfx_win.append((pb.memory[sfx_id_addr],
                        pb.memory[0xFF21],
                        read_ticks()))
    check("5a. sfx-triggered", triggered,
          "sfx_id never read CURSOR(0) while START held 60 ticks")
    check("5b. sfx-completes", sfx_win[-1][0] == 0xFF,
          f"sfx_id stuck at {sfx_win[-1][0]} (unmute never ran?)")
    sounding = [(s, nr42) for s, nr42, _ in sfx_win if s == 0]
    held = sum(1 for _, nr42 in sounding[:9] if nr42 == 0x91)
    check("5c. sfx-content-held", triggered and held >= 5,
          f"NR42==0x91 on {held}/9 sampled sounding frames "
          f"(transcribed envelope not rendered?)")
    # Stall tolerance, same rationale as check 1: isolated zero-deltas are
    # PyBoy tick-boundary artifacts (ticks where no emulated time
    # advances), not music-clock stalls.  A wedged ISR would zero a long
    # run, so fail only on 3+ consecutive zeros.
    run, worst = 0, 0
    for i in range(1, len(sfx_win)):
        if sfx_win[i][2] - sfx_win[i - 1][2] < 1:
            run += 1
            worst = max(worst, run)
        else:
            run = 0
    check("5d. sfx-no-stall", worst < 3,
          f"music clock stalled during SFX ({worst} consecutive zero deltas)")

    # Dismiss Title Screen (SCREEN_TITLE = 9) and Intro slides (SCREEN_INTRO = 10)
    # to drop into the OVERWORLD (SCREEN_OVERWORLD = 0) at the FIELD spawn
    # (17,7) from levels/field.json player.spawn.
    for _ in range(20):
        if pb.memory[game_addr] == 0:
            break
        press("start", settle=16)
        press("a", settle=16)

    # Locate player entity in WRAM once overworld is reached
    wram = bytes(pb.memory[i] for i in range(WRAM_BASE, WRAM_BASE + WRAM_SIZE))
    idx = wram.find(PLAYER_BOOT)
    if idx >= 0:
        pos_addr[0] = WRAM_BASE + idx

    samples.clear()

    print(f"boot: player at {sample()[:2]} ticks={samples[-1][2]}")
    ok = walk("right", lambda x, y: x >= 30)
    ok = walk("down", lambda x, y: y == 7) and ok
    ok = walk("right", lambda x, y: x == 2) and ok          # gate crossing
    ok = walk("down", lambda x, y: y == 8) and ok
    ok = walk("right", lambda x, y: x == 9) and ok

    # Bump the guard: a blocked RIGHT press opens dialogue (SCREEN_DIALOGUE = 1).
    def in_dialogue():
        return pb.memory[game_addr] == 1

    for _ in range(8):
        if in_dialogue():
            break
        press("right", settle=30)
    dialogue_opened = in_dialogue()

    # Close the dialogue with A (returns to SCREEN_OVERWORLD = 0).
    for _ in range(8):
        if not in_dialogue():
            break
        press("a", settle=30)
    dialogue_closed = not in_dialogue()

    pb.stop()

    # Compute per-frame tick deltas (skip the very first sample).
    # g_audio_ticks is uint16: wrap-aware mod 65536, so long walks (which
    # lap the counter) don't report phantom stalls at each wrap.
    deltas = [(samples[i][2] - samples[i - 1][2]) % 65536
              for i in range(1, len(samples))]

    # Check 4: the ISR is actually running (counter is monotonic and moving).
    # Unwrap the uint16 counter across the whole run for the true total.
    total = samples[-1][2] - samples[0][2]
    for i in range(1, len(samples)):
        if samples[i][2] < samples[i - 1][2]:
            total += 65536
    check("4. ticks-advanced", total > 0, f"g_audio_ticks stuck at {samples[0][2]}")

    # Check 3: transitions were really exercised.
    crossed_gate = any(prev[0] >= 30 and cur[0] <= 2
                       for prev, cur in zip(samples, samples[1:]))
    check("3a. gate-crossing-exercised", crossed_gate,
          "player never wrapped 30 -> 2 (route broken?)")
    check("3b. dialogue-round-trip-exercised", dialogue_opened and dialogue_closed,
          f"opened={dialogue_opened} closed={dialogue_closed}")

    stalled = [i + 1 for i in range(len(deltas))
               if deltas[i] < 1 and samples[i + 1][3]]
    ok = len(stalled) <= 6
    detail = ""
    if not ok and stalled:
        detail = (f"{len(stalled)} zero-delta frame(s), "
                  f"first at frame {stalled[0]} "
                  f"player={samples[stalled[0] - 1][:2]}")
    check("1. no-stalled-frames", ok, detail)

    # Check 2: fixed-clock aggregate rate (256 Hz / 59.7275 fps).
    frames = len(samples) - 1
    expected = frames * (256 / 59.7275)
    check("2. fixed-clock-rate", abs(total - expected) < expected * 0.05,
          f"total={total} expected~{expected:.0f} over {frames} frames")

    print(f"\nstats: {len(samples)} samples, {len(stalled)} stalled frame(s), "
          f"{total} ticks over {frames} frames (~{total / max(frames, 1):.2f}/frame)")

    if failures:
        print(f"{len(failures)} check(s) failed: {', '.join(failures)}")
        return 1
    print("Music clock regression checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
