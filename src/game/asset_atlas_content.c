#pragma bank 8

#include "gfx/asset_atlas.h"

/* Asset atlas lookup rows + per-icon CGB palettes, extracted by
 * tools/asset_atlas.py (make atlas).  One row per AssetId, in enum order --
 * g_asset_atlas_entries[ASSET_X] is the entry for ASSET_X.  Read through
 * asset_atlas_get() / asset_atlas_icon_palette() (banked_copy), never
 * directly from the fixed bank.  Icons (unique 2bpp tiles) live in
 * src/game/asset_atlas_icons_content.c (ASSET_ATLAS_BANK_ICONS). */
const AssetAtlasEntry g_asset_atlas_entries[ASSET_ID_COUNT] = {
#include "gfx/asset_atlas_entries.inc"
};

/* Per-tile CGB palettes, 8 bytes each (RGB555 LE, lightest-first). */
const uint8_t g_asset_icon_palettes[8 * ASSET_ICON_PALETTE_COUNT] = {
#include "gfx/asset_atlas_icon_palettes.inc"
};