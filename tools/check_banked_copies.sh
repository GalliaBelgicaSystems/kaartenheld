#!/bin/sh
# Banked-ABI lowering guard (AGENTS.md 52.11.1, 52.1 / issue A1).
#
# Banked bodies run with a switchable ROM bank mapped and are entered
# through the WRAM trampoline.  Two lowering families are fatal there:
#
#   * struct assignment / memcpy -> call ___memcpy (fixed-bank helper).
#     The harness skips CRT0, so the RAM-resident helpers are absent and
#     the ROM hangs.  Copy field-by-field instead (see card_copy_banked,
#     battle_hud_load_banked's "no struct assignment" comment).
#   * indirect calls -> ___sdcc_call_hl / ___sdcc_banked_call (same
#     RAM-resident family; function pointers are banned in
#     harness-exercised code for the same reason).
#
# This check compiles every real banked unit exactly as the debug build
# does and fails if the generated assembly references any of those
# helpers.  It detects the hazard itself (compiler lowering), not source
# heuristics, so byte-sized *dst stores and field-wise copies pass while
# any present-or-future whole-struct copy fails loudly.
#
# File selection anchors on ^#pragma bank at line start: comment mentions
# (e.g. events.c/dialogue.c describing their content files) are fixed-bank
# wrappers and must NOT be scanned.  Per-file flags come from the
# Makefile's own explicit build/debug/<obj> rules (52.20 alloc caps,
# TEST_LEVELS selection), so this cannot drift from the real build --
# codegen is flag- and layout-sensitive (AGENTS.md 52.19).
#
# Invoked via `make lint` with CC and INCLUDES exported.
set -u

cd "$(dirname "$0")/.." || exit 2

: "${CC:=lcc}"
if [ -z "${INCLUDES:-}" ]; then
    echo "banked-abi: INCLUDES must be set (invoke via make lint)" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# Extra flags for one source file, parsed from the Makefile's explicit
# build/debug/<obj>: src/<src> override rule (the -D* but -DDEBUG_BUILD,
# which is always passed, plus any -Wf* codegen flags).  Falls back to
# empty (base flags only), matching the generic pattern rule.
makefile_flags() {
    awk -v src="$1" '
        /^build\/debug\/[^:]*:/ {
            want = (index($0, ": " src " ") > 0 || \
                    index($0, ": " src "|") > 0 || \
                    index($0, " " src " ") > 0 || \
                    index($0, " " src "|") > 0) ? 1 : 0
            next
        }
        want && /^\t/ && index($0, "$(CC)") > 0 {
            out = ""
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^-D/ && $i != "-DDEBUG_BUILD") out = out " " $i
                else if ($i ~ /^-Wf/) out = out " " $i
            }
            sub(/^ /, "", out)
            print out
            exit
        }
    ' Makefile
}

fail=0
count=0
for f in $(grep -rl "^#pragma bank" src --include='*.c' | LC_ALL=C sort); do
    count=$((count + 1))
    extra=$(makefile_flags "$f")
    # INCLUDES and the derived flags are intentionally word-split.
    # shellcheck disable=SC2086
    if ! $CC -S -DDEBUG_BUILD $extra $INCLUDES -o "$tmp/out.asm" "$f" \
            2>"$tmp/err.txt"; then
        echo "banked-abi: cannot compile $f (with flags: -DDEBUG_BUILD $extra):"
        sed 's/^/    /' "$tmp/err.txt" >&2
        fail=1
        continue
    fi
    hits=$(grep -nE '___memcpy|___sdcc_banked_call|___sdcc_call_hl' \
        "$tmp/out.asm" || true)
    if [ -n "$hits" ]; then
        echo "banked-abi: FAIL $f lowers to RAM-resident helpers" \
            "(no struct assignment / memcpy / indirect calls in banked bodies):"
        printf '%s\n' "$hits" | sed 's/^/    /'
        fail=1
    fi
done
if [ "$fail" = 0 ]; then
    echo "banked-abi: clean ($count banked units, no RAM-resident lowerings)"
fi
exit "$fail"
