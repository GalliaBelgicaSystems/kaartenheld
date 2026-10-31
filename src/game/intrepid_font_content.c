#pragma bank 10

#include <stdint.h>
#include <gb/gb.h>

/* Intrepid console font tiles (assets/intrepid.png, 96 tiles, make gfx).
 * Lives in bank 10 (first use of the free 10..15 range, AGENTS.md 55.5):
 * bank 5 (world bank) is full, and the sole reader (ui_sprite_init,
 * fixed bank) already streams it through banked_copy(), which takes any
 * bank 0..15 via the MBC5 low byte with no assembly change.  Pure data:
 * no code moves, so no banked-body self-containment rules apply. */
const uint8_t g_intrepid_font_tiles[1536] = {
#include "gfx/intrepid_font_tiles.inc"
};
