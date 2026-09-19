#pragma bank 4

#include "battle.h"
#include "rpg/status.h"
#include "banked.h"

/* ── Banked battle navigation / selection cluster (ROM bank 4) ──────
 * Hosts the hand-cursor / combo-selection / target-selection / enemy
 * liveness logic, moved out of the fixed bank (which was overflowing
 * make memmap, AGENTS.md §52.18) into a single banked dispatch body.
 *
 * Entered through the WRAM banked-call trampoline (src/crt0.s) via the
 * fixed thin wrappers in src/battle/battle.c: g_bk_ptr_a = Battle*,
 * g_bk_byte_a = NAV_OP_ opcode, g_bk_byte_b = argument, results returned
 * in g_bk_byte_c (booleans).
 *
 * Self-contained (AGENTS.md §52.11.1): reads/writes only WRAM structs
 * (the staged Battle*, the battle-scoped grey masks in status.h) and its
 * own bank-local helpers.  It must NOT call any fixed-bank function --
 * telemetry for target moves is emitted by the fixed wrapper after the
 * dispatch returns. */

/* Dispatch opcodes (shared with src/battle/battle.c wrappers). */
#define NAV_OP_CURSOR_MOVE      1
#define NAV_OP_TARGET_MOVE      2
#define NAV_OP_ALL_DEAD         3
#define NAV_OP_CARD_SELECT      4
#define NAV_OP_IS_CARD_SELECTED 5
#define NAV_OP_HAND_PLAYABLE    6

/* Total energy cost reserved by the cards currently selected into the combo.
 * Selection validates against the un-reserved remainder, so this never
 * exceeds b->energy. */
static uint8_t nav_combo_reserved_cost(const Battle *b)
{
    uint8_t i, sum = 0;
    for (i = 0; i < b->combo_count; i++) {
        sum += b->hand[b->selected_indices[i]].cost;
    }
    return sum;
}

/* A hand card can be added to the current combo only while it has uses left,
 * is not greyed out by poison, AND its cost fits in the phase energy not yet
 * reserved by the pending combo (deck.md Phase 10).  Greyed hand cards (bit
 * set in s_grey_mask[0]) are unplayable for the grey-out duration -- the
 * cursor skips them, selection rejects them, and the auto-play fallback in
 * battle_execute_combo ignores them. */
static bool nav_hand_playable(const Battle *b, uint8_t hand_idx)
{
    uint8_t available;
    if (b->hand[hand_idx].type == BATTLE_CARD_TYPE_EMPTY) return false;
    if (b->hand[hand_idx].uses_remaining == 0) return false;
    if ((s_grey_mask[0] & (uint8_t)(1u << hand_idx)) != 0) return false;
    available = b->energy - nav_combo_reserved_cost(b);
    return b->hand[hand_idx].cost <= available;
}

static bool nav_is_card_selected(const Battle *b, uint8_t hand_idx)
{
    uint8_t i;
    for (i = 0; i < b->combo_count; i++) {
        if (b->selected_indices[i] == hand_idx) {
            return true;
        }
    }
    return false;
}

/* Transient HUD message (row 12): id 1 = NO ENERGY, 2 = OUT OF USES,
 * 3 = ONE RING (docs/loot.md §34.3).  Field writes only (the fixed-bank
 * static helper is unreachable while the content bank is mapped). */
static void nav_msg(Battle *b, uint8_t id)
{
    b->msg_id = id;
    b->msg_ttl = 45;
    b->dirty |= BATTLE_DIRTY_MSG;
}

static void nav_cursor_move(Battle *b, int8_t dir)
{
    uint8_t start, step;
    if (b->phase != BATTLE_PHASE_PLAYER_SELECT && b->phase != BATTLE_PHASE_PLAYER_DEFEND) return;
    start = b->cursor_pos;
    for (step = 0; step < BATTLE_HAND_SIZE; step++) {
        if (dir < 0) {
            b->cursor_pos = (b->cursor_pos == 0) ? (BATTLE_HAND_SIZE - 1) : (uint8_t)(b->cursor_pos - 1);
        } else {
            b->cursor_pos = (uint8_t)((b->cursor_pos + 1 >= BATTLE_HAND_SIZE) ? 0 : (b->cursor_pos + 1));
        }
        if (nav_hand_playable(b, b->cursor_pos)) break;
        if (b->cursor_pos == start) break;
    }
    b->dirty |= (BATTLE_DIRTY_HAND | BATTLE_DIRTY_DESC);
}

static void nav_target_move(Battle *b, int8_t dir)
{
    uint8_t i, t, old;
    if (b->enemy_count <= 1) return;

    old = b->target_idx;
    t = old;
    for (i = 0; i < b->enemy_count; i++) {
        if (dir < 0) {
            t = (t == 0) ? (uint8_t)(b->enemy_count - 1) : (uint8_t)(t - 1);
        } else {
            t = (uint8_t)((t + 1 >= b->enemy_count) ? 0 : (t + 1));
        }
        if (b->enemies[t].hp != 0) {
            if (t != old) {
                b->target_idx = t;
                b->dirty |= (uint8_t)(BATTLE_DIRTY_ENEMIES | BATTLE_DIRTY_BANNER);
            }
            return;
        }
    }
}

static void nav_card_select(Battle *b)
{
    uint8_t step, next_pos;
    if (b->phase != BATTLE_PHASE_PLAYER_SELECT && b->phase != BATTLE_PHASE_PLAYER_DEFEND) {
        return;
    }

    if (nav_is_card_selected(b, b->cursor_pos)) {
        return;
    }

    if (!nav_hand_playable(b, b->cursor_pos)) {
        nav_msg(b,
                (b->hand[b->cursor_pos].uses_remaining == 0) ? 2 : 1);
        return;
    }

    /* MAX ONE RING per selection (docs/loot.md §34.3). */
    if (b->hand[b->cursor_pos].ring) {
        uint8_t s;
        for (s = 0; s < b->combo_count; s++) {
            if (b->hand[b->selected_indices[s]].ring) {
                nav_msg(b, 3);
                return;
            }
        }
    }

    if (b->combo_count < BATTLE_HAND_SIZE) {
        b->selected_indices[b->combo_count++] = b->cursor_pos;
        /* BATTLE_DIRTY_HERO: the deck/AP line (row 7) draws the un-reserved
         * energy, so it must redraw on every selection/undo or the AP badge
         * goes stale while the combo is being built. */
        b->dirty |= (BATTLE_DIRTY_COMBO | BATTLE_DIRTY_HAND | BATTLE_DIRTY_DESC | BATTLE_DIRTY_HERO);
        /* Report a successful selection to the fixed wrapper so it can
         * play the select blip only when a card actually joined the combo
         * (not on rejections like NO ENERGY / a duplicate). */
        g_bk_byte_c = 1;

        next_pos = b->cursor_pos;
        for (step = 1; step < BATTLE_HAND_SIZE; step++) {
            next_pos++;
            if (next_pos >= BATTLE_HAND_SIZE) next_pos = 0;
            if (!nav_is_card_selected(b, next_pos) &&
                nav_hand_playable(b, next_pos)) {
                b->cursor_pos = next_pos;
                break;
            }
        }
    }
}

void battle_nav_banked(void)
{
    Battle *b = (Battle *)g_bk_ptr_a;
    uint8_t op = g_bk_byte_a;

    if (!b) return;

    switch (op) {
    case NAV_OP_CURSOR_MOVE:
        nav_cursor_move(b, (int8_t)g_bk_byte_b);
        break;
    case NAV_OP_TARGET_MOVE:
        nav_target_move(b, (int8_t)g_bk_byte_b);
        break;
    case NAV_OP_ALL_DEAD:
        g_bk_byte_c = 0;
        {
            uint8_t i;
            for (i = 0; i < b->enemy_count; i++) {
                if (b->enemies[i].hp != 0) return;
            }
            g_bk_byte_c = 1;
        }
        break;
    case NAV_OP_CARD_SELECT:
        g_bk_byte_c = 0;
        nav_card_select(b);
        break;
    case NAV_OP_IS_CARD_SELECTED:
        g_bk_byte_c = nav_is_card_selected(b, g_bk_byte_b) ? 1 : 0;
        break;
    case NAV_OP_HAND_PLAYABLE:
        g_bk_byte_c = nav_hand_playable(b, g_bk_byte_b) ? 1 : 0;
        break;
    default:
        break;
    }
}
