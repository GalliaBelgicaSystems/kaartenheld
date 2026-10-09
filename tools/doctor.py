#!/usr/bin/env python3
"""doctor.py - verify the ROM toolchain actually executes.

Resolving a binary is not enough: a broken file shadowing the real one
fails at exec time with a cryptic OSError (e.g. ENOEXEC deep inside a
build rule). This probes every required native binary by RUNNING it, so a
broken toolchain fails here with a clear message instead of mid-build.

Usage:
    python3 tools/doctor.py
Exits 0 when every required tool runs, 1 otherwise. Emulators are
warn-only (needed only for `make run`).
"""

import shutil
import subprocess
import sys

# name -> (probe argv, purpose). Probe must be side-effect free and fast.
REQUIRED = [
    ("python3", ["python3", "--version"], "level compiler / tooling"),
    ("make", ["make", "--version"], "build orchestration"),
    ("lcc", ["lcc", "-v"], "GBDK-4 C toolchain"),
    ("uge2source", ["uge2source"], "hUGETracker song converter (usage text)"),
]

# Either rgbasm is fine (mirrors the Makefile RGBASM_HUGE fallback).
RGBASM_CANDIDATES = [
    ("rgbasm-huge", ["rgbasm-huge", "--help"]),
    ("rgbasm", ["rgbasm", "--help"]),
]

EMULATORS = ["sameboy", "mgba", "mgba-sdl", "mgba-qt", "pyboy"]


def probe(name, argv):
    path = shutil.which(name)
    if path is None:
        return False, f"'{name}' not found on PATH"
    if name == "uge2source":
        # No probe args: prints usage, exits 0. Must not touch the repo.
        argv = [path]
    else:
        argv = [path] + argv[1:]
    try:
        subprocess.run(argv, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=30)
    except OSError as e:
        return False, f"'{name}' at {path} failed to launch: {e.strerror} (errno {e.errno})"
    except subprocess.TimeoutExpired:
        return False, f"'{name}' at {path} hung on its probe"
    return True, path


def main():
    failures = []
    print("toolchain:")
    for name, argv, purpose in REQUIRED:
        ok, detail = probe(name, argv)
        print(f"  [{'OK ' if ok else 'FAIL'}] {name:12s} {detail}  ({purpose})")
        if not ok:
            failures.append(name)
    rgbasm_ok = False
    for name, argv in RGBASM_CANDIDATES:
        ok, detail = probe(name, argv)
        if ok:
            print(f"  [OK ] {name:12s} {detail}  (hUGEDriver assembler)")
            rgbasm_ok = True
            break
    if not rgbasm_ok:
        print("  [FAIL] rgbasm       neither 'rgbasm-huge' nor 'rgbasm' runs"
              "  (hUGEDriver assembler)")
        failures.append("rgbasm(huge)")
    found_emu = [e for e in EMULATORS if shutil.which(e)]
    if found_emu:
        print(f"  [OK ] emulator(s): {', '.join(found_emu)}")
    else:
        print("  [WARN] no emulator found (sameboy/mgba/pyboy) - `make run` needs one")
    if failures:
        print(f"\ndoctor: FAIL ({', '.join(failures)})."
              " The ROM toolchain comes from the Nix flake: run inside `nix develop`"
              " (AGENTS.md section 1). `make editor` must be started from a shell"
              " with that environment, or the editor server inherits a broken PATH.")
        return 1
    print("\ndoctor: all required tools execute.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
