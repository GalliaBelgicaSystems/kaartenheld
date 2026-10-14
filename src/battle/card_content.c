#pragma bank 2

#include "card.h"

/* Packed card descriptions (fixed 18-byte stride, NUL-padded), read directly
 * by item_content_banked.c (same bank).  Bank 2 keeps the text
 * out of the fixed-bank budget (make memmap).
 *
 * NOTE: rows must be a proper 2D array.  A flat uint8_t[] initialized with
 * multiple string literals is a constraint violation that SDCC silently
 * miscompiles to "first row + zero fill" (shipped once; the
 * card_description_render scenario locks the rendered text). */
const uint8_t s_card_desc_blob[CARD_DESC_TYPES][CARD_DESC_STRIDE] = {
    /* NOTE: each literal's implicit NUL counts toward STRIDE -- pad with
     * STRIDE - len - 1 explicit NULs only. */
    /* SWORD */  "Sword: physical\0\0",
    /* SHIELD */ "Shield: block dmg",
    /* BOW */    "Bow: ranged dmg\0\0",
    /* HEAL */   "Heal: restore HP\0",
    /* DAGGER */ "Dagger: poison"
};
