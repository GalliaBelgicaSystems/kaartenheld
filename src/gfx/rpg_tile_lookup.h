#ifndef RPG_TILE_LOOKUP_H
#define RPG_TILE_LOOKUP_H

#include <stdint.h>

#define RPG_TILE_BASE_WORLD          128u
#define RPG_TILE_BASE_DESOLATE       128u

extern const uint8_t g_tileset_forest[768];
extern const uint8_t g_tileset_desolate_landscape[768];
extern const uint8_t g_tileset_castle[432];
extern const uint8_t g_tileset_village[768];
extern const uint8_t g_intrepid_font_tiles[1536];
extern const uint8_t g_tile_pal_forest[48];
extern const uint8_t g_tile_pal_desolate_landscape[48];
extern const uint8_t g_tile_pal_castle[16];
extern const uint8_t g_tile_pal_village[48];

static inline uint8_t rpg_lookup_tile_id(uint8_t tileset_kind, char glyph)
{
    if (tileset_kind == 15 /* WORLD_TILESET_CASTLE */) {
        if (glyph == '.') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 18); /* Plain solid stone floor (0,2) */
        if (glyph == '#') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 10); /* Castle wall */
        if (glyph == '>' || glyph == '<') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 13); /* Archway gate */
        if (glyph == 'B' || glyph == '*') return (uint8_t)(RPG_TILE_BASE_DESOLATE + 16); /* Torch / prop */
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
