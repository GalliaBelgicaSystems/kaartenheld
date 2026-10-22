#ifndef GAME_GAMBLE_H
#define GAME_GAMBLE_H

#include <stdint.h>
#include <stdbool.h>
#include "screen.h"

/* Chuck-a-luck (friendly kobold dice game, docs/chuck-a-luck.md).
 * The bank (kobold) has infinite cash: payouts never run out.
 *
 * FIXED-BANK BUDGET NOTE (AGENTS.md 52.18 family): the fixed bank has
 * ~50 B headroom, so the fixed side of this feature is exactly three
 * small items -- the screen.c dispatch case, the update stub, and the
 * dialogue YES handoff in dialogue_screen_update (paid for by removing
 * the direct INTERACTION_GAMBLE engage paths).  Entry, paint, input, dice, OAM, gold AND the overworld exit
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

/* Staged audio requests (WRAM): the banked body cannot call the audio
 * API, so game_update() consumes these.  g_gamble_sfx is an SFX id
 * (0xFF = none); g_gamble_jingle queues the shared chest jingle. */
extern uint8_t g_gamble_sfx;
extern uint8_t g_gamble_jingle;
/* Enter request: raised by the bank-8 dialogue choice body when the
 * player says YES; the per-frame tick opens SCREEN_GAMBLE and clears
 * it.  Keeps the fixed bank out of the gamble handoff entirely. */
extern uint8_t g_gamble_enter;
/* Farewell request: raised on exit; the audio tick hands the scene track
 * back while the fixed hook opens Nis's goodbye dialogue. */
extern uint8_t g_gamble_bye;

/* Bank-8 audio tick (src/game/gamble_content.c), called from the
 * per-frame tick body after the gamble frame + jingle bodies: decides
 * the frame's music request (casino while at the table, scene-track
 * restore after leaving) via g_bk_byte_b. */
void gamble_audio_tick(void);

/* Bank-8 screen switch used by the tick body when g_gamble_enter is
 * raised (replicates screen_change for the table). */
void gamble_enter_screen(void);

/* Bank-8 frame body (src/game/gamble_content.c): entry on the first
 * frame (LCD off, CRAM + kobold art + dice stream + full paint + OAM),
 * input + dynamic repaint on later frames, direct overworld exit on B
 * (replicated screen_change, see above).  Dispatched via
 * banked_call_run(). */
void gamble_frame_banked(void);

#endif /* GAME_GAMBLE_H */
