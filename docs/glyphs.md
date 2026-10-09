# Glyph registry — the single source of truth for what every ASCII cell means

Every glyph is mandatory and unique per element. Terrain glyphs live in the
tileset manifests (`tools/level_editor/tilesets/*.json`, per-tile `glyph`
field; absent means the historic derivation: exit `>`, object `*`,
walkable `.`, else `#`). Actor glyphs live in `levels/*.json`
`objects[].properties.visual` (single char, validated). The two layers
never share a glyph, so a screen cell names exactly one thing.

## Terrain

| glyph | meaning | tiles |
|---|---|---|
| `.` | walkable ground | plain floors (see `default_walkable` per level) |
| `,` | tufted ground | `*_floor_with_stuff_walkable_*` |
| `#` | blocking decor | walls, furniture, boss-block |
| `>` / `<` | exits | exit tiles (directional pair) |
| `*` | fire / campfire | fire frames, campfire |
| `T` | tree canopy | `*_treetop`, `*_tree_top` |
| `t` | trunk | `*_treetrunk`, `*_tree_trunk` |
| `s` | stump | `*_stump` |
| `O` | chest | `*_chest`, `*_treasure_chest_*` |
| `R` | rock | `*_rock` |

## Actors

| glyph | entity |
|---|---|
| `@` | hero / player (host-only; ROM draws an OAM sprite) |
| `E` | slime |
| `V` | bat |
| `L` | lord of slimes (boss) |
| `M` | mayor |
| `G` | guard |
| `S` | shopkeeper |
| `C` | merchant |
| `W` | wizard |
| `?` | signpost |
| `A` | amulet pickup |

## Rules

1. One element, one glyph, everywhere (all maps, all screens, editor, ROM,
   harness display). Collisions (`M/M` mayor/merchant, `?/?`
   signpost/amulet) were fixed by assigning `C` and `A`.
2. Glyphs carry identity, never mechanics: collision comes from `walkable`,
   behavior from `battle`/`ai`/`interaction`. Changing a glyph never
   changes gameplay (only `actor_name_for_visual` in `src/world/actor.c`
   derives a display name from hostile visuals — keep it in sync).
3. Color follows glyphs where the fixed-bank budget allows (see the palette
   plan); DMG output is glyph-identical regardless.
