#include "dialogue.h"
#include "telemetry.h"
#include "banked.h"
#include <stddef.h>

/* The dialogue table is game content, registered at boot via
 * dialogue_register().  The engine only matches and plays lines.
 *
 * The table may live in a banked ROM region (see game_ids.h
 * GAME_CONTENT_BANK).  dialogue_start_def() copies the matching row and its
 * text into WRAM staging (g_dialogue_scratch / g_dlg_speaker / g_dlg_lines)
 * so DialogueState.speaker/.lines point at WRAM, never at banked ROM, and
 * the bank register is restored to 0 before returning.  A second
 * dialogue_start_def() while a dialogue is still active would overwrite the
 * staging; the screen model prevents this (only one dialogue is ever active,
 * started from the overworld screen and not restarted until it ends). */
const DialogueDefinition *g_dialogue_table = NULL;
uint8_t g_dialogue_table_count = 0;
/* BSS: dialogue_register() (via game_content_init() at every boot) sets
 * the home bank before any row is read. */
static uint8_t g_dialogue_bank;

/* Single-flight WRAM staging (not reentrant): a nested banked row/text
 * lookup would silently corrupt the outer one.  Safe today because only one
 * dialogue is ever active and scene_load() does not re-enter dialogue
 * resolution; keep that invariant. */
static DialogueDefinition g_dialogue_scratch;
char g_dlg_speaker[12];
char g_dlg_lines[MAX_DIALOGUE_LINES][21];

/* banked_copy() takes a uint8_t byte count; a larger row cannot be staged. */
typedef char dialogue_def_fits_banked_copy[sizeof(DialogueDefinition) <= 255 ? 1 : -1];

void dialogue_register(const DialogueDefinition *table, uint8_t count, uint8_t bank)
{
    g_dialogue_table = table;
    g_dialogue_table_count = count;
    g_dialogue_bank = bank;
}

static const DialogueDefinition *dialogue_get_row(uint8_t i)
{
    banked_copy(g_dialogue_bank, &g_dialogue_scratch, &g_dialogue_table[i], sizeof(DialogueDefinition));
    return &g_dialogue_scratch;
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
}

void dialogue_start_def(DialogueState *d, DialogueId id)
{
    /* Body runs banked (src/core/dialogue_banked.c) reading the registered
     * table directly; the wrapper emits DIALOGUE_STARTED afterwards
     * (banked code cannot call fixed-bank telemetry). */
    if (!d) return;
    d->active = false;
    g_bk_call_bank = 2;
    g_bk_call_target = (uint16_t)&dialogue_start_def_banked;
    g_bk_ptr_a = (void *)d;
    g_bk_byte_a = id;
    banked_call_run();
    if (d->active) {
        telemetry_emit(EVENT_DIALOGUE_STARTED, (uint8_t)d->id, 0, 0, 0);
    }
}

bool dialogue_next(DialogueState *d)
{
    if (!d || !d->active) return false;
    d->current_line++;
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
