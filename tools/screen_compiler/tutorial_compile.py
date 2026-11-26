#!/usr/bin/env python3
"""
tutorial_compile.py - Tutorial slides JSON to C for the Game Boy RPG.

screens/tutorial.json -> src/screens/tutorial_text_generated.h (slide
text arrays + pointer table + TUTORIAL_SLIDE_COUNT).  The banked render
body stays hand-written in src/screens/tutorial_content.c, which includes
the generated header: generated initializers must keep the exact
static-const-array shape SDCC compiles correctly (see the
s_card_desc_blob precedent — flat multi-literal initializers miscompile).

Slide geometry: at most 4 rows per slide, each centered by the
compiler in an 18-wide field (cols 1..18, col 0/19 whitespace) and
drawn at rows 7..10 by the banked renderer (region rows 2..15, the
[LR] NAV [B] EXIT footer stays at row 16).  Short slides pad with "".
Lines may carry button-icon markup ({A} {B} {SELECT} {LR} {UD});
tags expand to icon control bytes the renderer stamps as tiles.

Usage:
    python3 tools/screen_compiler/tutorial_compile.py --all -o src/screens/
    python3 tools/screen_compiler/tutorial_compile.py --all --check
"""
import argparse
import json
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parent.parent

TUTORIAL_JSON = "tutorial.json"
MAX_ROWS = 4
STAGE_COLS = 20     # s_tut_N[][20] incl NUL
RENDER_WIDTH = 18   # draw width (clipped at render)

# Inline button-icon markup for tutorial lines: {A} {B} {SELECT} {LR}
# {UD}.  Tags expand to raw control bytes (0x80..0x86) that the banked
# renderer maps to the streamed UI_TILE_BUTTON_* tiles (same tiles the
# footer already stamps, so no VRAM/streaming change).  {SELECT} is the
# 3-cell-wide button (SELECT_1/2/3, like START_1/2/3); every other tag
# is one cell.  Cell counts (not raw chars) drive the 18-wide centering.
ICON_TAGS = {
    "A": (0x80,),
    "B": (0x81,),
    "SELECT": (0x82, 0x83, 0x84),
    "LR": (0x85,),
    "UD": (0x86,),
}


def expand_tags(line, where):
    """Expand {TAG} markup to control bytes.  Returns (cells, raw_bytes)
    or exits loudly on an unknown tag (a typo must never ship as text)."""
    cells = 0
    out = bytearray()
    i = 0
    while i < len(line):
        if line[i] == "{":
            end = line.find("}", i)
            tag = line[i + 1:end] if end != -1 else None
            if tag not in ICON_TAGS:
                print(f"ERROR: {where}: unknown icon tag "
                      f"'{line[i:end + 1] if end != -1 else line[i:]}' "
                      f"(want one of {sorted(ICON_TAGS)})", file=sys.stderr)
                sys.exit(1)
            for b in ICON_TAGS[tag]:
                out.append(b)
                cells += 1
            i = end + 1
        else:
            out.append(ord(line[i]))
            cells += 1
            i += 1
    return cells, bytes(out)


def load_tutorial():
    path = REPO_ROOT / "screens" / TUTORIAL_JSON
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        print(f"ERROR: cannot read {path}: {exc}", file=sys.stderr)
        sys.exit(1)
    slides = data.get("slides")
    if not isinstance(slides, list) or not slides:
        print(f"ERROR: {path}: need at least 1 slide (the title menu "
              f"links here unconditionally)", file=sys.stderr)
        sys.exit(1)
    errors, warnings = [], []
    rows = []
    for i, slide in enumerate(slides):
        lines = (slide or {}).get("lines", [])
        if not isinstance(lines, list) or not (1 <= len(lines) <= MAX_ROWS):
            errors.append(
                f"slide {i}: need 1..{MAX_ROWS} lines, have "
                f"{len(lines) if isinstance(lines, list) else '?'}")
            continue
        exp = []
        for j, line in enumerate(lines):
            if not isinstance(line, str):
                errors.append(f"slide {i} line {j} must be a string")
                continue
            cells, raw = expand_tags(line, f"slide {i} line {j}")
            if len(raw) >= STAGE_COLS:
                errors.append(
                    f"slide {i} line {j} is {len(raw)} bytes, exceeds "
                    f"staging ({STAGE_COLS - 1})")
            elif cells > RENDER_WIDTH:
                warnings.append(
                    f"slide {i} line {j} is {cells} cells, wider than "
                    f"the {RENDER_WIDTH}-col box (clipped at render)")
            exp.append((cells, raw))
        exp += [(0, b"")] * (MAX_ROWS - len(exp))
        rows.append(exp)
    for w in warnings:
        print(f"WARNING: {w}")
    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        sys.exit(1)
    return rows


def c_escape_raw(raw):
    """Escape expanded line bytes for a C string literal: printable
    ASCII (and space) as-is, backslash/quote escaped, control bytes
    (>= 0x80 icon tags) as 3-digit octal (unambiguous next to any
    following char, unlike \\x escapes which eat hex letters)."""
    out = []
    for b in raw:
        if b == 0x5C:
            out.append("\\\\")
        elif b == 0x22:
            out.append('\\"')
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append("\\%03o" % b)
    return "".join(out)


def center_pad(cells, raw):
    """Center expanded `raw` bytes in the 18-wide text field
    (cols 1..18): pad both sides with spaces so col 0 and col 19 stay
    blank.  Lines over 18 cells are rejected by load_tutorial, so this
    only ever pads."""
    pad = RENDER_WIDTH - cells
    left = pad // 2
    return " " * left + c_escape_raw(raw) + " " * (pad - left)


def build_text_output(rows):
    """Generate src/screens/tutorial_text_generated.h: slide arrays +
    pointer table.  Array shapes mirror the legacy hand-written
    tutorial_content.c exactly.  Included ONCE (by tutorial_content.c):
    static data must not duplicate per translation unit.  The slide
    count travels separately (see build_count_output)."""
    lines = []
    lines.append("/* Generated by tools/screen_compiler/tutorial_compile.py --all.")
    lines.append(" * Do not edit directly -- edit screens/tutorial.json and re-run. */")
    lines.append("#ifndef TUTORIAL_TEXT_GENERATED_H")
    lines.append("#define TUTORIAL_TEXT_GENERATED_H")
    lines.append("")
    for i, slide in enumerate(rows):
        lines.append(f"static const char s_tut_{i}[{MAX_ROWS}][{STAGE_COLS}] = {{")
        for j, (cells, raw) in enumerate(slide):
            comma = "," if j < len(slide) - 1 else ""
            lines.append(f'    "{center_pad(cells, raw)}"{comma}')
        lines.append("};")
        lines.append("")
    lines.append(f"static const char (* const s_tut_slides[{len(rows)}])[{STAGE_COLS}] = {{")
    for i in range(len(rows)):
        comma = "," if i < len(rows) - 1 else ""
        lines.append(f"    s_tut_{i}{comma}")
    lines.append("};")
    lines.append("")
    lines.append("#endif /* TUTORIAL_TEXT_GENERATED_H */")
    lines.append("")
    return "\n".join(lines)


def build_count_output(rows):
    """Generate src/screens/tutorial_count_generated.h: just the slide
    count define (included by screen.h — defines duplicate harmlessly,
    unlike the arrays above)."""
    lines = []
    lines.append("/* Generated by tools/screen_compiler/tutorial_compile.py --all.")
    lines.append(" * Do not edit directly -- edit screens/tutorial.json and re-run. */")
    lines.append("#ifndef TUTORIAL_COUNT_GENERATED_H")
    lines.append("#define TUTORIAL_COUNT_GENERATED_H")
    lines.append("")
    lines.append(f"#define TUTORIAL_SLIDE_COUNT {len(rows)}")
    lines.append("")
    lines.append("#endif /* TUTORIAL_COUNT_GENERATED_H */")
    lines.append("")
    return "\n".join(lines)


def main(args=None):
    parser = argparse.ArgumentParser(description="Compile tutorial JSON to C.")
    parser.add_argument("--all", action="store_true",
                        help="Compile screens/tutorial.json")
    parser.add_argument("-o", "--output", default="src/screens/",
                        help="Output directory for tutorial_text_generated.h")
    parser.add_argument("--check", action="store_true",
                        help="Do not write; exit nonzero if fresh output "
                             "differs from the file")
    args = parser.parse_args(args)

    outdir = Path(args.output)
    if str(outdir).endswith(".h"):
        outdir = outdir.parent
    rows = load_tutorial()
    fresh = {
        outdir / "tutorial_text_generated.h": build_text_output(rows),
        outdir / "tutorial_count_generated.h": build_count_output(rows),
    }
    if args.check:
        for path, text in fresh.items():
            try:
                committed = path.read_text(encoding="utf-8")
            except FileNotFoundError:
                committed = None
            if committed is None or committed.strip() != text.strip():
                print(f"DRIFT: fresh compile differs from {path}", file=sys.stderr)
                sys.exit(1)
        print(f"tutorial --check OK: {len(rows)} slide(s) match fresh output")
        return
    for path, text in fresh.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print(f"Wrote {path}")
    print(f"Compiled {len(rows)} tutorial slide(s)")


if __name__ == "__main__":
    main()
