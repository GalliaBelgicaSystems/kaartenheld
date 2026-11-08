#include "content.h"
#include "banked.h"

/* Tunnel skeleton bone art: fixed-bank dispatch only.  The swap body
 * lives in ROM bank 11 (skeleton_swap_banked.c) -- banks 5 and 8 are
 * full in the debug/release links, and a banked body can neither be
 * reached from the fixed grant path without a second body nor read
 * another bank's ROM (AGENTS.md §52.11.1), so the body copies VRAM
 * slot 31 to slot 30 instead of reading the sheet (both skeleton
 * tiles share TileType 254; the u8 space is spent, world.h).  Thin
 * wrapper by structure (AGENTS.md §52.18): both call sites share it. */
void game_after_tileset_load(void)
{
    g_bk_call_bank = 11;
    g_bk_call_target = (uint16_t)&skeleton_swap_banked;
    banked_call_run();
}
