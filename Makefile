# Makefile for Game Boy ROM development with GBDK-4 and RGBDS

# Delete a target when its recipe fails: the link rules fail loudly on
# rgblink bank-overflow warnings (AGENTS.md 52.18) AFTER the linker has
# already written the ROM, so without this the corrupt .gb survives and
# the next make reports "up to date" and exits 0 on top of it.
.DELETE_ON_ERROR:

CC = lcc
RGBFIX = rgbfix

BUILD_DIR = build
SRC_DIR = src

TARGET = $(BUILD_DIR)/kaartenheld.gb
TARGET_DEBUG = $(BUILD_DIR)/kaartenheld_debug.gb

JOBS ?= auto

# sdldgb links entire archives; trimmed closures keep the non-bankable
# _HOME area below 0x8000 on MBC5 (see docs/roadmap.md state foundation).
GB_LITE = $(BUILD_DIR)/gb_lite.lib
SM83_LITE = $(BUILD_DIR)/sm83_lite.lib

GENERATED_MUSIC_DIR = generated/music
RGBASM_HUGE ?= $(shell command -v rgbasm-huge 2>/dev/null || echo rgbasm)
RGB2SDAS = python3 tools/rgb2sdas.py
UGE2SOURCE = uge2source

INCLUDES = -I$(SRC_DIR) -I$(SRC_DIR)/core -I$(SRC_DIR)/world -I$(SRC_DIR)/battle -I$(SRC_DIR)/input -I$(SRC_DIR)/audio -I$(SRC_DIR)/ui -I$(SRC_DIR)/debug -I$(SRC_DIR)/screens -I$(SRC_DIR)/game -Ilib/hUGEDriver/include -I$(GENERATED_MUSIC_DIR) -I$(GENERATED_SFX_DIR) -I$(GENERATED_TILES_DIR)

ALL_SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/*/*.c)
BANK5_EARLY_SRCS = $(SRC_DIR)/world/scene_load.c
SRCS = $(BANK5_EARLY_SRCS) $(filter-out $(BANK5_EARLY_SRCS),$(ALL_SRCS))
# Frozen harness-test content (tools/scenarios/fixtures/levels/, compiled
# with --bank 4 into src/game/*_content_test.c): compiled ONLY into the
# debug (harness) build so the scenario suite sees a stable world while
# real content (levels/) evolves freely.  The release build must never
# see these symbols (same g_scenes/g_actor_tables names).
TEST_CONTENT_SRCS = $(SRC_DIR)/game/scenes_content_test.c $(SRC_DIR)/game/actors_content_test.c
CONTENT_SRCS = $(SRC_DIR)/game/scenes_content.c $(SRC_DIR)/game/actors_content.c
SRCS := $(filter-out $(TEST_CONTENT_SRCS),$(SRCS))
# Release-only source: the title-logo data+loader must stay in bank 5 for
# the harness build (layout-pinned; AGENTS.md 52.19) but moves to bank 4 in
# release to free the tight world bank for expanded content.  Excluded from
# the debug link (the debug copy lives in tiles_content.c).
RELEASE_ONLY_SRCS = $(SRC_DIR)/screens/title_logo_content.c

DEBUG_SRCS = $(filter-out $(CONTENT_SRCS) $(RELEASE_ONLY_SRCS),$(SRCS)) $(TEST_CONTENT_SRCS)

# Debug-harness-only sources excluded from the release ROM.
# telemetry.c IS needed by gameplay (game.c/world.c emit events);
# telemetry_snap.c is the banked extended-snapshot builder (debug-only).
DEBUG_ONLY_SRCS = $(SRC_DIR)/debug/scenarios.c $(SRC_DIR)/debug/assertions.c $(SRC_DIR)/debug/telemetry_snap.c $(SRC_DIR)/debug/snapshot_banked.c
RELEASE_SRCS = $(filter-out $(DEBUG_ONLY_SRCS),$(SRCS))

MUSIC_SRCS = $(GENERATED_MUSIC_DIR)/battle.c $(GENERATED_MUSIC_DIR)/desolate_landscape.c $(GENERATED_MUSIC_DIR)/forest.c $(GENERATED_MUSIC_DIR)/boss_fight.c $(GENERATED_MUSIC_DIR)/village.c $(GENERATED_MUSIC_DIR)/castle.c $(GENERATED_MUSIC_DIR)/mimic.c $(GENERATED_MUSIC_DIR)/title.c $(GENERATED_MUSIC_DIR)/victory.c
GENERATED_SFX_DIR = generated/sfx
# Explicit list (not wildcard): asset names contain spaces, which make
# would split. Escaped following the assets/music rules' convention.
SFX_UGE = assets/sfx/sfx\ accept.uge assets/sfx/sfx\ back.uge \
          assets/sfx/sfx\ block.uge assets/sfx/sfx\ cursor.uge \
          assets/sfx/sfx\ hit.uge assets/sfx/sfx\ hit2.uge
# screens/sfx.json may map any SFX id to any .uge (assets/sfx or
# assets/music), so the tables depend on the registry plus both pools.
SFX_REGISTRY = screens/sfx.json
SFX_ALL_UGE = $(SFX_UGE) \
              assets/music/Battle\ BGM.uge assets/music/Boss\ fight.uge \
              assets/music/castle.uge assets/music/desolate_landscape.uge \
              assets/music/Forest.uge assets/music/Mimic.uge \
              assets/music/title\ long.uge assets/music/title\ short.uge \
              assets/music/Village.uge
# Both C files come from one transcriber run (plus sfx_tables.h); the
# recipe is deterministic, so a double invocation is a harmless no-op.
SFX_TABLES = $(GENERATED_SFX_DIR)/sfx_tables.c $(GENERATED_SFX_DIR)/sfx_index.c
MUSIC_OBJS = $(patsubst $(GENERATED_MUSIC_DIR)/%.c,$(BUILD_DIR)/music/%.o,$(MUSIC_SRCS))
MUSIC_OBJS_DEBUG = $(patsubst $(GENERATED_MUSIC_DIR)/%.c,$(BUILD_DIR)/debug/music/%.o,$(MUSIC_SRCS))

HUGEDRIVER_OBJ = $(BUILD_DIR)/lib/hUGEDriver.o
HUGEDRIVER_OBJ_DEBUG = $(BUILD_DIR)/debug/lib/hUGEDriver.o
# Second hUGE driver copy in bank 7 (renamed exports, see rules below) so
# the mimic song can live outside the full bank 6.  The driver reads song
# bytes through the mapped ROM window, so driver + song must share a bank.
HUGEDRIVER_B7_OBJ = $(BUILD_DIR)/lib/hUGEDriver_b7.o
HUGEDRIVER_B7_OBJ_DEBUG = $(BUILD_DIR)/debug/lib/hUGEDriver_b7.o

OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(RELEASE_SRCS)) $(MUSIC_OBJS) $(SFX_OBJS) $(HUGEDRIVER_OBJ) $(HUGEDRIVER_B7_OBJ)
OBJS_DEBUG = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/debug/%.o,$(DEBUG_SRCS)) $(MUSIC_OBJS_DEBUG) $(SFX_OBJS_DEBUG) $(HUGEDRIVER_OBJ_DEBUG) $(HUGEDRIVER_B7_OBJ_DEBUG)

# Emulator detection
EMULATOR ?= $(shell command -v pyboy 2>/dev/null || command -v sameboy 2>/dev/null || command -v mgba-sdl 2>/dev/null || command -v mgba-qt 2>/dev/null || command -v mgba 2>/dev/null || echo "")

.PHONY: all release debug run run-debug test test-harness test-scenario state roundtrip screenshot screenshots gifs verify-walkthrough parity lint memmap verify-oam verify-vram verify-scroll verify-music verify-endurance vram-check vram-text vram-dialogue gfx atlas atlas-check manifest palette-check tiles tiles-check reload-boss-tiles reload-boss-tiles-check levels-test levels-test-check doctor music music-preview sfx sfx-preview level levels levels-check screens screens-check dialogues dialogues-check shops shops-check entities entities-check registry-check editor clean

all: $(TARGET)

release: $(TARGET)

debug: $(TARGET_DEBUG)

# Compile-to-assembly warning pass.  -Wall cannot be part of the normal
# build: sdcc's --use-stdout pipeline corrupts the .asm stream when warnings
# are enabled (they leak into stdout).  Compiling with -S surfaces the same
# warnings without invoking the assembler.
# sfx is a prerequisite: the banked sfx_step.c includes the transcribed
# generated/sfx/sfx_tables.h, which does not exist on a fresh clone
# (generated/ is untracked) -- without it the ABI guard below fails.
lint: gfx tiles sfx $(SRCS)
	@ok=1; \
	for f in $(SRCS); do \
		out=$$($(CC) -S -Wf-Wall $(INCLUDES) -o /dev/null "$$f" 2>&1 || true); \
		if echo "$$out" | grep -q "warning"; then \
			echo "=== $$f ==="; echo "$$out" | grep "warning"; ok=0; \
		fi; \
	done; \
	CC="$(CC)" INCLUDES="$(INCLUDES)" sh tools/check_banked_copies.sh || ok=0; \
	if [ "$$ok" = "1" ]; then echo "lint: no warnings"; else exit 1; fi

# Regenerate GB tile data headers from PNG assets (docs/graphics.md pipeline).
# Deterministic: rerunning produces byte-identical output.  Requires Pillow,
# which the Nix dev shell provides.
GFX_OUT_DIR = $(SRC_DIR)/gfx

gfx: manifest
	@mkdir -p $(GFX_OUT_DIR)
	# ── Intrepid font ─────────────────────────────────────────────────────
	@python3 tools/png2gb.py assets/intrepid.png --name intrepid_font_tiles \
		--raw -o $(GFX_OUT_DIR)/intrepid_font_tiles.inc
	# ── Forest tileset (assets/forest-tile.png, 16 cols × 3 rows) ────────
	# Shade maps come from generated/tiles/shades/*.json (palette_compiler:
	# exact artist ramps per tile, no luminance guessing). Every rule below
	# takes --shade-map; off-ramp pixels use the nearest shade of the
	# tile's ramp and are listed in generated/tiles/ramp_mismatches.json.
	# Full 48-tile world sheet (g_tileset_forest)
	@python3 tools/png2gb.py assets/forest-tile.png --name rpg_forest_world_tiles \
		--shade-map generated/tiles/shades/forest.json \
		--raw -o $(GFX_OUT_DIR)/rpg_forest_world_tiles.inc
	# Floor tile: col 0, row 2
	@python3 tools/png2gb.py assets/forest-tile.png --name rpg_forest_floor \
		--shade-map generated/tiles/shades/forest.json --tile-coords "0,2" \
		--raw -o $(GFX_OUT_DIR)/rpg_forest_floor.inc
	# Treetop tile: col 12, row 0
	@python3 tools/png2gb.py assets/forest-tile.png --name rpg_forest_tree \
		--shade-map generated/tiles/shades/forest.json --tile-coords "12,0" \
		--raw -o $(GFX_OUT_DIR)/rpg_forest_tree.inc
	# Exit tile: col 8, row 2
	@python3 tools/png2gb.py assets/forest-tile.png --name rpg_forest_exit \
		--shade-map generated/tiles/shades/forest.json --tile-coords "8,2" \
		--raw -o $(GFX_OUT_DIR)/rpg_forest_exit.inc
	# Stump tiles TL,TR,BL,BR + mini (BR repeated): cols 14-15, rows 0-1
	@python3 tools/png2gb.py assets/forest-tile.png --name rpg_forest_stumps \
		--shade-map generated/tiles/shades/forest.json --tile-coords "14,0 15,0 14,1 15,1 15,1" \
		--raw -o $(GFX_OUT_DIR)/rpg_forest_stumps.inc
	# Chest sprite tile from forest-tile.png (tile 11,2 "treasure chest
	# forest"). Shade 0 of its ramp is the transparent key, matching the
	# sheet encoding, so no anchor flag is needed.
	@python3 tools/png2gb.py assets/forest-tile.png --name forest_chest_sprite_tile \
		--shade-map generated/tiles/shades/forest.json --tile-coords "11,2" \
		-o $(GFX_OUT_DIR)/forest_chest_sprite_tile.h
	# ── Battle enemy art (assets/battle_sprites.png, 3 cols × N rows) ────
	# Cell order comes from screens/combat_art/*.json (set order, frame0
	# then frame1 per set); the compiler also emits per-set blob offsets
	# into battle_types.c, so the loader needs no fixed set size.
	# Sheet layout: see tools/compose_battle_sprites.py.
	@python3 tools/png2gb.py assets/battle_sprites.png --name battle_enemy_art \
		--shade-map generated/tiles/shades/battle.json --tile-coords "$$(python3 tools/screen_compiler/battle_compile.py --gfx-coords)" \
		-o $(GFX_OUT_DIR)/battle_enemy_art.h
	# ── Shared overworld enemy sprites (assets/enemy_sprites.png) ──
	# One transparent-background sprite per enemy type, shared by every
	# world. Cell order comes from screens/enemy_types overworld.cells
	# (sorted enemy-id order); tiles load to OAM at ENEMY_OW_BASE (100).
	# Sheet layout: see tools/compose_enemy_sprites.py.
	@python3 tools/png2gb.py assets/enemy_sprites.png --name enemy_ow_tiles \
		--shade-map generated/tiles/shades/enemy_ow.json --tile-coords "$$(python3 tools/screen_compiler/battle_compile.py --ow-coords)" \
		-o $(GFX_OUT_DIR)/enemy_ow_tiles.h
	# ── Desolate landscape (assets/desolate-tile.png) ──
	# Full world sheet (g_tileset_desolate)
	@python3 tools/png2gb.py assets/desolate-tile.png --name rpg_desolate_world_tiles \
		--shade-map generated/tiles/shades/desolate_landscape.json \
		--raw -o $(GFX_OUT_DIR)/rpg_desolate_world_tiles.inc
	# 41-tile subset for scene terrain lookup (rows 0–2, cols 0–8 on row 2)
	@python3 tools/png2gb.py assets/desolate-tile.png --name rpg_desolate_tiles \
		--shade-map generated/tiles/shades/desolate_landscape.json --tile-coords "0,0 1,0 2,0 3,0 4,0 5,0 6,0 7,0 8,0 9,0 10,0 11,0 12,0 13,0 14,0 15,0 0,1 1,1 2,1 3,1 4,1 5,1 6,1 7,1 8,1 9,1 10,1 11,1 12,1 13,1 14,1 15,1 0,2 1,2 2,2 3,2 4,2 5,2 6,2 7,2 8,2" \
		--raw -o $(GFX_OUT_DIR)/rpg_desolate_tiles.inc
	# Player sprite tiles from assets/hero_sprites.png (hero frames 1 & 2)
	@python3 tools/png2gb.py assets/hero_sprites.png --name hero_desolate_sprite_tile \
		--shade-map generated/tiles/shades/hero_ow.json \
		-o $(GFX_OUT_DIR)/hero_desolate_sprite_tile.h
	# ── Castle tileset (assets/castle-tile.png, 9 cols × 3 rows) ─────────
	# The sheet packs 9-wide but VRAM slots pack 8-wide (vram_block index
	# order), so bytes AND palette entries follow vram-index order via
	# --tile-coords (slots 0-15; see palette_compiler). Sized by the rule.
	@python3 tools/png2gb.py assets/castle-tile.png --name rpg_castle_tiles \
		--shade-map generated/tiles/shades/castle.json --tile-coords "0,0 1,0 2,0 3,0 4,0 5,0 6,0 7,0 0,1 1,1 2,1 3,1 4,1 5,1 6,1 7,1" --raw -o $(GFX_OUT_DIR)/rpg_castle_tiles.inc
	# ── Village tileset (assets/village-tile.png, 16 cols × 3 rows) ────────
	# Full 48-tile world sheet (g_tileset_village). Arranged in SheetIndex
	# order (the tileset JSON's vram_block section numbering = scanning order).
	@python3 tools/png2gb.py assets/village-tile.png --name rpg_village_world_tiles \
		--shade-map generated/tiles/shades/village.json \
		--raw -o $(GFX_OUT_DIR)/rpg_village_world_tiles.inc
	# ── Battle hand-card frame (assets/card_frames.png, 3 cols × 10 rows) ──
	# 9 border/background tiles for the boxed battle-hand cards (TL TM TR /
	# L C R / BL BM BR); loaded to VRAM at UI_TILE_CARD_FRAME_BASE (118).
	@python3 tools/png2gb.py assets/card_frames.png --name card_frame_tiles \
		--shade-map generated/tiles/shades/card_frames.json -o $(GFX_OUT_DIR)/card_frame_tiles.h
	# ── Title logo (assets/title-red.png, 16 cols × 3 rows) ──────────────
	# Full 48-tile logo sheet, loaded into the world BG block (ids 128-175)
	# with CGB palette 1 on the title screen.  The level-editor Title Studio
	# previews the exact same image, so a copy is published under
	# public/tiles/title/ (mirrors the ROM 1:1; the CI drift diff covers it).
	@python3 tools/png2gb.py assets/title-red.png --name title_logo_tiles \
		--shade-map generated/tiles/shades/title.json --raw \
		-o $(GFX_OUT_DIR)/title_logo_tiles.inc
	@mkdir -p tools/level_editor/public/tiles/title
	@cp assets/title-red.png tools/level_editor/public/tiles/title/logo.png
	@echo "Title logo: src/gfx/title_logo_tiles.inc + public/tiles/title/logo.png"
	# ── Studio splash logo (assets/gallia_belgica_systems.png, 13x5) ──────
	# Fixed global shade map -> one CGB palette [white, red, blue, black]
	# rendered by the bank-2 splash body.  LLM-only content (no editor view).
	@python3 tools/screen_compiler/splash_logo_compile.py \
		-o $(GFX_OUT_DIR)/splash_logo_tiles.h



# Regenerate the asset atlas (docs/assets_atlas.md + src/gfx/asset_atlas.h
# + the banked .inc data).  Deterministic: rerunning produces byte-identical
# output.  See docs/assets_atlas.md and tools/asset_atlas.py.
atlas:
	@python3 tools/asset_atlas.py

# Drift check: Makefile gfx --tile-coords must match the atlas registry, and
# the generated artifacts must be in sync with the current source assets.
# Level compiler targets (docs/level-editor.md)
LEVEL ?= forest
level:
	@python3 tools/level_compiler/validate.py levels/$(LEVEL).json
	@python3 tools/level_compiler/compile.py --all --actors-bank 4 -o src/game/scenes_content.c
	@echo "Compiled level: $(LEVEL)"

levels:
	@python3 tools/level_compiler/validate.py levels/*.json
	@python3 tools/level_compiler/compile.py --all --actors-bank 4 -o src/game/scenes_content.c
	@echo "All levels compiled to src/game/scenes_content.c"

# JSON is the source of truth: committed C must equal fresh compile (no
# hand edits to generated files). decompile.py is a recovery/forensics
# tool, not a gate (see docs/level-editor.md Phase 15).
levels-check:
	@python3 tools/level_compiler/validate.py levels/*.json
	@python3 tools/level_compiler/compile.py --all --actors-bank 4 -o src/game/scenes_content.c --check

src/game/scenes_content.c src/world/scene_ids_generated.h &: $(wildcard levels/*.json)
	@python3 tools/level_compiler/compile.py --all --actors-bank 4 -o src/game/scenes_content.c
# ^ Also (re)generates src/world/scene_ids_generated.h (MAP_*/SCENE_* values
# from levels/registry.json) as a side effect; the wildcard above includes
# registry.json so id assignments trigger a rebuild.  --check verifies it too.

# Frozen harness-test fixtures (tools/scenarios/fixtures/levels/): the
# debug (harness) ROM links ONLY these, so real content edits can never
# break the scenario suite.  Compiled with --bank 4 (GAME_TEST_CONTENT_BANK)
# so the release budgets (banks 2/5) are untouched.  Committed C must equal
# fresh compile (no hand edits, same convention as the real content).
LEVELS_TEST_DIR = tools/scenarios/fixtures/levels

levels-test:
	@python3 tools/level_compiler/validate.py $(LEVELS_TEST_DIR)/*.json
	@python3 tools/level_compiler/compile.py $(LEVELS_TEST_DIR)/*.json --bank 4 \
		-o src/game/scenes_content_test.c --actors-output src/game/actors_content_test.c
	@echo "All test fixture levels compiled to src/game/scenes_content_test.c"

levels-test-check:
	@python3 tools/level_compiler/validate.py $(LEVELS_TEST_DIR)/*.json
	@python3 tools/level_compiler/compile.py $(LEVELS_TEST_DIR)/*.json --bank 4 \
		-o src/game/scenes_content_test.c --actors-output src/game/actors_content_test.c --check

src/game/scenes_content_test.c src/game/actors_content_test.c &: $(wildcard $(LEVELS_TEST_DIR)/*.json)
	@python3 tools/level_compiler/compile.py $(LEVELS_TEST_DIR)/*.json --bank 4 \
		-o src/game/scenes_content_test.c --actors-output src/game/actors_content_test.c

# Screen content compiler (docs/level-editor.md Phase 17): screens/*.json is
# the source of truth for title + battle mockup data, just as levels/*.json
# is for scenes.  Committed C must equal fresh compile (no hand edits).
screens:
	@python3 tools/screen_compiler/title_compile.py -o src/game/title_data.c screens/title.json
	@python3 tools/screen_compiler/battle_compile.py --all -o src/game/
	@python3 tools/screen_compiler/tutorial_compile.py --all -o src/screens/
	@echo "All screens compiled to src/game/{title_data,battle_screens,battle_types,battle_obj_tables,card_skin}.c + src/screens/tutorial_*_generated.h"

screens-check:
	@python3 tools/screen_compiler/title_compile.py --check
	@python3 tools/screen_compiler/battle_compile.py --all --check
	@python3 tools/screen_compiler/tutorial_compile.py --all --check

# Dialogue content: screens/dialogue/*.json is the source of truth for the
# NPC dialogue table (ids assign by sorted filename) and
# screens/tutorial.json for the title-menu slides.  Text is authored in
# the editor; flags/events/scenarios stay LLM-authored.
dialogues:
	@python3 tools/screen_compiler/dialogue_compile.py --all -o src/game/
	@echo "All dialogue compiled to src/game/{dialogue_content.c,dialogue_ids_generated.h}"

dialogues-check:
	@python3 tools/screen_compiler/dialogue_compile.py --all -o src/game/ --check
	@python3 tools/level_compiler/validate.py --dialogue-refs

# Shop content: screens/shops/<id>.json is the source of truth for
# g_shops[] (id = filename stem; items are CARD_* symbols).  Referenced by
# each actor's `shop` property.  The editor's Shop view edits the same JSON.
shops:
	@python3 tools/screen_compiler/shops_compile.py --all -o src/game/shops_content.c
	@echo "All shops compiled to src/game/shops_content.c"

shops-check:
	@python3 tools/screen_compiler/shops_compile.py --all --check
	@python3 tools/level_compiler/validate.py --shop-refs

# Entity types: screens/enemy_types/*.json + screens/entity_types/*.json are
# the single source of truth for the ENTITY_ID_* game range.  Adding a type
# in the editor regenerates src/game/entity_ids_generated.h with no C edit.
entities:
	@python3 tools/screen_compiler/entity_compile.py --all -o src/game/entity_ids_generated.h
	@echo "All entity types compiled to src/game/entity_ids_generated.h"

entities-check:
	@python3 tools/screen_compiler/entity_compile.py --all --check

# Registry invariants (levels/registry.json contract): versioning,
# append-only ids, never-reuse of retired ids, registry/file agreement.
# Locks the contract the editor's save/rename/delete operations satisfy.
registry-check:
	@python3 tools/test_registry.py

src/screens/tutorial_text_generated.h src/screens/tutorial_count_generated.h &: screens/tutorial.json
	@python3 tools/screen_compiler/tutorial_compile.py --all -o src/screens/

src/game/dialogue_content.c src/game/dialogue_ids_generated.h &: $(wildcard screens/dialogue/*.json)
	@python3 tools/screen_compiler/dialogue_compile.py --all -o src/game/

src/game/shops_content.c: $(wildcard screens/shops/*.json) tools/screen_compiler/shops_compile.py
	@python3 tools/screen_compiler/shops_compile.py --all -o src/game/shops_content.c

src/game/entity_ids_generated.h: $(wildcard screens/enemy_types/*.json) $(wildcard screens/entity_types/*.json) tools/screen_compiler/entity_compile.py tools/screen_compiler/entity_ids.py
	@python3 tools/screen_compiler/entity_compile.py --all -o src/game/entity_ids_generated.h

src/game/title_data.c: screens/title.json
	@python3 tools/screen_compiler/title_compile.py -o src/game/title_data.c screens/title.json

src/game/battle_screens.c src/game/battle_types.c src/game/card_skin.c: $(wildcard screens/battle/*.json) $(wildcard screens/enemy_types/*.json) screens/cards_skin.json
	@python3 tools/screen_compiler/battle_compile.py --all -o src/game/

# Extract tile images from source PNGs for the web editor (import_tileset.py)
extract-tiles:
	@python3 tools/level_editor/import_tileset.py \
		--sheet assets/forest-tile.png --csv assets/forest-tileset-description.csv \
		--tileset-id forest --label "Whispering Forest" \
		--gb-tileset-kind WORLD_TILESET_FOREST \
		--output-dir tools/level_editor/public/tiles/forest \
		--output-json tools/level_editor/tilesets/forest.json
	@python3 tools/level_editor/import_tileset.py \
		--sheet assets/desolate_landscape.png --csv assets/desolate_landscape-description.csv \
		--tileset-id desolate_landscape --label "Desolate Landscape" \
		--gb-tileset-kind WORLD_TILESET_DESOLATE \
		--output-dir tools/level_editor/public/tiles/desolate_landscape \
		--output-json tools/level_editor/tilesets/desolate_landscape.json
	@python3 tools/level_editor/import_tileset.py \
		--sheet assets/castle-tile.png --csv assets/castle-tileset-description.csv \
		--tileset-id castle --label "Castle & Bastion" \
		--gb-tileset-kind WORLD_TILESET_CASTLE \
		--output-dir tools/level_editor/public/tiles/castle \
		--output-json tools/level_editor/tilesets/castle.json
	@python3 tools/level_editor/import_tileset.py \
		--sheet assets/village-tile.png --csv assets/village-tileset-description.csv \
		--tileset-id village --label "Oakhaven Village" \
		--gb-tileset-kind WORLD_TILESET_VILLAGE \
		--output-dir tools/level_editor/public/tiles/village \
		--output-json tools/level_editor/tilesets/village.json
# NOTE: no actors stanza: assets/actor-sprites.png was removed (commit
# "removed unneeded actors"); the actors tileset is curated-source now and
# its boss cells reload from assets/sprites.png via reload-boss-tiles below.

# Deterministic Boss-tile reload: source sheets + CSVs -> curated editor
# PNGs + color swatches (tools/level_editor/reload_boss_tiles.py). Reruns
# are byte-identical; --check fails loudly on drift (no hand edits to the
# reloaded PNGs). Covers all 12 combat Boss cells (9 live + 3 glow-eyes
# editor-only) and all 8 OW Boss cells (actors tileset) + 4 enemies aliases
# (boss_ow_*, the slime-lord OW sprite).
reload-boss-tiles:
	@python3 tools/level_editor/reload_boss_tiles.py

reload-boss-tiles-check:
	@python3 tools/level_editor/reload_boss_tiles.py --check

# NOTE: extract-tiles is intentionally NOT a dependency here.
# The tileset JSON files (tools/level_editor/tilesets/*.json) are the sole
# source of truth for tile definitions, vram_block layouts, and gb_constant
# values.  Run 'make extract-tiles' once manually when importing a new PNG
# sheet, then commit the resulting JSON.  Re-running it on every editor
# launch would overwrite hand-crafted fields (gb_constant, vram_block).
editor:
	@echo "Starting Game Boy RPG Level Editor..."
	@cd tools/level_editor && npm install --no-audit --no-fund && npm run dev

atlas-check:
	@python3 tools/asset_atlas.py --check

# Tileset manifest check: vram_block composition, exit marking, sheet bounds.
tiles-check:
	@python3 tools/level_editor/validate_tilesets.py tools/level_editor/tilesets/*.json

# Palette manifest generation: curated PNGs -> composed sheets ->
# generated/tiles/*.json + shades + C palette includes + mismatch report.
# Ramps always win (every tile gets an existing artist ramp); wrongness is
# reported in generated/tiles/ramp_mismatches.json, never a build failure.
# See tools/palette_compiler.py.
manifest: tools/level_editor/tilesets/forest.json tools/level_editor/tilesets/castle.json tools/level_editor/tilesets/desolate_landscape.json tools/level_editor/tilesets/village.json tools/palette_parse.py tools/palette_compiler.py tools/palette_slots.json assets/palette.txt screens/combat_art/*.json screens/enemy_types/*.json screens/hero.json screens/cards_skin.json screens/battle_hud.json | $(GENERATED_TILES_DIR)
	@python3 tools/compose_battle_sprites.py
	@python3 tools/compose_enemy_sprites.py
	@python3 tools/compose_hero_sprites.py
	@python3 tools/compose_card_frames.py
	@python3 tools/palette_compiler.py
	@python3 tools/verify_palette_manifest.py

# Mismatch report: tiles whose pixels don't exactly fit their ramp.
# Always exits green (ramps win); the JSON is the artist todo list.
palette-check: manifest
	@python3 -c "import json; m=json.load(open('generated/tiles/ramp_mismatches.json')); print('palette-check: %d reported tiles (see generated/tiles/ramp_mismatches.json)' % len(m)); [print('  %-14s %-7s declared=%-12s used=%-12s off=%s' % (r['sheet'], str(r['tile']), r['declared'], r['used_ramp'], ','.join(r['off_colors']))) for r in m]"

# Tile-trait generation: manifests -> generated/tiles/tile_traits.h
# (walk/glyph ranges + exit indices consumed by world.c, patrol_banked.c,
# ui.c). Deterministic: rerunning reproduces the header byte-identically.
GENERATED_TILES_DIR = generated/tiles
GENERATED_TILE_WALK = $(GENERATED_TILES_DIR)/tile_walk.h
GENERATED_TILE_GLYPH = $(GENERATED_TILES_DIR)/tile_glyph.h
GENERATED_TILE_PALETTE = $(GENERATED_TILES_DIR)/tile_palette.h
GENERATED_TILE_ANIM = $(GENERATED_TILES_DIR)/anim_pairs.h
tiles: manifest $(GENERATED_TILE_WALK) $(GENERATED_TILE_GLYPH) $(GENERATED_TILE_PALETTE) $(GENERATED_TILE_ANIM)

$(GENERATED_TILE_WALK) $(GENERATED_TILE_GLYPH) $(GENERATED_TILE_PALETTE) $(GENERATED_TILE_ANIM): manifest tools/level_editor/tilesets/desolate_landscape.json tools/level_editor/tilesets/forest.json tools/level_editor/tilesets/castle.json tools/level_editor/tilesets/village.json tools/level_compiler/generate_tiles.py | $(GENERATED_TILES_DIR)
	python3 tools/level_compiler/generate_tiles.py --out "$(GENERATED_TILES_DIR)"

$(GENERATED_TILES_DIR):
	mkdir -p $(GENERATED_TILES_DIR)

# Consumers rebuild when the generated headers change (they are untracked).
$(BUILD_DIR)/world/world.o $(BUILD_DIR)/world/patrol_banked.o $(BUILD_DIR)/debug/world/world.o $(BUILD_DIR)/debug/world/patrol_banked.o: $(GENERATED_TILE_WALK)
$(BUILD_DIR)/ui/ui.o $(BUILD_DIR)/debug/ui/ui.o: $(GENERATED_TILE_GLYPH)
$(BUILD_DIR)/game/tiles_content.o $(BUILD_DIR)/debug/game/tiles_content.o: $(GENERATED_TILE_PALETTE)
$(BUILD_DIR)/ui/ui_world_sprite_banked.o $(BUILD_DIR)/debug/ui/ui_world_sprite_banked.o: $(GENERATED_TILE_ANIM)

# Header-dependency safety net (AGENTS.md 52.2): the compile rules track
# only .c -> .o mtimes, so an object compiled against an older struct
# layout links SILENTLY against rebuilt neighbors -- field offsets shift,
# gameplay reads garbage (wrong HP/positions/hangs), and the symptom looks
# like a mysterious regression instead of a build bug.  Over-rebuild every
# object when ANY project header or gfx asset changes; the parallel build
# makes the full pass cheap (~10s).
PROJECT_HEADERS = $(wildcard $(SRC_DIR)/*.h $(SRC_DIR)/*/*.h) \
	$(wildcard $(GFX_OUT_DIR)/*.h $(GFX_OUT_DIR)/*.inc)
# NOTE: generated/tiles/tile_{walk,glyph,palette}.h are deliberately NOT
# here -- their rule depends on the phony `manifest`, so they remake (new
# mtime) on every make run and would force a full rebuild every time.
# Their consumers are wired explicitly above.
$(OBJS) $(OBJS_DEBUG): $(PROJECT_HEADERS)

# Toolchain self-check: every required native binary must not only resolve
# but EXECUTE (a broken file shadowing the real one fails at exec time with
# a cryptic OSError deep inside a build rule). Order-only prerequisite of
# the music/SFX conversion rules, so a broken toolchain fails here first
# with the fix command instead of mid-build.
doctor:
	@python3 tools/doctor.py

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# NOTE: deliberately NO global --max-allocs-per-node override.  SDCC 4.4.1
# (sm83) has a pointer-cache miscompile family across branch joins; the
# per-function volatile guards (battle_update vb, battle_screen vg,
# const-volatile Battle* render params, patrol_banked byte-pointer reads)
# are the fix -- verified by clean-tree A/B (commit history Aug 2026).
# Empirically, BOTH a lowered budget (12000) AND making the g_bk_* banked
# staging globals volatile change codegen in ways that break patrol stepping
# under otherwise-identical sources; default flags with the local guards is
# the only validated configuration.  If you touch optimization flags, run
# patrol_slime_cross / patrol_enemy_bumps_player / battle_multi_enemy_cycle_kill
# plus the full harness before trusting the build.

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c $(INCLUDES) -o $@ $<

# Ensure UI modules rebuild when generated sprite headers change
$(BUILD_DIR)/ui/ui_world_sprite_banked.o $(BUILD_DIR)/debug/ui/ui_world_sprite_banked.o: $(GFX_OUT_DIR)/enemy_ow_tiles.h
$(BUILD_DIR)/ui/ui.o $(BUILD_DIR)/debug/ui/ui.o: $(GFX_OUT_DIR)/hero_desolate_sprite_tile.h

# Per-file alloc caps (see docs/roadmap.md post-mortem): the bank-3 patrol
# path needs a hard-capped budget in these units to keep its commit-path
# stores intact under SDCC 4.4.1; battle/UI keep their volatile guards with
# default flags (caps there re-expose the transition-render HP corruption).
build/debug/world/world.o: src/world/world.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -Wf--max-allocs-per-node500 $(INCLUDES) -o $@ $<

build/debug/world/patrol_banked.o: src/world/patrol_banked.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -Wf--max-allocs-per-node500 $(INCLUDES) -o $@ $<

build/world/world.o: src/world/world.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -Wf--max-allocs-per-node500 $(INCLUDES) -o $@ $<

build/world/patrol_banked.o: src/world/patrol_banked.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -Wf--max-allocs-per-node500 $(INCLUDES) -o $@ $<

$(BUILD_DIR)/debug/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD $(INCLUDES) -o $@ $<

# -DTEST_LEVELS reaches ONLY the files that consume it (engine content
# selection + the content-reading banked bodies, which move to bank 4 in
# the test build).  Compiling it into every debug object shifts every
# banked body's layout; SDCC miscompiles are layout-sensitive
# (AGENTS.md 52.19) and the full-flag variant broke the patrol
# sentinels.  Explicit rules win over the generic pattern above (same
# mechanism as the 52.20 alloc-cap rules).
build/debug/world/scene.o: src/world/scene.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -DTEST_LEVELS $(INCLUDES) -o $@ $<

build/debug/game/actors.o: src/game/actors.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -DTEST_LEVELS $(INCLUDES) -o $@ $<

build/debug/world/scene_load.o: src/world/scene_load.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -DTEST_LEVELS $(INCLUDES) -o $@ $<

build/debug/world/actor_load_banked.o: src/world/actor_load_banked.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -DTEST_LEVELS -Wf--max-allocs-per-node500 $(INCLUDES) -o $@ $<

# Alloc cap (52.19): the MAX_STATIC_ACTORS growth re-exposed a latent
# pointer-cache miscompile when this unit compiled at default flags
# (hostile spawns silently skipped at cap >= 10).  Keep in sync with the
# release rule below.
build/world/actor_load_banked.o: src/world/actor_load_banked.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -Wf--max-allocs-per-node500 $(INCLUDES) -o $@ $<

build/debug/world/actor.o: src/world/actor.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -DTEST_LEVELS $(INCLUDES) -o $@ $<

build/debug/game/content.o: src/game/content.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD -DTEST_LEVELS $(INCLUDES) -o $@ $<

music: $(MUSIC_SRCS) sfx

# Tracker SFX -> synth step tables (Path C transcription). Deterministic:
# rerunning reproduces generated/sfx/sfx_tables.c byte-identically.
sfx: $(SFX_TABLES)

$(SFX_TABLES) &: $(SFX_ALL_UGE) $(SFX_REGISTRY) tools/transcribe_sfx.py | $(GENERATED_SFX_DIR) doctor
	python3 tools/transcribe_sfx.py --out $(GENERATED_SFX_DIR)/sfx_tables.c

$(GENERATED_SFX_DIR):
	mkdir -p $(GENERATED_SFX_DIR)

SFX_OBJS = $(patsubst $(GENERATED_SFX_DIR)/%.c,$(BUILD_DIR)/sfx/%.o,$(SFX_TABLES))
SFX_OBJS_DEBUG = $(patsubst $(GENERATED_SFX_DIR)/%.c,$(BUILD_DIR)/debug/sfx/%.o,$(SFX_TABLES))

$(BUILD_DIR)/sfx/%.o: $(GENERATED_SFX_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c $(INCLUDES) -o $@ $<

$(BUILD_DIR)/debug/sfx/%.o: $(GENERATED_SFX_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD $(INCLUDES) -o $@ $<

$(GENERATED_MUSIC_DIR)/battle.c: assets/music/Battle\ BGM.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 6 song_battle "$@"

$(GENERATED_MUSIC_DIR)/desolate_landscape.c: assets/music/desolate_landscape.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 6 song_desolate_landscape "$@"

$(GENERATED_MUSIC_DIR)/forest.c: assets/music/Forest.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 6 song_forest "$@"

$(GENERATED_MUSIC_DIR)/boss_fight.c: assets/music/Boss\ fight.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 6 song_boss_fight "$@"

$(GENERATED_MUSIC_DIR)/village.c: assets/music/Village.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 6 song_village "$@"

$(GENERATED_MUSIC_DIR)/castle.c: assets/music/castle.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 6 song_castle "$@"

# Mimic battle theme.  Bank 6 (driver + six songs) is full, so the song
# lives in bank 7 alongside a second driver copy (see hUGEDriver_b7
# rules).  huge_music.c switches to the song's bank around hUGE calls.
$(GENERATED_MUSIC_DIR)/mimic.c: assets/music/Mimic.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 7 song_mimic "$@"

# Title theme.  Replaces the old hardcoded chiptune title table now that
# the legacy music engine is gone (docs/uge.md Phase 6).
$(GENERATED_MUSIC_DIR)/title.c: assets/music/title\ short.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 6 song_title "$@"

# Battle-victory jingle (one-shot source).  Replaces the old hardcoded
# 4-note victory table.  Bank 6 is nearly full, so it plays from bank 7.
$(GENERATED_MUSIC_DIR)/victory.c: assets/music/victory.uge tools/compile_music.py | $(GENERATED_MUSIC_DIR) doctor
	python3 tools/compile_music.py "$<" 7 song_victory "$@"

# WAV previews for the level editor's BGM toggle (Inspector Map Info).
# Rendered from the generated song C by tools/render_music_preview.py
# (driver-faithful approximation, not the ROM mix). Explicit target so
# song builds stay fast; re-run after changing any assets/music/*.uge.
MUSIC_PREVIEW_DIR = tools/level_editor/public/audio
MUSIC_PREVIEW_WAVS = $(MUSIC_PREVIEW_DIR)/battle.wav $(MUSIC_PREVIEW_DIR)/desolate_landscape.wav $(MUSIC_PREVIEW_DIR)/forest.wav $(MUSIC_PREVIEW_DIR)/boss_fight.wav $(MUSIC_PREVIEW_DIR)/village.wav $(MUSIC_PREVIEW_DIR)/castle.wav $(MUSIC_PREVIEW_DIR)/mimic.wav

music-preview: music $(MUSIC_PREVIEW_WAVS)
	@echo "Music previews up to date in $(MUSIC_PREVIEW_DIR)"

$(MUSIC_PREVIEW_DIR)/%.wav: $(GENERATED_MUSIC_DIR)/%.c tools/render_music_preview.py | $(MUSIC_PREVIEW_DIR)
	python3 tools/render_music_preview.py $*

$(MUSIC_PREVIEW_DIR):
	mkdir -p $(MUSIC_PREVIEW_DIR)

# WAV previews for the level editor's sound-test modal (toolbar 🔊 SFX).
# Rendered from the transcribed tables by tools/render_sfx_preview.py.
# Explicit target; re-run after changing any assets/sfx/*.uge.
SFX_PREVIEW_DIR = tools/level_editor/public/audio/sfx
SFX_PREVIEW_WAVS = $(SFX_PREVIEW_DIR)/cursor.wav $(SFX_PREVIEW_DIR)/confirm.wav $(SFX_PREVIEW_DIR)/select.wav $(SFX_PREVIEW_DIR)/back.wav $(SFX_PREVIEW_DIR)/attack.wav $(SFX_PREVIEW_DIR)/hit.wav $(SFX_PREVIEW_DIR)/block.wav

sfx-preview: sfx $(SFX_PREVIEW_WAVS)
	@echo "SFX previews up to date in $(SFX_PREVIEW_DIR)"

$(SFX_PREVIEW_DIR)/%.wav: $(GENERATED_SFX_DIR)/sfx_tables.c tools/render_sfx_preview.py | $(SFX_PREVIEW_DIR)
	python3 tools/render_sfx_preview.py $*

$(SFX_PREVIEW_DIR):
	mkdir -p $(SFX_PREVIEW_DIR)

$(GENERATED_MUSIC_DIR):
	mkdir -p $(GENERATED_MUSIC_DIR)

$(BUILD_DIR)/music/%.o: $(GENERATED_MUSIC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c $(INCLUDES) -o $@ $<

$(BUILD_DIR)/debug/music/%.o: $(GENERATED_MUSIC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) -c -DDEBUG_BUILD $(INCLUDES) -o $@ $<

$(HUGEDRIVER_OBJ): lib/hUGEDriver/src/hUGEDriver.asm tools/rgb2sdas.py | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(RGBASM_HUGE) -I lib/hUGEDriver/ -DGBDK -o $(BUILD_DIR)/lib/hUGEDriver.obj $<
	$(RGB2SDAS) -b 6 -o $@ $(BUILD_DIR)/lib/hUGEDriver.obj

$(HUGEDRIVER_OBJ_DEBUG): lib/hUGEDriver/src/hUGEDriver.asm tools/rgb2sdas.py | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(RGBASM_HUGE) -I lib/hUGEDriver/ -DGBDK -o $(BUILD_DIR)/debug/lib/hUGEDriver.obj $<
	$(RGB2SDAS) -b 6 -o $@ $(BUILD_DIR)/debug/lib/hUGEDriver.obj

# Second hUGE driver copy in bank 7 (renamed exports so the two copies
# coexist; internal driver references are section-relative and resolve
# within bank 7).  Paired with the mimic song data in bank 7.
$(HUGEDRIVER_B7_OBJ): lib/hUGEDriver/src/hUGEDriver.asm tools/rgb2sdas.py | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(RGBASM_HUGE) -I lib/hUGEDriver/ -DGBDK -o $(BUILD_DIR)/lib/hUGEDriver_b7.obj $<
	$(RGB2SDAS) -b 7 \
		-r hUGE_init=hUGE_init_b7 \
		-r _hUGE_init=_hUGE_init_b7 \
		-r hUGE_dosound=hUGE_dosound_b7 \
		-r _hUGE_dosound=_hUGE_dosound_b7 \
		-r hUGE_mute_channel=hUGE_mute_channel_b7 \
		-r _hUGE_mute_channel=_hUGE_mute_channel_b7 \
		-r hUGE_set_position=hUGE_set_position_b7 \
		-r _hUGE_set_position=_hUGE_set_position_b7 \
		-r hUGE_current_wave=hUGE_current_wave_b7 \
		-r _hUGE_current_wave=_hUGE_current_wave_b7 \
		-r hUGE_mute_mask=hUGE_mute_mask_b7 \
		-r _hUGE_mute_mask=_hUGE_mute_mask_b7 \
		-r hUGE_NO_WAVE=hUGE_NO_WAVE_B7 \
		-o $@ $(BUILD_DIR)/lib/hUGEDriver_b7.obj

$(HUGEDRIVER_B7_OBJ_DEBUG): lib/hUGEDriver/src/hUGEDriver.asm tools/rgb2sdas.py | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(RGBASM_HUGE) -I lib/hUGEDriver/ -DGBDK -o $(BUILD_DIR)/debug/lib/hUGEDriver_b7.obj $<
	$(RGB2SDAS) -b 7 \
		-r hUGE_init=hUGE_init_b7 \
		-r _hUGE_init=_hUGE_init_b7 \
		-r hUGE_dosound=hUGE_dosound_b7 \
		-r _hUGE_dosound=_hUGE_dosound_b7 \
		-r hUGE_mute_channel=hUGE_mute_channel_b7 \
		-r _hUGE_mute_channel=_hUGE_mute_channel_b7 \
		-r hUGE_set_position=hUGE_set_position_b7 \
		-r _hUGE_set_position=_hUGE_set_position_b7 \
		-r hUGE_current_wave=hUGE_current_wave_b7 \
		-r _hUGE_current_wave=_hUGE_current_wave_b7 \
		-r hUGE_mute_mask=hUGE_mute_mask_b7 \
		-r _hUGE_mute_mask=_hUGE_mute_mask_b7 \
		-r hUGE_NO_WAVE=hUGE_NO_WAVE_B7 \
		-o $@ $(BUILD_DIR)/debug/lib/hUGEDriver_b7.obj

# Grouped targets (&:): the lite libs come from ONE make_lite_libs.py run.
# A plain multi-target rule runs its recipe once PER TARGET, which under
# `make debug release -j` (the editor's Compile ROM button) races the two
# runs on the script's temp file.  &: runs the recipe exactly once.
$(GB_LITE) $(SM83_LITE) &: $(OBJS) $(OBJS_DEBUG) | $(BUILD_DIR)
	python3 tools/make_lite_libs.py $(BUILD_DIR)

# The VBlank ISR is copied to WRAM 0xC900 by crt0.s.  sdldgb auto-places
# _DATA at 0xC0A0 (after shadow OAM) and ignores ABS .org reservations, so
# _DATA is pinned at 0xC940 to keep every C symbol above the reserved
# 0xC900-0xC93F ISR region.  Without this, g_boot_phase/g_harness_mode land
# at 0xC89A-C89B and get corrupted by the fixed-layout WRAM (blank screen).
LDFLAGS = -Wl-b_DATA=0xC940

$(TARGET): gfx tiles levels screens dialogues shops entities music $(OBJS) build/crt0.o $(GB_LITE) $(SM83_LITE) | $(BUILD_DIR)
	@$(CC) -no-crt -Wm-yc -Wl-yt0x19 -Wl-yo8 $(LDFLAGS) -Wl-m -Wl-j -o $@ build/crt0.o $(OBJS) $(GB_LITE) $(SM83_LITE) > $(BUILD_DIR)/link-release.log 2>&1; rc=$$?; cat $(BUILD_DIR)/link-release.log; test $$rc -eq 0
# Fixed-bank overflow is silent: rgblink warns ("Possible overflow ...
# bank 1 -> 2" / "spans into the next") but still overwrites the start of
# the next bank (AGENTS.md 52.18).  Fail loudly instead (see also
# tools/check_banked_copies.sh, wired into `make lint`).
	@if grep -Eq "Possible overflow|spans into the next" $(BUILD_DIR)/link-release.log; then echo "link: BANK OVERFLOW detected (see $(BUILD_DIR)/link-release.log)" >&2; exit 1; fi
	@python3 tools/make_sym.py $(BUILD_DIR)/kaartenheld.noi $(BUILD_DIR)/kaartenheld.sym
# NOTE: lcc already runs rgbfix once internally at link time, so this
# explicit second fix only (idempotently) stamps MBC/RAM/title fields.
# -W no-overwrite silences the resulting "overwrote a non-zero byte"
# noise (verified byte-identical via sha256 before/after).
	@$(RGBFIX) -v -C -m 0x1b -r 2 -t "KAARTENHELD" -W no-overwrite $@

$(TARGET_DEBUG): gfx tiles levels levels-test screens dialogues shops entities music $(OBJS_DEBUG) build/crt0.o $(GB_LITE) $(SM83_LITE) | $(BUILD_DIR)
	@$(CC) -no-crt -Wm-yc -Wl-yt0x19 -Wl-yo8 $(LDFLAGS) -Wl-m -Wl-j -Wl-y -o $@ build/crt0.o $(OBJS_DEBUG) $(GB_LITE) $(SM83_LITE) > $(BUILD_DIR)/link-debug.log 2>&1; rc=$$?; cat $(BUILD_DIR)/link-debug.log; test $$rc -eq 0
	@if grep -Eq "Possible overflow|spans into the next" $(BUILD_DIR)/link-debug.log; then echo "link: BANK OVERFLOW detected (see $(BUILD_DIR)/link-debug.log)" >&2; exit 1; fi
	@python3 tools/make_sym.py $(BUILD_DIR)/kaartenheld_debug.noi $(BUILD_DIR)/kaartenheld_debug.sym
	@$(RGBFIX) -v -C -m 0x1b -r 2 -t "KAARTENHELD" -W no-overwrite $@

build/crt0.o: src/crt0.s | $(BUILD_DIR)
	sdasgb -o $@ $<

run: $(TARGET)
	@if [ -z "$(EMULATOR)" ]; then \
		echo "Error: No suitable Game Boy emulator found in PATH." >&2; \
		exit 1; \
	fi; \
	echo "Launching ROM in emulator ($(EMULATOR))..."; \
	$(EMULATOR) $(TARGET)

run-debug: $(TARGET_DEBUG)
	@if [ -z "$(EMULATOR)" ]; then \
		echo "Error: No suitable Game Boy emulator found in PATH." >&2; \
		exit 1; \
	fi; \
	echo "Launching Debug ROM in emulator ($(EMULATOR))..."; \
	$(EMULATOR) $(TARGET_DEBUG)

test: $(TARGET)
	@echo "Validating Game Boy ROM header..."
	@if command -v $(RGBFIX) >/dev/null 2>&1; then \
		$(RGBFIX) -v -C -t "KAARTENHELD" -W no-overwrite $(TARGET); \
	else \
		test -s $(TARGET); \
	fi
	@echo "ROM validation successful: $(TARGET)"

test-harness: debug
	python3 tools/dev.py test --jobs $(JOBS)

test-scenario: debug
	@python3 tools/dev.py scenario $(SCENARIO)

state: debug
	@python3 tools/dev.py state $(SCENARIO)

roundtrip: debug
	@python3 tools/dev.py roundtrip $(SCENARIO)

screenshot: $(TARGET)
	@bash tools/screenshot.sh $(BUILD_DIR)/screenshot.png $(TARGET)

# Headless gameplay walkthrough screenshots for visual review without booting
# the ROM (see tools/capture_walkthrough.py and AGENTS.md §56).  Boots the
# real release ROM in headless PyBoy, walks deterministically to each
# milestone, and saves raw 160x144 PNGs into screenshots/.  Visual review
# only -- semantic telemetry stays authoritative.  Manual only; not part of
# the CI chain.
# WYSIWYG gate: the ROM must show what the level editor shows for the
# same levels/*.json (whole-map VRAM parity per map + animation frame-set
# parity).  Needs the debug ROM (runs the SameBoy harness like
# test-harness).  Manual only for now; not part of the CI chain.
parity: debug
	@python3 tools/parity_check.py

screenshots: $(TARGET)
	@python3 tools/capture_walkthrough.py

# README GIFs (host-side, never CI-gated): boot splash+title, a kobold
# battle, and the Field -> Forest overworld walk.  Committed under
# screenshots/ (the walkthrough --clean prune only removes *.png, so these
# survive).
gifs: $(TARGET)
	@python3 tools/capture_gifs.py

# Semantic real-content gate (docs/verify-walkthrough.md): drives the
# RELEASE ROM (real levels/ content) headlessly and asserts canonical
# gameplay state read from WRAM.  This is the counterweight to the
# two-tier fixture suite (AGENTS.md 42.1): engine regressions that only
# fire with working content are caught here.  Required on push (CI).
# The saved PNGs stay a non-gating visual aid (AGENTS.md 56.4).
verify-walkthrough: release
	@python3 tools/capture_walkthrough.py --clean

# Verify the player sprite's real-OAM transition-hide across screen changes
# and scene (map) changes via the mGBA debugger (see tools/verify_oam.py).
verify-oam: debug
	@python3 tools/verify_oam.py

# Verify real-boot VRAM writes land (vsync-before-render + LCD-off boot
# redraw): boots the debug ROM WITHOUT harness mode and compares the real
# background ring against the WRAM mirror (see tools/verify_vram.py).
verify-vram: debug
	@python3 tools/verify_vram.py

# Verify camera scrolling invariants (0 VRAM writes during scroll, 2 writes on
# tile commit, no blank floor cells, hero anchored, no LCD-off frames).
verify-scroll: debug
	@python3 tools/verify_scroll.py

# Verify autonomous enemy patrol AI.  The authoritative patrol regressions
# are the harness sentinel scenarios (telemetry-based; see AGENTS.md 52.19):
# patrol_slime_cross (cross stepping) and patrol_enemy_bumps_player
# (patrol -> collision -> encounter).  The old tools/verify_patrol.py used
# stale hardcoded WRAM offsets and was retired.
verify-patrol: debug
	@python3 tools/dev.py scenario patrol_slime_cross
	@python3 tools/dev.py scenario patrol_enemy_bumps_player

# Verify the music clock never stalls across screen/map transitions: boots the
# debug ROM WITHOUT harness mode (real interrupts) and walks FIELD -> TOWN and
# a guard dialogue round-trip, asserting g_audio_ticks advances every sampled
# frame (timer-driven clock; VBlank stalls 1-2 frames per LCD-off redraw).
verify-music: debug
	@python3 tools/verify_music.py

# Endurance stress test: 60 seconds (3600 frames) of continuous gameplay with
# real timer-driven audio and OAM DMA to assert long-running stability.
verify-endurance: release debug
	@python3 tools/verify_endurance.py

# Font/VRAM pixel ground truth via PyBoy (see tools/vram_check.py).  Boots the
# real release ROM headlessly and reads VRAM directly -- the one place a
# pixel-level check is the correct tool (the char->tile mapping has no
# semantic representation).  Manual only; not part of the CI chain.
vram-check: release
	@python3 tools/vram_check.py

# Text-layer ground truth via PyBoy (see tools/vram_text_check.py): asserts
# generic text lands on the always-displayed BACKGROUND (0x9800), not the
# overworld-only WINDOW (0x9C00), by opening the ITEM menu with START and
# reading real VRAM.  Manual only; not part of the CI chain.
vram-text: release
	@python3 tools/vram_text_check.py

# Dialogue-box placement ground truth via PyBoy (see
# tools/vram_dialogue_check.py): asserts the dialogue box renders in the
# BACKGROUND tilemap (0x9800) at screen rows 12-17, written into the
# scrolled ring at (12 + scroll_y)..(17 + scroll_y) so it cannot shift with
# the camera or pollute the map ("text from other scenes").  Manual only;
# not part of the CI chain.
vram-dialogue: release
	@python3 tools/vram_dialogue_check.py

# Print a reproducible memory budget (code/WRAM usage, _HOME headroom vs the
# 0x8000 ceiling).  Exits non-zero if a documented invariant is violated.
memmap: debug
	@python3 tools/memmap.py $(BUILD_DIR)/kaartenheld_debug.map

clean:
	rm -rf $(BUILD_DIR) $(GENERATED_MUSIC_DIR)
