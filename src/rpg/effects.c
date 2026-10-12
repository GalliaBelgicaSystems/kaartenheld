#include "effects.h"

/* Shared result slot (WRAM _DATA): written by the bank-3 resolver inside
 * the banked combo dispatch, read by fixed-bank battle code through
 * effect_last().  Single slot by design -- combat resolution is
 * sequential. */
EffectResult g_effect_last;

const EffectResult *effect_last(void)
{
    return &g_effect_last;
}
