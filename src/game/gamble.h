#ifndef GAME_GAMBLE_H
#define GAME_GAMBLE_H

#include <stdint.h>
#include <stdbool.h>
#include "screen.h"

/* Chuck-a-luck (friendly kobold dice game, docs/chuck-a-luck.md).
 * The bank (kobold) has infinite cash: payouts never run out.
 *
 * FIXED-BANK BUDGET NOTE (AGENTS.md 52.18 family): the fixed bank runs
 * close to 0x8000, so the fixed side of this feature is only three small
 * items -- the dialogue YES branch in dialogue_screen_update (records the
 * choice, closes the dialogue), the post-tick dialogue hook in
 * game_update() (opens Nis's farewell) and the casino music code.  Entry,
 * paint, input, dice, OAM, gold, the screen switches AND both dialogue
 * starts triggered from the table live in ROM bank 8, called from the
 * existing per-frame tick body -- the feature has no screen.c dispatch
 * case at all.  Consequences, documented in docs/chuck-a-luck.md:
 *  - no currency or screen-change telemetry for the table's own gold
 *    moves / screen switches (the extended snapshot still shows every
 *    gold delta and the screen id); dialogue and music telemetry are
 *    complete because those run through the fixed APIs.
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
 * input + dynamic repaint on later frames, and on B stages Nis's
 * farewell dialogue (g_bk_dlg_id) for the fixed hook.  Called from the
 * bank-8 per-frame tick body (same bank: plain call). */
void gamble_frame_banked(void);

#endif /* GAME_GAMBLE_H */
