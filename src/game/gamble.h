#ifndef GAME_GAMBLE_H
#define GAME_GAMBLE_H

#include <stdint.h>
#include <stdbool.h>
#include "screen.h"

/* Chuck-a-luck (friendly kobold dice game, docs/chuck-a-luck.md).
 * The bank (kobold) has infinite cash: payouts never run out.
 *
 * FIXED-BANK BUDGET NOTE (AGENTS.md 52.18 family): the fixed bank has
 * ~50 B headroom, so the fixed side of this feature is exactly two
 * items -- the screen.c dispatch case and the update stub (~30 B
 * total).  Entry, paint, input, dice, OAM, gold AND the overworld exit
 * all live in ROM bank 8 behind the single frame body: even the
 * screen_change exit call does not fit in fixed, so the frame body
 * replicates screen_change's overworld-target effects with WRAM
 * writes (see gamble_exit_to_overworld).  Consequences, all documented
 * as v1 gaps with follow-ups in docs/chuck-a-luck.md:
 *  - silent (no SFX);
 *  - gold moves by direct WRAM writes + no screen-change call, so no
 *    currency or screen-change telemetry (the extended snapshot still
 *    shows every gold delta and the screen id).
 * Bet steps are fixed 1/5/10; payouts 1:1 / 2:1 / 3:1 by match count
 * (lose the bet on zero matches). */

typedef enum {
    GAMBLE_PHASE_CHOOSE = 0,
    GAMBLE_PHASE_RESULT = 1
} GamblePhase;

/* Bet steps (fixed, per design): 1 / 5 / 10 gold. */
#define GAMBLE_BET_COUNT 3u

typedef struct {
    uint8_t chosen;     /* 1..6, the number the hero bets on */
    uint8_t bet_idx;    /* 0..2 -> {1,5,10} gold */
    uint8_t dice[3];    /* last roll, each 1..6 (0 = not rolled yet) */
    uint8_t phase;      /* GamblePhase */
    uint8_t field;      /* 0 = NUM row, 1 = BET row (caret position) */
    uint8_t matches;    /* 0..3 matches of chosen in dice */
    uint8_t won;        /* nonzero when the last roll paid */
    uint8_t entered;    /* nonzero once the entry paint has run */
} GambleState;

extern GambleState g_gamble;

/* Bank-8 frame body (src/game/gamble_content.c): entry on the first
 * frame (LCD off, CRAM + kobold art + dice stream + full paint + OAM),
 * input + dynamic repaint on later frames, direct overworld exit on B
 * (replicated screen_change, see above).  Dispatched via
 * banked_call_run(). */
void gamble_frame_banked(void);

#endif /* GAME_GAMBLE_H */
