#ifndef CARL_TUTORIAL_H
#define CARL_TUTORIAL_H

#include <stdint.h>

/* Bit contract for g_carl_tut_flags (shared by the fixed wrappers in
 * carl_tutorial.c and the bank-10 bodies in carl_tutorial_banked.c).
 * All six bits are single-bit tested (mask &) and set/cleared (|= / &= ~)
 * only; no site compares the whole byte (==), so 0x80 never collides
 * with a multi-bit state -- verified by grep over carl_tutorial_banked.c
 * and choice_nav_banked.c (Nov 2026). */
#define CARL_TUT_FLAG_ATTACK_SHOWN 0x01
#define CARL_TUT_FLAG_DEFEND_SHOWN 0x02
#define CARL_TUT_FLAG_POKER_SHOWN  0x04
#define CARL_TUT_FLAG_FLEE_SHOWN   0x08
/* Bit 0x10 retired (was the mid-battle open flag): the open box now
 * parks Battle.msg_id 9 instead, so no game-state flag -- and no
 * game-owned close hook -- is needed for the return trip. */
#define CARL_TUT_FLAG_HURT_ARMED  0x20
/* Offer accepted (INTRO YES): the spar that follows is a lesson, so the
 * coaching boxes fire.  Assigned (full re-init) by the bank-10 choice
 * body on YES, consumed when the attack box opens, cleared on non-Carl
 * battles by the tick body.  Battles that bypass the offer (debug boot,
 * debug re-arm) run plain. */
#define CARL_TUT_FLAG_LESSON      0x40
/* THIS spar's start will grant the AP (armed by the bank-10 INTRO YES
 * handler when TUTORIAL_SKIPPED is still clear -- exactly the case the
 * bank-5 grant body does not early-return on): the attack box claims
 * the AP only then.  A re-spar after a flee, or a spar after the
 * leave-the-field skip, grants nothing (one-shot via TUTORIAL_SKIPPED)
 * and opens the no-grant coaching box instead, so the "TAKE THIS AP"
 * line is never a lie.  Assigned fresh on every YES accept (the grant
 * runs right after YES), consumed with LESSON when the attack box
 * opens, cleared on non-Carl battles.  Lives in the choice body and
 * not the grant body because release bank 5 sits at 0 B headroom. */
#define CARL_TUT_FLAG_AP_GRANTED  0x80

/* Mile tracker retired with the 10-HP spar (the every-10-HP flee reminder
 * is gone): the storage byte stays so the WRAM layout does not shift
 * (AGENTS.md 52.24), but nothing reads or writes it anymore. */

#define CARL_TUT_HURT_COUNT 7

extern uint8_t g_carl_tut_flags;
extern uint8_t g_carl_tut_hurt;
extern uint8_t g_carl_tut_mile;

/* Bank-10 decision body (carl_tutorial_banked.c, dispatched through
 * the WRAM trampoline; self-contained: WRAM + regs only). */
void carl_tutorial_banked(void);

/* Bank-5 spar-start grant body (carl_grant_banked.c, dispatched from
 * start_battle_from_world(); self-contained: WRAM + regs only). */
void carl_spar_grant_banked(void);

#endif /* CARL_TUTORIAL_H */
