#pragma bank 12

#include "save.h"
#include "game.h"
#include "ui.h"
#include <gb/gb.h>
#include "banked.h"
#include "telemetry.h"
#include "telemetry_ring.h"

extern uint8_t ui_font_tile_base;

/* ── Save-flow banked bodies (ROM bank 12) ──────────────────────────
 * The SAVE/LOAD screen's fixed update sits hard against 0x8000
 * (AGENTS.md 52.18): the whole picker decision (nav, pick, confirm,
 * auto-continue, leave) plus the slot commit (SRAM write + inline
 * GAME_SAVED ring emit, debug only) live here; the fixed caller only
 * stages, applies screens, and runs the LOAD tail (load + telemetry +
 * scene_load must stay fixed: bank-8 dispatch and fixed calls).
 * Bank 12 (not bank 8 with the SRAM bodies) has room to spare in both
 * builds.
 *
 * Self-contained (AGENTS.md §52.11.1): WRAM staging + GameState +
 * SRAM + ROM consts only, never a fixed-bank call, no trampoline
 * nesting, no di/ei (§52.11.3), no * / % (§52.18).  The SRAM probe and
 * write duplicate the op-0/op-1 paths of save_op_banked (bank 12):
 * KEEP IN SYNC with save_sram_banked.c if the slot geometry or checksum
 * ever changes. */

static uint8_t pick_checksum(const GameState *state)
{
    uint8_t sum = 0;
    uint16_t n = (uint16_t)sizeof(GameState);
    const uint8_t *b = (const uint8_t *)state;
    while (n--) sum += *b++;
    return sum;
}

static uint8_t pick_present(uint8_t idx)
{
    uint8_t *sram;
    uint8_t present;
    if (idx >= SAVE_SLOT_COUNT) return 0;
    if (g_save_state_capacity <
        (uint16_t)sizeof(GameState)) {
        return 0;
    }
    sram = (uint8_t *)(SAVE_SRAM_BASE + ((uint16_t)idx << 10));
    ENABLE_RAM;
    present = (sram[0] == (uint8_t)(SAVE_MAGIC & 0xFF) &&
               sram[1] == (uint8_t)(SAVE_MAGIC >> 8) &&
               sram[2] == SAVE_VERSION &&
               sram[3] == pick_checksum((const GameState *)(sram + 4))) ? 1 : 0;
    DISABLE_RAM;
    return present;
}

/* Save-spin countdown (frames left to show; 120 = ~2 s floor so an
 * instant SRAM write still reads as saving).  File BSS: zero covers
 * boot, transitions re-arm through the tick below. */
static uint8_t s_save_spin_ttl;

/* Shared slot commit: clamps the active slot, writes it, emits
 * GAME_SAVED inline (debug builds; the release macro compiles it
 * out -- same field-for-field replica pattern as
 * carl_grant_banked.c), and (re)arms the save-spin indicator. */
static void commit_write_save(GameState *state, uint8_t slot)
{
    uint8_t *sram;
    uint8_t *dst;
    const uint8_t *src;
    uint16_t n;
    if (slot >= SAVE_SLOT_COUNT) slot = 0;
    if (g_save_state_capacity <
        (uint16_t)sizeof(GameState)) {
        return;
    }
    sram = (uint8_t *)(SAVE_SRAM_BASE + ((uint16_t)slot << 10));
    ENABLE_RAM;
    sram[0] = (uint8_t)(SAVE_MAGIC & 0xFF);
    sram[1] = (uint8_t)(SAVE_MAGIC >> 8);
    sram[2] = SAVE_VERSION;
    sram[3] = pick_checksum(state);
    dst = sram + 4;
    src = (const uint8_t *)state;
    n = (uint16_t)sizeof(GameState);
    while (n--) *dst++ = *src++;
    DISABLE_RAM;
    telemetry_ring_emit((uint8_t)EVENT_GAME_SAVED, (uint8_t)(slot + 1), 0, 0, 0);
    /* Arm the spin indicator (upkeep draws it; ~2 s floor). */
    s_save_spin_ttl = SPIN_TTL;
}

/* Boot-notice spin primed flag: forces tile 241 to stream on the very
 * first SCREEN_NOTICE frame regardless of (frame & 7) alignment. */
static uint8_t s_notice_spin_primed;

/* Save-spin indicator tick: OAM entry 39 (always free) with tile 241
 * (free in both fetch blocks; re-streamed from the font every phase
 * so there is no boot load and no clobber state), hero palette slot
 * (prop 0, always programmed).  A phase redraws the glyph (\ | / -);
 * OAM is rewritten every frame so transition wipes never strand it. */
static void spin_show(uint8_t phase, uint8_t restream)
{
    uint8_t ch;
    uint8_t i;
    uint8_t b;
    volatile uint8_t *src;
    volatile uint8_t *dst;
    if (restream) {
        if (phase == 0) ch = '|';
        else if (phase == 1) ch = '/';
        else if (phase == 2) ch = '-';
        else ch = '\\';
        VBK_REG = 0;
        src = (volatile uint8_t *)(0x9000u + ((uint16_t)(ui_font_tile_base + (uint8_t)(ch - 32)) << 4));
        dst = (volatile uint8_t *)(0x8000u + ((uint16_t)SPIN_TILE << 4));
        for (i = 0; i < 16; i++) {
            BANKED_VRAM_READ(b, &src[i]);
            BANKED_VRAM_WRITE(&dst[i], b);
        }
    }
    shadow_OAM[SPIN_OAM_ENTRY].tile = SPIN_TILE;
    shadow_OAM[SPIN_OAM_ENTRY].prop = 0;
    shadow_OAM[SPIN_OAM_ENTRY].x = (uint8_t)(SPIN_PX + 8);
    shadow_OAM[SPIN_OAM_ENTRY].y = (uint8_t)(SPIN_PY + 16);
}

/* ── Active-slot commit (intro end + upkeep flush) ──────────────────
 * Writes g_game.save_slot_index with the live canonical state.  The
 * intro calls this right after game_restart() (fresh state reseeded
 * the world, no frame in between -- no sync needed); the upkeep
 * flush calls it after game_update()'s scene sync. */
void save_commit_active_banked(void)
{
    commit_write_save(&g_game.state, g_game.save_slot_index);
}

/* ── Per-frame upkeep (game_update, after the scene sync) ───────────
 * Flushes a staged autosave request (AP/quest rows, banked AP/quest
 * grants, town/castle entry) once, on canonical state, then runs the
 * save-spin indicator.  One dispatch covers both (no extra fixed
 * bytes for the spinner). */
void save_upkeep_banked(void)
{
    if (g_autosave_pending) {
        g_autosave_pending = 0;
        save_commit_active_banked();
    }
    if (g_game.screen == SCREEN_NOTICE) {
        /* Boot notice: spin continuously off the frame counter so
         * players learn the mark before they ever save. */
        uint8_t restream = (uint8_t)(!s_notice_spin_primed ||
                                     (((uint8_t)g_game.frame & 7) == 0));
        s_notice_spin_primed = 1;
        spin_show((uint8_t)(((uint8_t)g_game.frame >> 3) & 3), restream);
        return;
    }
    s_notice_spin_primed = 0;
    if (s_save_spin_ttl != 0) {
        spin_show((uint8_t)((s_save_spin_ttl >> 3) & 3),
                  (uint8_t)((s_save_spin_ttl & 7) == 0));
        s_save_spin_ttl--;
        if (s_save_spin_ttl == 0) {
            shadow_OAM[SPIN_OAM_ENTRY].y = 0;
        }
    }
}

/* ── Picker update (save_load_screen_update) ────────────────────────
 * In: g_bk_ptr_a = Game*.  Out: g_bk_byte_b = action (0 nothing,
 * 1 start intro, 2 load slot staged in g_bk_byte_c, 3 leave to the
 * previous screen).  Owns nav edges (UP/DOWN straight from WRAM,
 * item_quest_nav_banked idiom), the overwrite confirm, and the
 * auto-continue flag; UP wins over DOWN on a shared frame and A is
 * never double-handled (same else-if priority as the fixed chain this
 * replaces). */
void save_menu_update_banked(void)
{
    Game *g = (Game *)g_bk_ptr_a;
    uint8_t edges;
    uint8_t idx;
    if (!g) {
        g_bk_byte_b = 0;
        return;
    }
    idx = g->save_slot_index;
    /* Game-over auto-continue (message 4): act as an A press on LOAD
     * with the active slot, same frame, no picker input. */
    if (g->save_slot_message == 4) {
        g->save_slot_message = 0;
        g_bk_byte_b = 2;
        g_bk_byte_c = idx;
        return;
    }
    edges = (uint8_t)(g_inp_pad & (uint8_t)~g_inp_prev);
    if (edges & (uint8_t)(1u << INPUT_UP)) {
        if (idx > 0) {
            g->save_slot_index--;
            g->save_slot_message = 0;
            g->render_cache.valid = false;
        }
    } else if (edges & (uint8_t)(1u << INPUT_DOWN)) {
        if ((uint8_t)(idx + 1) < SAVE_SLOT_COUNT) {
            g->save_slot_index++;
            g->save_slot_message = 0;
            g->render_cache.valid = false;
        }
    } else if (edges & (uint8_t)((1u << INPUT_A) | (1u << INPUT_START))) {
        /* A/START confirms (START doubles as confirm so a start-only
         * boot press-through reaches the game): NEW-GAME pick stages
         * the overwrite confirm on occupied slots and starts the
         * intro otherwise; LOAD reports the slot for the applier.
         * B alone backs out / dismisses. */
        if (g->save_slot_mode == 2) {
            /* NEW-GAME pick: empty slot (or the confirm below) starts
             * the intro; first A/START on an occupied slot stages the
             * overwrite confirm and repaints. */
            if (g->save_slot_message == 3) {
                g->save_slot_message = 0;
                g->intro_slide = 0;
                g_bk_byte_b = 1;
            } else if (pick_present(idx)) {
                g->save_slot_message = 3;
                g->render_cache.valid = false;
                g_bk_byte_b = 0;
            } else {
                g->save_slot_message = 0;
                g->intro_slide = 0;
                g_bk_byte_b = 1;
            }
        } else {
            /* LOAD: the fixed applier validates the slot itself, so no
             * separate present probe is needed. */
            g->save_slot_message = 0;
            g_bk_byte_b = 2;
            g_bk_byte_c = idx;
        }
    } else if (edges & (uint8_t)(1u << INPUT_B)) {
        /* B dismisses a NEW-GAME overwrite confirm back to the picker;
         * anything else clears any stale status message and leaves to
         * the previous screen. */
        if (g->save_slot_message == 3) {
            g->save_slot_message = 0;
            g->render_cache.valid = false;
            g_bk_byte_b = 0;
        } else {
            g->save_slot_message = 0;
            g_bk_byte_b = 3;
        }
    } else {
        g_bk_byte_b = 0;
    }
}
