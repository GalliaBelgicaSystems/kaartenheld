#!/usr/bin/env python3
"""Generate minimal gb.lib + sm83.lib closures for linking the project ROMs.

sdldgb links entire archive files, so every unused library module (all five
fonts, the console/stdio chains, metasprites, decompressors) is placed into
the ROM. On MBC5 that inflates the non-bankable `_HOME` area past 0x8000,
where CPU addresses 0x8000-0x9FFF alias VRAM: boot-critical library code
(joypad, set_bkg_data, font_load) then lands in VRAM and the ROM hangs.

This script computes the transitive module closure seeded from the project
objects across BOTH archives and writes trimmed archives containing exactly
the needed modules.

Usage:
    GBDKDIR=/path/to/gbdk python3 tools/make_lite_libs.py <output_dir>
"""
import os
import shutil
import subprocess
import sys


def sdnm(obj):
    return subprocess.run(["sdnm", obj], capture_output=True, text=True).stdout.splitlines()


def parse(obj):
    """Return (defined, referenced) symbol sets for an object file."""
    defined = set()
    referenced = set()
    for line in sdnm(obj):
        parts = line.split(None, 2)
        if not parts:
            continue
        if parts[0] == "U":
            referenced.add(parts[1])
        elif len(parts) >= 3 and parts[1] in ("T", "D", "B", "C", "A", "R"):
            name = parts[2]
            if not name.startswith(".."):
                defined.add(name)
    return defined, referenced


# Symbols provided by the project's crt0.o, main.o, or sdldgb -g/-b flags.
RESOLVED_EXTERNALLY = {
    "_main", "_add_VBL", "_vsync", "_wait_vbl_done", "_display_off",
    "_console_mode", "__cpu", "__is_GBA", "_cpu", "__current_bank",
    "__shadow_OAM_base", "_shadow_OAM", "_oam_dma_init", "_timer_isr_init",
    ".mode", ".int", ".sys_time", ".reset", ".add_int", ".remove_int",
    ".add_VBL", ".remove_VBL", ".display_off", ".wait_vbl_done",
    ".vsync", ".call_hl", ".STACK", ".refresh_OAM",
    ".__margs", "__margs", "_stack_end", "___sdcc_enter_ix",
    "___sdcc_call_hl", "___sdcc_call_hl_ix", "___sdcc_banked_call",
    ".jpad",
}


def extract(archive, dest):
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    subprocess.run(["sdar", "x", archive], cwd=dest, check=True)


def scan_dir(d):
    objs = []
    for root, dirs, files in os.walk(d):
        dirs[:] = [dd for dd in dirs if not dd.startswith(".") and dd != "travel"]
        for fn in files:
            if fn.endswith(".o"):
                objs.append(os.path.join(root, fn))
    return objs


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    gbdkdir = os.environ.get("GBDKDIR")
    if not gbdkdir:
        print("GBDKDIR not set; cannot locate gb.lib / sm83.lib")
        sys.exit(1)
    outdir = sys.argv[1]
    gb_lib = os.path.join(gbdkdir, "lib", "gb", "gb.lib")
    sm83_lib = os.path.join(gbdkdir, "lib", "sm83", "sm83.lib")

    gbdir = os.path.join(outdir, ".libx_gb")
    sm83dir = os.path.join(outdir, ".libx_sm83")
    extract(gb_lib, gbdir)
    extract(sm83_lib, sm83dir)

    def modules_dir(name):
        return gbdir if os.path.exists(os.path.join(gbdir, name)) else sm83dir

    modules = [fn for fn in sorted(os.listdir(gbdir)) if fn.endswith(".o")]
    modules += [fn for fn in sorted(os.listdir(sm83dir)) if fn.endswith(".o")]

    defs_of, refs_of = {}, {}
    for m in modules:
        defined, referenced = parse(os.path.join(modules_dir(m), m))
        refs_of[m] = referenced
        for s in defined:
            defs_of.setdefault(s, m)

    project_refs = set()
    for d in ("build/debug", "build"):
        for o in scan_dir(d):
            _, referenced = parse(o)
            project_refs |= referenced

    pulled = set()
    for s in project_refs:
        if s in defs_of:
            pulled.add(defs_of[s])

    queue = list(pulled)
    while queue:
        m = queue.pop()
        for u in refs_of[m]:
            if u in defs_of and defs_of[u] not in pulled:
                pulled.add(defs_of[u])
                queue.append(defs_of[u])

    leftover = project_refs - set(defs_of) - RESOLVED_EXTERNALLY
    if leftover:
        print("WARNING: project refs not resolved by libs or externals:")
        for s in sorted(leftover):
            print("  " + s)

    gb_pulled = sorted(m for m in pulled if os.path.exists(os.path.join(gbdir, m)))
    sm83_pulled = sorted(m for m in pulled if os.path.exists(os.path.join(sm83dir, m)))

    print("gb.lib pulled=%d excluded=%d" % (len(gb_pulled), len(os.listdir(gbdir)) - len(gb_pulled)))
    print("sm83.lib pulled=%d excluded=%d" % (len(sm83_pulled), len(os.listdir(sm83dir)) - len(sm83_pulled)))

    for members, src, name in ((gb_pulled, gbdir, "gb_lite.lib"),
                               (sm83_pulled, sm83dir, "sm83_lite.lib")):
        out = os.path.join(outdir, name)
        if os.path.exists(out):
            os.remove(out)
        for m in members:
            subprocess.run(["sdar", "r", out, os.path.join(src, m)], check=True)
        print("wrote " + out)


if __name__ == "__main__":
    main()
