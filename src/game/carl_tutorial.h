#ifndef CARL_TUTORIAL_H
#define CARL_TUTORIAL_H

#include <stdint.h>

/* Bit contract for g_carl_tut_flags (shared by the fixed wrappers in
 * carl_tutorial.c and the bank-10 bodies in carl_tutorial_banked.c). */
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

/* Next flee-reminder milestone (HP removed): 10, 20, ... 90.  Reset to
 * 10 at every battle start alongside the flags. */
#define CARL_TUT_MILE_START 10
#define CARL_TUT_MILE_MAX   90
#define CARL_TUT_MILE_STEP  10

#define CARL_TUT_HURT_COUNT 7

extern uint8_t g_carl_tut_flags;
extern uint8_t g_carl_tut_hurt;
extern uint8_t g_carl_tut_mile;

/* Bank-10 decision body (carl_tutorial_banked.c, dispatched through
 * the WRAM trampoline; self-contained: WRAM + regs only). */
void carl_tutorial_banked(void);

#endif /* CARL_TUTORIAL_H */
