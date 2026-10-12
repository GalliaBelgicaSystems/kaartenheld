#pragma bank 8

#include <stdint.h>

/* Shared overworld enemy/chest OAM blob (assets/enemy_sprites.png via
 * battle_compile.py --ow-coords).  Lives in bank 8 (not bank 4 with the
 * sprite body): the boot loader streams it into OAM through banked_copy,
 * so the array bank is just a number to the reader.  Offsets (ow_tile)
 * are unaffected by the bank -- see battle_types.c. */
#include "gfx/enemy_ow_tiles.h"
