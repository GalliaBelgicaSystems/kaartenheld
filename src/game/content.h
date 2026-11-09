#ifndef GAME_CONTENT_H
#define GAME_CONTENT_H

#include "rpg/state.h"
#include "rpg/progression.h"
#include "screen.h"

/* The game layer: everything that makes THIS game (content tables, named
 * ids, stat derivation, initial state, post-battle decisions).  The engine
 * (src/rpg, src/core, src/world, src/battle, src/ui) never references these
 * functions directly except through the hooks it is designed to call. */

/* Register all game content with the engine: story flag bound, event table,
 * dialogue table, and per-map actor tables.  Must run before gameplay. */
void game_content_init(void);

/* Build the canonical persistent state for a brand-new game (hero party,
 * starting scene/position, chapter variable, starting gold). */
void game_new_game(GameState *state);

/* Game-specific consequence of a generic progression level-up. */
void game_on_level_up(GameState *state, ProgressionTarget target,
                      const ProgressionAddResult *result);

/* Decide the next screen after a battle victory (e.g. the ending once the
 * final boss is defeated, otherwise the overworld).  Called by the shared
 * battle screen. */
ScreenId game_screen_after_victory(const Game *g);

/* Post-tileset-load art fixup (fixed-bank dispatch in
 * src/game/skeleton_art.c, VRAM body in src/game/skeleton_swap_banked.c
 * ROM bank 11).  Called by the engine's ui_load_tileset() wrapper after
 * every VRAM tileset load, and by the bone-grant path mid-scene: with
 * BONE_TAKEN set on an underground tileset, VRAM slot 30 is repainted
 * with the slot-31 no-bone art (both tiles share TileType 254 -- the
 * u8 space is spent, so no map-value change is possible).  No-op
 * otherwise.  Engine-to-game hook: the engine never branches on game
 * ids itself. */
void game_after_tileset_load(void);

/* Bank-11 VRAM body behind game_after_tileset_load(): slot-31 to
 * slot-30 copy when BONE_TAKEN is set on an underground tileset.
 * Dispatched through the WRAM trampoline; never called directly. */
void skeleton_swap_banked(void);

/* Despair white fade-out (src/game/despair_fade_banked.c, ROM bank 10):
 * whites the current palettes out.  Dispatched from
 * game_slime_despair_outro() on the DEFEATED result screen.
 * Self-contained (palette regs + LY only); the call site runs LCD-on. */
void game_despair_fade_out_banked(void);

/* Slime-lord desperation outro (unwinnable final battle, game-owned flow):
 * called by the shared battle screen's defeat exit when g_slime_despair is set.
 * Opens the Lord of Slimes defeat dialogue in the Throne Room. */
void game_slime_despair_outro(void);

/* Transitions from the Lord of Slimes defeat dialogue to the white fade-out,
 * loads the wake scene, and opens strange_wake. */
void game_slime_despair_wake(void);

/* Per-frame tutorial tick for Carl's spar: stages each coaching dialogue
 * box once (attack, defend, poker, flee, then one looping jeer per
 * player attack) for game_update's post-tick opener.  Called at the top
 * of battle_screen_update(); returns true when a box was staged (the
 * caller yields the frame).  Never fires for other battles. */
bool game_carl_tutorial_update(Game *g);

/* Victory loot drop (docs/loot.md §17/§34.5): runs the bank-3
 * profile + roll body and returns the derived CardId (always drops).
 * Uses the isolated loot RNG only -- never consumes the shared stream. */
uint8_t game_loot_drop(uint8_t battle_type);

/* Battle -> enemy rank R0-R6 (src/game/loot_gate_banked.c, ROM bank 9
 * with the rank table): g_bk_byte_a = battle id, result in g_bk_byte_b.
 * Fixed-bank victory code reports it as LOOT_CARD_ADDED d2 (no WRAM
 * growth, docs/enemies_ranking.md §4.3). */
void game_loot_rank_banked(void);

/* Unwinnable-boss desperation check (src/game/despair_banked.c, ROM
 * bank 8): re-derives the engine's g_despair_slot cache from the game
 * battle table, floors the boss HP at 1, stages the banner + message
 * strings into the engine WRAM buffers, sets g_slime_despair and
 * reports 1 in g_bk_byte_c on trigger (0 otherwise).  The engine
 * dispatches this after every damage source (attack tail, per-frame
 * battle_update) and opens the DESPAIR hold itself. */
void game_despair_check_banked(void);

/* Desperation reprisal message (same file, ROM bank 8): restages the
 * row buffers with the spell text ("L. OF SLIMES CASTS" / "POWERFUL
 * SPELL!"), read by the bank-3 painters through the despair-defeat
 * branch.  Dispatched once by the battle screen's A-press handler
 * before it lands the scripted 99 damage. */
void game_despair_reprisal_banked(void);

/* Banked body (bank 4): loads the active battle screen's HUD layout from
 * g_battle_screens into WRAM g_battle_hud.  Called via banked_call_run(). */
void battle_hud_load_banked(void);

/* Game-layer combat-art mapping (src/game/enemy_art_content.c, bank 4):
 * BattleId -> enemy-type row id ("slime", "bat", ...), 0 when no row
 * claims the battle.  Called directly by battle_art_load_banked (same
 * bank, plain call) so the fixed bank pays nothing for the mapping. */
const char *game_battle_enemy_type_id(uint8_t battle_id);

/* Content registration helpers, implemented in the content modules. */
void game_events_register(void);
void game_dialogue_register(void);
void game_actors_register(void);
void game_cards_register(void);

/* Hero starter deck (src/game/hero_content.c, bank 2, generated by
 * battle_compile.py from screens/hero.json): ordered draw-pile CardIds
 * plus count.  Fixed-bank readers must use banked_copy (bank 2); bank-2
 * bodies may read the table directly. */
extern const uint8_t g_hero_starter_deck_count;
extern const uint8_t g_hero_starter_deck_ids[];

#endif /* GAME_CONTENT_H */
