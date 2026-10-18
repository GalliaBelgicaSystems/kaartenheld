#include "dialogue.h"
#include "telemetry.h"
#include "banked.h"
#include <stddef.h>

/* The dialogue table is game content, registered at boot via
 * dialogue_register().  The engine only matches and plays lines.
 *
 * The table lives in a banked ROM region; dialogue_start_def_banked() reads
 * it directly (same bank) and stages speaker + lines into the WRAM buffers
 * (g_dlg_speaker / g_dlg_lines) so DialogueState points at WRAM, never at
 * banked ROM. */
const DialogueDefinition *g_dialogue_table = NULL;
uint8_t g_dialogue_table_count = 0;

/* Single-flight WRAM staging (not reentrant): a nested banked row/text
 * lookup would silently corrupt the outer one.  Safe today because only one
 * dialogue is ever active and scene_load() does not re-enter dialogue
 * resolution; keep that invariant. */
char g_dlg_speaker[12];
char g_dlg_lines[MAX_DIALOGUE_LINES][21];

void dialogue_register(const DialogueDefinition *table, uint8_t count, uint8_t bank)
{
    (void)bank;
    g_dialogue_table = table;
    g_dialogue_table_count = count;
}

void dialogue_init(DialogueState *d)
{
    if (!d) return;
    d->active = false;
    d->id = DIALOGUE_ID_NONE;
    d->current_line = 0;
    d->line_count = 0;
    d->speaker = "";
    d->completion_flag = 0;
    /* choice/index/made need no init: dialogue_start_def_banked stages
     * all three on every activation (the sole writer), and update
     * early-returns unless active. */
}

void dialogue_start_def(DialogueState *d, DialogueId id)
{
    /* Body runs banked (src/core/dialogue_banked.c) reading the registered
     * table directly; the wrapper emits DIALOGUE_STARTED afterwards
     * (banked code cannot call fixed-bank telemetry).  Staging
     * discipline: only bank/target/ptr_a/byte_a are staged here;
     * g_bk_byte_b/c arrive stale from whatever dispatched last, so the
     * banked body must never read them (the retired floor-stamp block
     * did, corrupting world.map on small stale pairs). */
    if (!d) return;
    d->active = false;
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&dialogue_start_def_banked;
    g_bk_ptr_a = (void *)d;
    g_bk_byte_a = id;
    banked_call_run();
#ifdef DEBUG_BUILD
    if (d->active) {
        telemetry_emit(EVENT_DIALOGUE_STARTED, (uint8_t)d->id, 0, 0, 0);
    }
#endif
}

bool dialogue_next(DialogueState *d)
{
    if (!d || !d->active) return false;
    /* Three-line pages: each A press turns to the next triple of lines,
     * so a 2-line greeting reads in one page and an 8-line lesson in
     * three.  The continue chevron (far bottom right, col 19, row 17)
     * shows whenever another page follows (current+3 < line_count) and
     * hides on the last page. */
    d->current_line = (uint8_t)(d->current_line + 3);
    if (d->current_line >= d->line_count) {
        dialogue_end(d);
        return false;
    }
    telemetry_emit(EVENT_DIALOGUE_NEXT, (uint8_t)d->id, d->current_line, 0, 0);
    return true;
}

DialogueId dialogue_end(DialogueState *d)
{
    DialogueId old_id;
    if (!d || !d->active) return DIALOGUE_ID_NONE;
    old_id = d->id;
    d->active = false;
    d->id = DIALOGUE_ID_NONE;
    telemetry_emit(EVENT_DIALOGUE_ENDED, (uint8_t)old_id, 0, 0, 0);
    return old_id;
}
