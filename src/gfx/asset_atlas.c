/* Fixed-bank accessors for the asset atlas (see gfx/asset_atlas.h).
 *
 * The atlas data lives in ROM banks 8 (entries + palettes) and 7 (icon
 * tiles); the engine code that reads it stays in the fixed bank.  Each
 * accessor stages its source inside the banked_copy() trampoline and copies
 * the small row / tile into a WRAM scratch buffer, so the caller never has
 * to handle ROM bank switching (and never reads banked data after the call
 * returns: banked_copy restores home bank 1).
 */
#include "banked.h"
#include "gfx/asset_atlas.h"

/* Static scratch (never large locals; §52.14). */
static AssetAtlasEntry s_atlas_scratch;

void asset_atlas_get(AssetId id, AssetAtlasEntry *out)
{
    const AssetAtlasEntry *row;

    if (id >= ASSET_ID_COUNT) {
        id = ASSET_NONE;
    }
    row = &g_asset_atlas_entries[id];
    banked_copy(ASSET_ATLAS_BANK_ENTRIES, &s_atlas_scratch, row,
                sizeof(AssetAtlasEntry));
    *out = s_atlas_scratch;
}

void asset_atlas_icon_tile(uint16_t uid, uint8_t *out)
{
    banked_copy(ASSET_ATLAS_BANK_ICONS, out,
                &g_asset_icon_tiles[(uint16_t)uid << 4], 16);
}

void asset_atlas_icon_palette(uint8_t pid, uint8_t *out)
{
    banked_copy(ASSET_ATLAS_BANK_ENTRIES, out,
                &g_asset_icon_palettes[(uint16_t)pid << 3], 8);
}