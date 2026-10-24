#pragma bank 5

#include "scenarios.h"
#include "telemetry.h"
#include "banked.h"

/* ── Banked scenario-descriptor apply, bank-5 body ──────────────────
 * Moved out of scenario_load_state() (src/debug/scenarios.c) so the
 * debug fixed bank keeps headroom under 0x8000 (AGENTS.md §52.18,
 * docs/fixed-bank-move-out.md candidate 1).  The fixed wrapper keeps
 * its signature and orchestration (header parse, game_new_game,
 * world_init/world_load_map, scenario_begin, flags, dialogue/battle
 * entry, render reset, snapshot); this body applies ONLY the pure
 * GameState descriptor sections (variables, currency, party,
 * collection, deck, persistent world, progression) as direct WRAM
 * writes with no mechanic calls and no telemetry (AGENTS.md §53.3
 * setup semantics: the old inventory/progression paths called
 * deck_collection_add/progression_ensure, which are fixed-bank
 * functions a banked body must not call per §52.11.1 -- the direct
 * writes below match the deck section's existing direct-write
 * precedent.  One deliberate difference: collection writes bypass the
 * max_copies gate (e.g. cards_menu_toggle stages 3 IRON_RING against a
 * cap of 1, which the old path silently rejected).  That fixture's
 * description and deck section both intend 3 rings, so the direct
 * write matches author intent; every other inventory fixture is
 * within cap).
 *
 * Self-contained: reads g_scen_state_buf, writes g_game.state, uses
 * only its own bank-local code.  No fixed-bank calls, no
 * banked_copy/banked_call_run (non-reentrant, and the copy trampoline
 * restores home bank 1), no di/ei (music-clock stall, §52.11.3), no
 * * / % (SDCC would pull div/mult into fixed _CODE, §52.18).  Struct
 * array indexing uses pointer increments (sizes 3/6 are not powers of
 * two) so no multiply is emitted. */

extern Game g_game;

void scenario_load_apply_banked(void)
{
    const uint8_t *b;
    const uint8_t *p;
    uint8_t i;
    uint8_t count;
    uint8_t vid;
    uint8_t cid;
    uint8_t lo;
    uint8_t hi;
    uint8_t id;
    uint8_t qty;
    uint8_t n;
    uint8_t t_type;
    uint8_t lvl;
    uint16_t t_id;
    uint16_t prog;
    uint8_t found;
    uint8_t k;

    b = g_scen_state_buf;
    if (b[0] != STATE_LOAD_DESC_VERSION) return;

    /* Variables: direct int16 writes (byte offset to avoid mult). */
    p = b + STATE_LOAD_DESC_VARIABLES_ENTRY_OFF;
    count = b[STATE_LOAD_DESC_VARIABLES_COUNT_OFF];
    for (i = 0; i < count; i++) {
        uint8_t idx;
        uint8_t *bdst;
        vid = *p++;
        lo = *p++;
        hi = *p++;
        if (vid >= 1 && vid <= MAX_STATE_VARIABLES) {
            idx = (uint8_t)(vid - 1);
            idx = (uint8_t)(idx << 1);
            bdst = (uint8_t *)g_game.state.variables.values;
            bdst[idx] = lo;
            bdst[(uint8_t)(idx + 1)] = hi;
        }
    }

    /* Currency: direct int16 writes. */
    p = b + STATE_LOAD_DESC_CURRENCY_ENTRY_OFF;
    count = b[STATE_LOAD_DESC_CURRENCY_COUNT_OFF];
    for (i = 0; i < count; i++) {
        uint8_t idx;
        uint8_t *bdst;
        cid = *p++;
        lo = *p++;
        hi = *p++;
        if (cid >= 1 && cid <= MAX_CURRENCIES) {
            idx = (uint8_t)(cid - 1);
            idx = (uint8_t)(idx << 1);
            bdst = (uint8_t *)g_game.state.currency.amount;
            bdst[idx] = lo;
            bdst[(uint8_t)(idx + 1)] = hi;
        }
    }

    /* Party: pointer increment (CharacterState is 3 bytes, not a
     * power of two -- indexing would emit a multiply). */
    {
        CharacterState *m;
        p = b + STATE_LOAD_DESC_PARTY_ENTRY_OFF;
        count = b[STATE_LOAD_DESC_PARTY_COUNT_OFF];
        m = &g_game.state.party.members[0];
        for (i = 0; i < count && i < MAX_PARTY_MEMBERS; i++) {
            m->id = (CharacterId)*p++;
            m->hp = *p++;
            m->max_hp = *p++;
            g_game.state.party.count = (uint8_t)(i + 1);
            m++;
        }
    }

    /* Collection: direct writes (no max_copies gate -- setup precedent
     * matches the deck section below; cards_menu_toggle intentionally
     * stages 3 IRON_RING over the cap of 1). */
    p = b + STATE_LOAD_DESC_INVENTORY_ENTRY_OFF;
    count = b[STATE_LOAD_DESC_INVENTORY_COUNT_OFF];
    for (i = 0; i < count; i++) {
        CardCollectionState *col;
        CardCollectionEntry *e;
        id = *p++;
        qty = *p++;
        if (id == 0 || qty == 0) continue;
        col = &g_game.state.cards.collection;
        e = &col->entries[0];
        found = 0;
        for (k = 0; k < col->count; k++) {
            if (e->id == id) {
                e->count = (uint8_t)(e->count + qty);
                found = 1;
                break;
            }
            e++;
        }
        if (!found) {
            if (col->count < MAX_CARD_COLLECTION) {
                e = &col->entries[0];
                for (k = 0; k < col->count; k++) e++;
                e->id = id;
                e->count = qty;
                col->count = (uint8_t)(col->count + 1);
            }
        }
    }

    /* Deck: direct copy when present (explicit empty allowed). */
    if (b[STATE_LOAD_DESC_DECK_PRESENT_OFF]) {
        uint8_t *dst;
        n = b[STATE_LOAD_DESC_DECK_COUNT_OFF];
        if (n > MAX_DECK_CARDS) n = MAX_DECK_CARDS;
        p = b + STATE_LOAD_DESC_DECK_ENTRY_OFF;
        dst = g_game.state.cards.deck.cards;
        for (i = 0; i < n; i++) {
            *dst++ = *p++;
        }
        g_game.state.cards.deck.count = n;
    }

    /* Persistent world actors: pointer increment (3-byte entries). */
    {
        PersistentActorState *w;
        p = b + STATE_LOAD_DESC_WORLD_ENTRY_OFF;
        count = b[STATE_LOAD_DESC_WORLD_COUNT_OFF];
        w = &g_game.state.world.actors[0];
        for (i = 0; i < count; i++) {
            lo = *p++;
            hi = *p++;
            w->actor_id = (ActorId)((uint16_t)lo | ((uint16_t)hi << 8));
            w->state = *p++;
            g_game.state.world.count = (uint8_t)(i + 1);
            w++;
        }
    }

    /* Progression: inline prog_get_or_add (pointer increments only). */
    p = b + STATE_LOAD_DESC_PROGRESSION_ENTRY_OFF;
    count = b[STATE_LOAD_DESC_PROGRESSION_COUNT_OFF];
    for (i = 0; i < count; i++) {
        ProgressionStore *store;
        ProgressionEntry *cur;
        ProgressionEntry *slot;
        t_type = *p++;
        lo = *p++;
        hi = *p++;
        t_id = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
        lvl = *p++;
        lo = *p++;
        hi = *p++;
        prog = (uint16_t)((uint16_t)lo | ((uint16_t)hi << 8));
        if (t_type == 0) continue;
        store = &g_game.state.progression;
        cur = &store->entries[0];
        slot = 0;
        for (k = 0; k < store->count; k++) {
            if (cur->target.type == t_type && cur->target.id == t_id) {
                slot = cur;
                break;
            }
            cur++;
        }
        if (slot == 0) {
            if (store->count >= MAX_PROGRESSION_TARGETS) continue;
            slot = &store->entries[0];
            for (k = 0; k < store->count; k++) slot++;
            slot->target.type = t_type;
            slot->target.id = t_id;
            slot->state.level = 1;
            slot->state.progress = 0;
            store->count = (uint8_t)(store->count + 1);
        }
        slot->state.level = lvl;
        slot->state.progress = prog;
    }
}
