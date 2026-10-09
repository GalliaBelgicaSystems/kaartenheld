#!/usr/bin/env python3
"""Compile a .uge tracker file to C via uge2source and patch empty arrays for SDCC."""
import errno
import os
import shutil
import sys
import subprocess
import re

if len(sys.argv) < 5:
    print("Usage: compile_music.py <uge_file> <bank> <symbol> <out_c>")
    sys.exit(1)

uge_file, bank, symbol, out_c = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]


def resolve_tool(name):
    """Resolve a native toolchain binary with a diagnosable failure.

    A bare subprocess.run() turns a broken toolchain into a cryptic
    OSError (e.g. ENOEXEC for an unexecutable file shadowing the real
    binary). Fail here instead, naming the resolved path (or PATH) and
    the fix (`nix develop` per AGENTS.md section 1)."""
    path = shutil.which(name)
    if path is None:
        print(f"compile_music.py: error: '{name}' not found on PATH.",
              file=sys.stderr)
        print("The ROM toolchain (uge2source, rgbasm-huge, GBDK) is provided"
              " by the Nix flake: run builds inside `nix develop`.",
              file=sys.stderr)
        sys.exit(2)
    if not os.access(path, os.X_OK):
        print(f"compile_music.py: error: '{name}' at {path} is not executable.",
              file=sys.stderr)
        sys.exit(2)
    return path


try:
    uge2source = resolve_tool("uge2source")
    subprocess.run([uge2source, uge_file, "-b", str(bank), symbol, out_c],
                   check=True)
except subprocess.CalledProcessError as e:
    print(f"compile_music.py: error: uge2source failed (exit {e.returncode})"
          f" on '{uge_file}'. The tracker file may be corrupt or written by"
          f" an incompatible hUGETracker version.", file=sys.stderr)
    sys.exit(4)
except OSError as e:
    print(f"compile_music.py: error: cannot execute '{e.filename or 'uge2source'}':"
          f" {e.strerror} (errno {e.errno}).", file=sys.stderr)
    print("The toolchain binary failed to launch; either PATH resolves to a"
          " broken file shadowing the Nix-provided one, or the Nix package"
          " itself is damaged. Check `command -v uge2source` and re-enter"
          " the environment with `nix develop`.", file=sys.stderr)
    sys.exit(3)

with open(out_c, "r", encoding="utf-8") as f:
    src = f.read()

# SDCC error 286: empty initializer on array of unknown size
src = re.sub(r"static const hUGENoiseInstr_t noise_instruments\[\] = \{\s*\};",
              "static const hUGENoiseInstr_t noise_instruments[1] = {{0, 0, 0, 0, 0}};", src)
src = re.sub(r"static const hUGEDutyInstr_t duty_instruments\[\] = \{\s*\};",
              "static const hUGEDutyInstr_t duty_instruments[1] = {{0, 0, 0, 0, 0}};", src)
src = re.sub(r"static const hUGEWaveInstr_t wave_instruments\[\] = \{\s*\};",
              "static const hUGEWaveInstr_t wave_instruments[1] = {{0, 0, 0, 0, 0}};", src)
# A song with no wave instruments emits an empty `waves[]` table; SDCC
# rejects the empty initializer (error 286).  Give it one zero wave.
src = re.sub(r"static const unsigned char waves\[\] = \{\s*\};",
              "static const unsigned char waves[1] = {0};", src)

# Deduplicate byte-identical pattern arrays.  uge2source emits one C array
# per tracker pattern slot, so a repeated section (e.g. Forest.uge P2/P6)
# lands in ROM twice.  Merging identical arrays is playback-identical: the
# driver reads the same byte stream through the rewritten order references.
# Bank 6 (driver + songs) is full, so every byte counts.  Runs on every
# song; songs without duplicates come out byte-identical.  If a future
# uge2source changes the P-name scheme the pass finds nothing and no-ops
# (a resulting bank overflow then fails loudly in make memmap).
def dedup_patterns(src):
    pat_re = re.compile(r"static const unsigned char (P\d+)\[\] = \{(.*?)\};", re.S)
    defs = [(m.group(1), m.group(2)) for m in pat_re.finditer(src)]
    if not defs:
        return src
    canon = {}
    seen = {}
    canon_sizes = {}
    for name, body in defs:
        nums = re.findall(r"\d+", body)
        key = ",".join(nums)
        if key in seen:
            canon[name] = seen[key]
            canon_sizes[name] = len(nums)
        else:
            seen[key] = name
    if not canon:
        return src
    for old in canon:
        src = re.sub(r"static const unsigned char " + old + r"\[\] = \{.*?\};\s*",
                      "", src, count=1, flags=re.S)
    for old, new in canon.items():
        src = re.sub(r"(?<![A-Za-z0-9_])" + old + r"(?![A-Za-z0-9_])", new, src)
    # Verify: every P-name referenced from the order tables must still be
    # defined (guards against a regex/toolchain mismatch corrupting them).
    # itDutySxx/itNoiseSxx tables share the P-prefix namespace, so only
    # order-array references are checked.
    defined = set(m.group(1) for m in pat_re.finditer(src))
    order_refs = set()
    for m in re.finditer(r"order\d+\[\] = \{(.*?)\};", src, re.S):
        order_refs.update(re.findall(r"(?<![A-Za-z0-9_])P\d+(?![A-Za-z0-9_])", m.group(1)))
    missing = order_refs - defined
    if missing:
        print(f"compile_music.py: error: dedup left dangling references: {sorted(missing)}",
              file=sys.stderr)
        sys.exit(4)
    saved = sum(canon_sizes.values())
    print(f"compile_music.py: dedup merged {sorted(canon)} saving ~{saved} pattern bytes",
          file=sys.stderr)
    return src

src = dedup_patterns(src)

with open(out_c, "w", encoding="utf-8") as f:
    f.write(src)
