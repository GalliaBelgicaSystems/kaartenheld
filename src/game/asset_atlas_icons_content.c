#pragma bank 7

#include "gfx/asset_atlas.h"

/* Unique icon tiles (2bpp, 16 bytes each) for the equipment + symbol icon
 * sheets, extracted by tools/asset_atlas.py (make atlas).  Indexed by the
 * `icon_uid` field of an AssetAtlasEntry; read through the fixed-bank
 * accessor asset_atlas_icon_tile(), never directly from the fixed bank. */
const uint8_t g_asset_icon_tiles[16 * ASSET_ICON_TILE_COUNT] = {
#include "gfx/asset_atlas_icons.inc"
};