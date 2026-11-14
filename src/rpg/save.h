#ifndef RPG_SAVE_H
#define RPG_SAVE_H

#include <stdint.h>
#include <stdbool.h>
#include "rpg/state.h"

/* Battery-backed SRAM save of the canonical GameState.  The format is
 * versioned ({magic, version, checksum, state}) so future saves can be
 * migrated; see docs/save-format.md. */

#define SAVE_MAGIC   0x5247   /* "GR" */
/* v6: CARD_FIRE_SWORD renamed to plain CARD_BRONZE_SWORD on the same id
 * (0x43) -- a v5 save's fire sword would silently load as plain, so v5
 * saves are rejected outright (no migration can distinguish the two).
 * v5: 50-row collection (was 12 rows in v3 -- the shorter state fails
 * the checksum against the grown struct, so pre-v4 saves read as
 * empty instead of loading truncated) + appended EconomyState
 * (docs/economy.md §6.3, the v4->v5 bump). */
#define SAVE_VERSION 6
#define SAVE_SLOT_COUNT 3

/* SRAM save geometry shared by the SRAM bodies.  Slots are
 * 1 KB apart: GameState reached ~806 bytes with the loot collection. */
#define SAVE_SRAM_BASE          0xA000
#define SAVE_SRAM_STATE_OFF     0x0004
#define SAVE_SLOT_STRIDE        0x0400

/* Persist the canonical state to a specific SRAM slot (0..2 for Slot 1..3). Returns true on success. */
bool save_game_slot(uint8_t slot, const GameState *state);

/* Restore the canonical state from a specific SRAM slot (0..2). Returns false if empty/corrupted. */
bool load_game_slot(uint8_t slot, GameState *state);

/* Lives in ROM (const, other TU) precisely so SDCC cannot fold it. */
extern const uint16_t g_save_state_capacity;
/* Bank-8 SRAM bodies (src/rpg/save_banked.c). */
void save_op_banked(void);

/* Bank-12 save-flow bodies (src/rpg/save_flow_banked.c).
 * save_commit_active_banked: writes g_game.save_slot_index with the
 * live state + inline GAME_SAVED ring emit (debug only).  Shared by
 * the intro claim and the upkeep flush (same-bank direct call, no
 * trampoline).  save_upkeep_banked: per-frame flush of a staged
 * autosave request (called after game_update()'s scene sync).
 * save_menu_update_banked: picker update (in: ptr_a = Game*; out:
 * byte_b = action 0 nothing / 1 start intro / 2 load slot in byte_c
 * / 3 leave; the fixed applier owns screens + the LOAD tail). */
void save_commit_active_banked(void);
void save_upkeep_banked(void);
void save_menu_update_banked(void);

/* Persist the active slot for a fresh playthrough (intro end).
 * Thin fixed-bank wrapper (AGENTS.md §52.18). */
void save_claim_active(void);

#endif /* RPG_SAVE_H */
