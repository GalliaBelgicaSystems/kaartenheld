#pragma bank 2

#include "deck.h"
#include "banked.h"
#include "rng.h"
#include "rpg/cards.h"
#include "rpg/status.h"

/* Banked body of deck_init_default() (see deck.c).  Lives in ROM bank 2 and
 * runs through the WRAM banked-call trampoline so the starter-deck unpacker
 * does not consume the fixed-bank _CODE budget.
 * Self-contained: it reads only the staged Deck pointer (ptr_a), the
 * generated starter table (same bank: direct read, like
 * battle_init_deck_banked's g_card_defs reads) and the registered card
 * catalog (bank 2, direct read).  It never calls fixed-bank code
 * (see src/core/banked.h). */

/* Generated hero starter deck (screens/hero.json, compiled by
 * battle_compile.py into src/game/hero_content.c, bank 2).  Declared
 * locally (not via a game-layer header) so this engine file keeps its
 * generic dependency direction (AGENTS.md 55.1); the table shape is the
 * contract: count byte + ordered CardIds in exact draw-pile order. */
extern const uint8_t g_hero_starter_deck_count;
extern const uint8_t g_hero_starter_deck_ids[];

void deck_init_default_banked(void)
{
    Deck *d = (Deck *)g_bk_ptr_a;
    uint8_t i, j;
    uint8_t n;

    if (!d) return;
    n = g_hero_starter_deck_count;
    if (n > MAX_DECK_SIZE) n = MAX_DECK_SIZE;
    d->count = n;
    d->draw_idx = 0;
    d->discard_count = 0;
    for (i = 0; i < n; i++) {
        /* Same row resolution as battle_init_deck_banked() for the same
         * ids, so the fallback deck is byte-identical to a granted deck
         * built from the same starter table (opening hand identical
         * whether battles run on real or fallback state). */
        const CardDefinition *def = (const CardDefinition *)0;
        for (j = 0; j < g_card_defs_count; j++) {
            if (g_card_defs[j].id == g_hero_starter_deck_ids[i]) {
                def = &g_card_defs[j];
                break;
            }
        }
        if (def) {
            d->cards[i].type = def->battle_type;
            d->cards[i].value = def->power;
            d->cards[i].uses_remaining =
                (def->uses_per_battle == 0) ? 0xFF : def->uses_per_battle;
            d->cards[i].cost = def->cost;
            d->cards[i].effect = def->effect;
            d->cards[i].status_id = def->status_id;
            d->cards[i].status_chance = def->status_chance;
            d->cards[i].ring =
                (def->battle_type == BATTLE_CARD_TYPE_HEAL) ? 1 : 0;
        } else {
            /* Unknown id: safety-net sword, mirroring the phantom draw. */
            d->cards[i].type = BATTLE_CARD_TYPE_SWORD;
            d->cards[i].value = 2;
            d->cards[i].uses_remaining = 0xFF;
            d->cards[i].cost = 1;
            d->cards[i].effect = CARD_EFFECT_DAMAGE_TARGET;
            d->cards[i].status_id = STATUS_NONE;
            d->cards[i].status_chance = 0;
            d->cards[i].ring = 0;
        }
    }
}

/* deck_reshuffle_banked and its card_copy/swap helpers used to live here;
 * they moved to deck_banked.c (ROM bank 3) when bank 2 ran out of room
 * for the reshuffle's remainder fold.  This file keeps only the starter
 * deck unpacker, which must stay in bank 2 (it reads the bank-2 card
 * catalog directly). */
