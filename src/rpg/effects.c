#include "effects.h"

/* Shared result slot (WRAM _DATA): written by the bank-3 resolver inside
 * the banked combo dispatch, read by fixed-bank battle code directly.
 * Single slot by design -- combat resolution is sequential. */
EffectResult g_effect_last;

/* Defend-block carry (see effects.h): stashed by the counter prep,
 * consumed by the defend body. */
uint8_t g_defend_block_save;
