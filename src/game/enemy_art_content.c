#pragma bank 4
#pragma disable_warning 110

#include "content.h"
#include "actor.h"

/* Game-layer combat-art mapping (bank 4, co-located with the tables it
 * names).  Maps a BattleId to the enemy-type row id whose combat art the
 * battle draws.  Trios share their base type's row; BATTLE_NONE (boss)
 * resolves to the boss row; unknown battles yield 0 (text fallback in
 * every slot).
 *
 * Called directly by battle_art_load_banked (same bank: plain call, no
 * trampoline) so the fixed bank pays nothing for the mapping (§52.18).
 * New BattleIds get a case here when content adds them. */
const char *game_battle_enemy_type_id(uint8_t battle_id)
{
    switch (battle_id) {
        case BATTLE_SLIME:
        case BATTLE_SLIME_TRIO:
            return "slime";
        case BATTLE_BAT:
            return "bat";
        case BATTLE_NONE:
            return "slime_lord";
        case BATTLE_MIMIC:
            return "mimic";
        case BATTLE_KOBOLD:
            return "kobold";
        case BATTLE_SPIDER:
            return "spider";
        default:
            return 0;
    }
}
