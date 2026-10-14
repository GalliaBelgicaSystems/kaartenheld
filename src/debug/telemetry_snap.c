#pragma bank 2

#include "telemetry.h"
#include "game.h"

/* Banked body of debug_state_snapshot() (see telemetry.c).  Lives in ROM
 * bank 2 and runs through the WRAM banked-call trampoline so the ~460-byte
 * extended-snapshot builder does not consume the fixed-bank _CODE budget.
 * It must stay self-contained (no calls into fixed-bank code): it reads only
 * the WRAM globals g_game / g_state_snap_buf and its own static snap_copy. */

static void snap_copy(uint8_t *dst, const void *src, uint8_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *dst++ = *s++;
}

void debug_state_snapshot_banked(void)
{
    const GameState *st = &g_game.state;
    uint8_t i;
    uint8_t *p;
    uint8_t *b = g_state_snap_buf;

    b[0] = STATE_SNAP_VERSION_BYTE;
    snap_copy(b + STATE_SNAP_FLAGS_OFFSET, st->flags.bytes, MAX_STATE_FLAGS / 8);
    snap_copy(b + STATE_SNAP_VARIABLES_OFFSET, st->variables.values, MAX_STATE_VARIABLES * 2);

    b[STATE_SNAP_CURRENCY_COUNT_OFF] = MAX_CURRENCIES;
    p = b + STATE_SNAP_CURRENCY_ENTRY_OFF;
    /* Single-currency game (gold only): unrolled single entry. */
    *p++ = 1;
    snap_copy(p, &st->currency.amount[0], 2);

    b[STATE_SNAP_PARTY_OFFSET] = st->party.count;
    p = b + STATE_SNAP_PARTY_OFFSET + 1;
    for (i = 0; i < st->party.count && i < MAX_PARTY_MEMBERS; i++) {
        *p++ = (uint8_t)st->party.members[i].id;
        *p++ = st->party.members[i].hp;
        *p++ = st->party.members[i].max_hp;
    }

    b[STATE_SNAP_INVENTORY_OFFSET] = st->cards.collection.count;
    p = b + STATE_SNAP_INVENTORY_OFFSET + 1;
    for (i = 0; i < st->cards.collection.count && i < MAX_CARD_COLLECTION; i++) {
        *p++ = st->cards.collection.entries[i].id;
        *p++ = st->cards.collection.entries[i].count;
    }

    b[STATE_SNAP_WORLD_OFFSET] = st->world.count;
    p = b + STATE_SNAP_WORLD_OFFSET + 1;
    for (i = 0; i < st->world.count && i < 16; i++) {
        snap_copy(p, &st->world.actors[i].actor_id, 2);
        p += 2;
        *p++ = st->world.actors[i].state;
    }

    b[STATE_SNAP_PROGRESSION_COUNT_OFF] = st->progression.count;
    p = b + STATE_SNAP_PROGRESSION_ENTRY_OFF;
    for (i = 0; i < st->progression.count && i < MAX_PROGRESSION_TARGETS; i++) {
        *p++ = st->progression.entries[i].target.type;
        snap_copy(p, &st->progression.entries[i].target.id, 2);
        p += 2;
        *p++ = st->progression.entries[i].state.level;
        snap_copy(p, &st->progression.entries[i].state.progress, 2);
        p += 2;
    }

    b[STATE_SNAP_EQUIPMENT_OFF] = 0;

    b[STATE_SNAP_SCROLL_X_OFF]     = g_game.world.scroll_x;
    b[STATE_SNAP_SCROLL_Y_OFF]     = g_game.world.scroll_y;
    b[STATE_SNAP_WORLD_WIDTH_OFF]  = g_game.world.width;
    b[STATE_SNAP_WORLD_HEIGHT_OFF] = g_game.world.height;
    b[STATE_SNAP_CAMERA_PX_X_OFF]  = g_game.world.camera_px_x;
    b[STATE_SNAP_CAMERA_PX_Y_OFF]  = g_game.world.camera_px_y;

    b[STATE_SNAP_DECK_COUNT_OFF] = st->cards.deck.count;
    p = b + STATE_SNAP_DECK_COUNT_OFF + 1;
    for (i = 0; i < st->cards.deck.count && i < MAX_DECK_CARDS; i++) {
        *p++ = st->cards.deck.cards[i];
    }
}
