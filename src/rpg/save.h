#ifndef RPG_SAVE_H
#define RPG_SAVE_H

#include <stdint.h>
#include <stdbool.h>
#include "rpg/state.h"

/* Battery-backed SRAM save of the canonical GameState.  The format is
 * versioned ({magic, version, checksum, state}) so future saves can be
 * migrated; see docs/save-format.md. */

#define SAVE_MAGIC   0x5247   /* "GR" */
#define SAVE_VERSION 3
#define SAVE_SLOT_COUNT 3

/* SRAM save geometry shared by save.c and the bank-3 body.  Slots are
 * 1 KB apart: GameState reached ~806 bytes with the loot collection. */
#define SAVE_SRAM_BASE          0xA000
#define SAVE_SRAM_STATE_OFF     0x0004
#define SAVE_SLOT_STRIDE        0x0400

/* Persist the canonical state to a specific SRAM slot (0..2 for Slot 1..3). Returns true on success. */
bool save_game_slot(uint8_t slot, const GameState *state);

/* Restore the canonical state from a specific SRAM slot (0..2). Returns false if empty/corrupted. */
bool load_game_slot(uint8_t slot, GameState *state);

/* True when specified SRAM slot holds a valid save for this build. */
/* Bank-3 body (src/rpg/save_banked.c) dispatched by all three wrappers. */
/* Lives in ROM (const, other TU) precisely so SDCC cannot fold it. */
extern const uint16_t g_save_state_capacity;
void save_op_banked(void);

bool save_present_slot(uint8_t slot);

#endif /* RPG_SAVE_H */
