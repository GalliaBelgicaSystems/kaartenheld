#ifndef RPG_TILE_LOOKUP_H
#define RPG_TILE_LOOKUP_H

#include <stdint.h>

#define RPG_TILE_BASE_WORLD          128u
#define RPG_TILE_BASE_DESOLATE       128u

extern const uint8_t g_tileset_forest[1280];
extern const uint8_t g_tileset_desolate[1024];
extern const uint8_t g_tileset_castle[384];
extern const uint8_t g_tileset_village[768];
extern const uint8_t g_intrepid_font_tiles[1536];
extern const uint8_t g_tile_pal_forest[80];
extern const uint8_t g_tile_pal_desolate[64];
extern const uint8_t g_tile_pal_castle[24];
extern const uint8_t g_tile_pal_village[48];

static inline uint8_t rpg_lookup_tile_id(uint8_t tileset_kind, char glyph)
{
    if (tileset_kind == 15 /* WORLD_TILESET_CASTLE */) {
        /* Slots follow the 24-tile VRAM window (base 0..15, extension
         * 16..22, exit LAST at 23): plain floor is slot 22, stairs slot
         * 23, chandelier (nearest prop art) slot 20. */
        if (glyph == '.') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 22); /* Plain floor (4,2) */
        if (glyph == '#') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 10); /* Castle wall */
        if (glyph == '>' || glyph == '<') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 23); /* Archway gate */
        if (glyph == 'B' || glyph == '*') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 20); /* Chandelier / prop */
        return 0;
    }
    if (tileset_kind == 14 /* WORLD_TILESET_DESOLATE */ && glyph == '.') {
        return (uint8_t)(RPG_TILE_BASE_DESOLATE + 32); /* Plain solid floor (0,2) */
    }
    if (tileset_kind == 2 /* WORLD_TILESET_FOREST */ && glyph >= '1' && glyph <= '4') {
        return (uint8_t)(RPG_TILE_BASE_DESOLATE + 3 + (glyph - '1'));
    }
    if (glyph == '.') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 0);
    if (glyph == '#') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 1);
    if (glyph == '>' || glyph == '<') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 2);
    if (glyph == 'B' || glyph == '*') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 3);
    return 0;
}

#endif /* RPG_TILE_LOOKUP_H */
