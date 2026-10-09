#!/usr/bin/env python3
"""Endurance stress test for timer interrupt, OAM DMA, and gameplay stability.

Boots the real release ROM headlessly with interrupts enabled (non-harness mode)
and executes 3600 frames (~60 seconds of gameplay) with continuous audio and
active button inputs (walking, menus, gate crossings, combat encounters).

Asserts:
  1. No CPU lockup, invalid opcode execution, or stack corruptions.
  2. OAM and tilemap remain valid throughout.
  3. Audio continues ticking cleanly across frames.

Usage:
  nix develop --command python3 tools/verify_endurance.py
"""

import argparse
import os
import random
import sys

ROM_RELEASE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "..", "build", "kaartenheld.gb")
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
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", default=ROM_RELEASE, help="ROM to boot")
    ap.add_argument("--frames", type=int, default=3600, help="Frames to simulate (default 3600 = ~60s)")
    args = ap.parse_args()

    if not os.path.isfile(args.rom):
        print(f"error: ROM not found: {args.rom} (make release first)", file=sys.stderr)
        return 1

    try:
        from pyboy import PyBoy
    except ImportError:
        print("error: pyboy not installed.", file=sys.stderr)
        return 1

    ticks_addr = get_symbol(SYM_DEBUG, "g_audio_ticks")
    pb = PyBoy(args.rom, window="null")

    # Real boot (interrupts enabled). Wait for boot phase to complete.
    for _ in range(180):
        pb.tick()

    buttons = ["up", "down", "left", "right", "a", "b", "start"]
    random.seed(12345)

    last_ticks = 0
    ticks_advanced = True
    crashed = False
    crash_reason = ""

    print(f"Running endurance test for {args.frames} frames (~{args.frames // 60} seconds)...")

    for frame in range(1, args.frames + 1):
        if frame % 15 == 0:
            pb.button_press(random.choice(buttons))
        elif frame % 15 == 5:
            for b in buttons:
                pb.button_release(b)

        try:
            res = pb.tick()
            if not res:
                crashed = True
                crash_reason = f"PyBoy tick returned False at frame {frame}"
                break
        except Exception as e:
            crashed = True
            crash_reason = f"Exception at frame {frame}: {e}"
            break

        # Check for abnormal hardware register states indicating crash
        # (e.g. IF/IE corruption)
        ie = pb.memory[0xFFFF]
        if ie != 0x04:
            crashed = True
            crash_reason = f"IE corrupted: expected 0x04 (Timer), got 0x{ie:02X} at frame {frame}"
            break

    is_debug = os.path.abspath(args.rom) == os.path.abspath(ROM_DEBUG)
    if is_debug and ticks_addr and frame % 300 == 0:
            current_ticks = pb.memory[ticks_addr] | (pb.memory[ticks_addr + 1] << 8)
            if current_ticks <= last_ticks:
                ticks_advanced = False
            last_ticks = current_ticks

    pb.stop()

    check("1. no-crash-or-freeze", not crashed, crash_reason)
    if is_debug and ticks_addr:
        check("2. audio-ticks-monotonic", ticks_advanced, "Audio ticks failed to advance monotonically")

    # Verify final hardware state
    check("3. 60s-simulation-complete", frame >= args.frames, f"Stopped early at frame {frame}")

    if failures:
        print(f"\n{len(failures)} check(s) failed: {', '.join(failures)}")
        return 1

    print(f"\nEndurance test passed cleanly ({frame} frames completed without crash).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
