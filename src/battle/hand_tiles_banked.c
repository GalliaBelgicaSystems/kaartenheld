#pragma bank 8
#pragma disable_warning 110

#include <gb/gb.h>
#include "battle.h"
#include "combo.h"
#include "game.h"
#include "banked.h"
#include "ui/ui.h"
#include "world/actor.h"

/* Battle poker-hand name tiles (assets/hand_names.png via
 * tools/compose_hand_names.py, encoded by the gfx rule into
 * src/gfx/hand_name_tiles.h).  Bank 8 with its only reader (this
 * body): bank 8 has room (overflow bank, AGENTS.md 55.5) while the
 * fixed bank and bank 3 are full, and a banked body must share its
 * table's bank (AGENTS.md 52.11.1).  Sheet cell order (compose_hand_names.py):
 * 0 down arrow (dark up-arrow rotated 180), then the row-6/7 letter
 * fragments tw o pa ir sh hor rt st ra ig ht f lu sh t hr ee ki nd
 * fo ur fiv ve ul lh ou se. */
#include "gfx/hand_name_tiles.h"

/* What the bank-3 renderer may stamp (UI_TILE_HAND_* in ui.h):
 * 0xFF = VRAM already shows this tier's word (+arrow), 0xFE =
 * arrow-only (enemy forecast with no staged hand), anything else =
 * nothing usable resident.  Re-armed off-battle, so a new fight never
 * inherits stale VRAM.  BSS zero doubles as HAND_NONE (no word): the
 * off-battle re-arm below promotes it to 0xFF before any battle runs
 * (AGENTS.md 52.27: no non-zero WRAM initializer -- CRT0 never copies
 * _INITIALIZER). */
uint8_t g_hand_shown_tier;
/* What the bank-3 renderer WANTS streamed (same encoding): staged by
 * every combo-row render (NONE when the row shows no word, including
 * the message states), read by the sync below.  The sync cannot read
 * last_combo.tier here: dagger-counter staging reports HAND_NONE from
 * the defend resolve while its display tier comes from the counter
 * path, so the renderer stages the display tier explicitly. */
uint8_t g_hand_want_tier;
/* Staged word cells, copied on every stream (see header contract). */
uint8_t g_hand_cells_wram[8];
uint8_t g_hand_cell_len;

/* Tier -> sheet cells (HAND_* in combo.h, 1..10; 0xFF = word space,
 * stamped as the font blank by the renderer).  14 columns: the longest
 * words (STRAIGHT FLUSH, SHORT STRAIGHT) use every slot; shorter words
 * pad with 0xFF (an 8-wide table silently read the NEXT tier's row for
 * positions 8+, painting garbage art the harness cannot see -- it only
 * asserts stamped ids and buffer letters, never VRAM bytes).  Kept in
 * sync with the letter words in ui_battle_content.c (same order, same
 * spaces) -- content and letters can never drift without both tables
 * changing. */
static const uint8_t s_hand_cells[11][14] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, /* NONE */
    { 3, 4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },       /* PAIR */
    { 1, 2, 0xFF, 3, 4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },             /* TWO PAIR */
    { 15, 16, 17, 0xFF, 18, 19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },          /* THREE KIND */
    { 8, 9, 10, 11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },          /* STRAIGHT */
    { 12, 13, 14, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },      /* FLUSH */
    { 12, 24, 25, 26, 27, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },         /* FULL HOUSE */
    { 20, 21, 0xFF, 18, 19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },        /* FOUR KIND */
    { 8, 9, 10, 11, 0xFF, 12, 13, 14, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },               /* STRAIGHT FLUSH */
    { 22, 23, 0xFF, 18, 19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },        /* FIVE KIND */
    { 5, 6, 7, 0xFF, 8, 9, 10, 11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },                  /* SHORT STRAIGHT */
};

/* Word lengths in CELLS (including the separating blank cell):
 * PAIR 2, TWO PAIR 5, THREE KIND 6, STRAIGHT 4, FLUSH 3,
 * FULL HOUSE 5, FOUR KIND 5, STRAIGHT FLUSH 8, FIVE KIND 5,
 * SHORT STRAIGHT 8.  Index 0 unused.  Cells, not letters: a sheet
 * cell renders 1-2 letters ("pa" shows PA), so the on-screen word is
 * cell_count columns wide while the buffer word is strlen columns
 * wide -- the renderer stamps tiles per cell and letters per letter,
 * each centered on its own.  Must equal the non-0xFF s_hand_cells
 * span per tier: the sync streams exactly these cells. */
static const uint8_t s_hand_len[11] = {
    0, 2, 5, 6, 4, 3, 5, 5, 8, 5, 8
};

/* Window slots (BG, UI_TILE_HAND_SLOTS in ui.h): slot 0 is the down
 * arrow, slots 1-8 the word cells.  Split across the rider tail and
 * the dice tail (see ui.h): battle needs neither, and every screen
 * that clobbers them re-arms g_hand_shown_tier. */
static const uint8_t s_hand_slots[15] = UI_TILE_HAND_SLOTS;

/* LCD-safe BG stream (BANKED_VRAM_WRITE per byte: the 256 Hz timer ISR
 * can otherwise eclipse the wait->store window and drop the write
 * into Mode 3, AGENTS.md 52.11.3).  Ids >= 128 fetch from the 0x8800
 * block, i.e. raw 0x8000 + id*16 like the battle-art loader. */
static void hand_stream_slot(uint8_t slot, uint8_t cell)
{
    volatile uint8_t *dst;
    const uint8_t *src;
    uint8_t j;

    dst = (volatile uint8_t *)(0x8000u + ((uint16_t)slot << 4));
    src = hand_name_tiles + ((uint16_t)cell << 4);
    VBK_REG = 0;
    for (j = 0; j < 16; j++) {
        BANKED_VRAM_WRITE(&dst[j], src[j]);
    }
}

/* On-demand hand-tile sync, called every non-terminal frame from
 * game_frame_tick_banked (same bank 8, plain call -- never a nested
 * trampoline).  Runs AFTER the render each tick, so last_combo already
 * holds this tick's preview: on a tier change it streams the word and
 * re-arms DIRTY_COMBO, and the next tick's render stamps over resident
 * art (one blank frame, invisible at 60 fps and settled before any
 * harness assert or screenshot).  The renderer blanks the name area
 * whenever its tier disagrees with g_hand_shown_tier, so a stale word
 * never shows. */
void hand_tiles_sync_banked(void)
{
    Battle *b = &g_game.battle;
    uint8_t tier;
    uint8_t i;
    uint8_t len;
    uint8_t cell;

    if (g_game.screen != SCREEN_BATTLE) {
        g_hand_shown_tier = 0xFF;
        g_hand_want_tier = HAND_NONE;
        return;
    }
    tier = g_hand_want_tier;
    if (b->combo_count != 0 && tier != HAND_NONE) {
        /* Staged hand with a poker tier: stream arrow + word.  Re-arm
         * the combo render: it runs BEFORE this tick (gating on the
         * old g_hand_shown_tier), so without a re-armed dirty bit the
         * settled frame would keep the blank it drew on the mismatch.
         * One extra combo redraw on the change tick only. */
        if (tier == g_hand_shown_tier) return;
        hand_stream_slot(s_hand_slots[0], 0);
        len = s_hand_len[tier];
        g_hand_cell_len = len;
        for (i = 0; i < len; i++) {
            cell = s_hand_cells[tier][i];
            g_hand_cells_wram[i] = cell;
            if (cell != 0xFF) {
                hand_stream_slot(s_hand_slots[(uint8_t)(i + 1)], cell);
            }
        }
        g_hand_shown_tier = tier;
        b->dirty |= BATTLE_DIRTY_COMBO;
        return;
    }
    /* No staged word: the enemy forecast still needs its down arrow
     * (row 7, cols 8-10) while a strike is incoming. */
    if ((b->phase == BATTLE_PHASE_PLAYER_DEFEND ||
         b->phase == BATTLE_PHASE_ENEMY_TELEGRAPH) &&
        b->enemy_incoming_dmg != 0) {
        if (g_hand_shown_tier == 0xFE) return;
        hand_stream_slot(s_hand_slots[0], 0);
        g_hand_shown_tier = 0xFE;
    }
}
