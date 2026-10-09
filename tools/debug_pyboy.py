#!/usr/bin/env python3
import os
import sys

ROM_DEBUG = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                         "..", "build", "kaartenheld_debug.gb")
SYM = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "build", "kaartenheld_debug.sym")

def get_symbol(name):
    for line in open(SYM):
        if line.strip().endswith(name):
            parts = line.strip().split()
            addr_str = parts[0].split(":")[-1]
            return int(addr_str, 16)
    return None

from pyboy import PyBoy

pb = PyBoy(ROM_DEBUG, window="null")

# Set harness mode = 1 so vsync busy loop doesn't block PyBoy frame ticks
hm_addr = get_symbol("g_harness_mode")
bp_addr = get_symbol("g_boot_phase")
print(f"g_harness_mode addr = 0x{hm_addr:04X}, g_boot_phase addr = 0x{bp_addr:04X}")

if hm_addr:
    pb.memory[hm_addr] = 1

for frame in range(60):
    pb.tick()
    bp = pb.memory[bp_addr] if bp_addr else 0
    lcdc = pb.memory[0xFF40]
    scx = pb.memory[0xFF43]
    scy = pb.memory[0xFF42]
    if frame % 10 == 0 or bp != 4:
        print(f"Frame {frame:02d}: boot_phase={bp}, LCDC=0x{lcdc:02X}, SCX={scx}, SCY={scy}")

print("Boot complete. Frame 60 finished.")
