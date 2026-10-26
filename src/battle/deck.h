#ifndef DECK_H
#define DECK_H

#include "card.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_DECK_SIZE 20
#define ENEMY_DECK_SIZE 8

typedef struct {
    Card cards[MAX_DECK_SIZE];
    uint8_t count;
    uint8_t draw_idx;
    Card discard[MAX_DECK_SIZE];
    uint8_t discard_count;
} Deck;

/* Initialize standard starter deck (deterministic fixed order) */
void deck_init_default(Deck *d);

/* Banked no-arg body (ROM bank 2) dispatched by deck_init_default(). */
void deck_init_default_banked(void);

/* Draw next card from the draw pile.  Never reshuffles: a dry pile yields
 * the phantom-card safety net (reshuffling is an explicit battle-level
 * turn, see battle.c). */
void deck_draw(Deck *d, Card *out_card);

/* Fold the undrawn remainder cards[draw_idx..count) into the discard pile,
 * then move the combined pile back into the draw pile (Fisher-Yates via
 * rng_next) and reset the draw index.  The fold makes the refill lossless:
 * a mid-refill reshuffle preserves the leftover instead of deleting it.
 * No-op when the discard pile is empty. */
void deck_reshuffle(Deck *d);

/* Banked no-arg body (ROM bank 7) dispatched by deck_reshuffle(). */
void deck_reshuffle_banked(void);

/* Add a card to the discard pile. */
void deck_discard(Deck *d, const Card *c);

/* Enemy card: the only fields enemy AI draws ever read (battle.c takes
 * .value as incoming damage, battle_defend_content.c takes the
 * .status_id/.status_chance rider).  Enemies never pay .cost, never
 * resolve .effect, and never deplete .uses_remaining -- those live only
 * on player Cards.  4 bytes instead of 8. */
typedef struct {
    uint8_t type;           /* BattleCardType */
    uint8_t value;          /* incoming damage when drawn */
    uint8_t status_id;      /* on-hit rider (STATUS_NONE = none) */
    uint8_t status_chance;  /* rider land chance in 1/255 units */
} EnemyCard;

/* Enemy compact deck: small fixed-size deck for enemy AI card draws. */
typedef struct {
    EnemyCard cards[ENEMY_DECK_SIZE];
    uint8_t count;
    uint8_t draw_idx;
} EnemyCompactDeck;

/* Banked no-arg body (ROM bank 2) dispatched by enemy_deck_setup(). */
void enemy_deck_setup_banked(void);

/* Set up the enemy deck for the current battle via banked call. */
void enemy_deck_setup(void);

#endif /* DECK_H */
