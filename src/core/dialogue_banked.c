#pragma bank 2

#include "dialogue.h"
#include "banked.h"
#include "game.h"

/* ── Banked dialogue start (src/core/dialogue.c dispatcher) ─────────
 * Scans the registered dialogue table DIRECTLY (same bank -- no
 * banked_copy, no per-row scratch) and stages speaker + lines into the
 * WRAM buffers.  Sets d->active on success; the fixed-bank wrapper emits
 * DIALOGUE_STARTED afterwards.  All pointers stored into DialogueState
 * target WRAM buffers, never bank 2 rodata. */

static DialogueState *s_dlg_d;
static DialogueId s_dlg_id;
static uint8_t s_dlg_i;
static uint8_t s_dlg_j;
static const DialogueDefinition *s_dlg_def;
static const char *s_dlg_src;

void dialogue_start_def_banked(void)
{
    s_dlg_d = (DialogueState *)g_bk_ptr_a;
    s_dlg_id = g_bk_byte_a;
    s_dlg_def = (const DialogueDefinition *)0;

    if (!s_dlg_d) return;

    for (s_dlg_i = 0; s_dlg_i < g_dialogue_table_count; s_dlg_i++) {
        if (g_dialogue_table[s_dlg_i].id == s_dlg_id) {
            s_dlg_def = &g_dialogue_table[s_dlg_i];
            break;
        }
    }
    if (!s_dlg_def) { g_dlg_speaker[0] = 0; return; }

    s_dlg_d->active = true;
    s_dlg_d->id = s_dlg_def->id;
    s_dlg_d->current_line = 0;
    s_dlg_d->line_count = (s_dlg_def->line_count > MAX_DIALOGUE_LINES) ? MAX_DIALOGUE_LINES : s_dlg_def->line_count;
    s_dlg_d->completion_flag = s_dlg_def->completion_flag;

    if (s_dlg_def->speaker) {
        for (s_dlg_i = 0; s_dlg_i < 11 && s_dlg_def->speaker[s_dlg_i]; s_dlg_i++) {
            g_dlg_speaker[s_dlg_i] = s_dlg_def->speaker[s_dlg_i];
        }
        g_dlg_speaker[s_dlg_i] = 0;
        s_dlg_d->speaker = g_dlg_speaker;
    } else {
        g_dlg_speaker[0] = 0;
        s_dlg_d->speaker = g_dlg_speaker;
    }

    for (s_dlg_i = 0; s_dlg_i < MAX_DIALOGUE_LINES; s_dlg_i++) {
        s_dlg_src = (s_dlg_i < s_dlg_d->line_count) ? s_dlg_def->lines[s_dlg_i] : (const char *)0;
        for (s_dlg_j = 0; s_dlg_j < 20 && s_dlg_src && s_dlg_src[s_dlg_j]; s_dlg_j++) {
            g_dlg_lines[s_dlg_i][s_dlg_j] = s_dlg_src[s_dlg_j];
        }
        g_dlg_lines[s_dlg_i][s_dlg_j] = 0;
        s_dlg_d->lines[s_dlg_i] = g_dlg_lines[s_dlg_i];
    }
    /* Floor-painted open-state mid-visit flip (AGENTS.md 55.7): the
     * event's DIALOGUE action stages the loot cell's flat map offset
     * in bytes b/c; stamp painted+1 now so the opening redraw already
     * shows the open cell (open art always lives one cell after the
     * closed art -- compile-time adjacency, validated).  Out-of-range
     * means "no stamp" (boot zeros, ordinary dialogues, stale staging
     * under harnesses that skip CRT0's WRAM clear); no loot cell may
     * sit at flat 0. */
    if (g_bk_byte_b | g_bk_byte_c) {
        uint16_t flat = ((uint16_t)g_bk_byte_c << 8) | g_bk_byte_b;
        if (flat < (uint16_t)(WORLD_WIDTH * WORLD_HEIGHT)) {
            ((uint8_t *)g_game.world.map)[flat] =
                (uint8_t)(((uint8_t *)g_game.world.map)[flat] + 1);
        }
    }
}
