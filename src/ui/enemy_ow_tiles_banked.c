#ifdef DEBUG_BUILD
#pragma bank 5
#else
#pragma bank 4
#endif

/* Shared per-enemy overworld sprite blob (368 B of OAM tile data,
 * generated from the enemy-type art by battle_compile.py into
 * gfx/enemy_ow_tiles.h).  Split out of ui_world_sprite_banked.c so the
 * nearly-full debug bank 4 keeps headroom (see make memmap): data can
 * live in any bank because the only reader (ui_sprite_init in ui.c)
 * fetches it with an explicit banked_copy source bank (ENEMY_OW_BANK
 * there), unlike banked CODE bodies which must share their tables'
 * bank.  Moving data never perturbs codegen (AGENTS.md 52.19 is about
 * banked-body placement). */

#include <stdint.h>
#include "gfx/enemy_ow_tiles.h"
