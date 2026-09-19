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
# This check compiles every #pragma-bank unit exactly as the debug build
# does and fails if the generated assembly references any of those
# helpers.  It detects the hazard itself (compiler lowering), not source
# heuristics, so byte-sized *dst stores and field-wise copies pass while
# any present-or-future whole-struct copy fails loudly.
#
# Invoked via `make lint` with CC and INCLUDES exported.  The per-file
# alloc-cap list must stay in sync with the Makefile's 52.20 rules:
# different optimization flags can change lowering.
set -u

: "${CC:=lcc}"
if [ -z "${INCLUDES:-}" ]; then
    echo "banked-abi: INCLUDES must be set (invoke via make lint)" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

# Files whose build rule adds -Wf--max-allocs-per-node500 (Makefile 52.20).
capped="src/world/world.c src/world/patrol_banked.c src/world/actor_load_banked.c"

fail=0
count=0
for f in $(grep -rl "#pragma bank" src --include='*.c' | LC_ALL=C sort); do
    count=$((count + 1))
    extra=""
    case " $capped " in
        *" $f "*) extra="-Wf--max-allocs-per-node500" ;;
    esac
    # INCLUDES is intentionally word-split (a -I flag list from make).
    # shellcheck disable=SC2086
    if ! $CC -S -DDEBUG_BUILD $extra $INCLUDES -o "$tmp/out.asm" "$f" \
            >/dev/null 2>&1; then
        echo "banked-abi: cannot compile $f" >&2
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
