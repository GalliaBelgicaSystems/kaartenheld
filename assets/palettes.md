# Defined palettes (generated — do not edit by hand)

Source of truth: `assets/palette.txt`, read by
`tools/palette_txt.py`.  Regenerate with `make manifest`
(the `palette-check` gate fails on drift).  Every shade
cites the `palette.txt` reference it resolves from; artist
tutorial: `assets/palette_tutorial.md`.

## Base — battle / card UI (`cgb_bg_palettes`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | gray | `#ffffff` | `#aaaaaa` | `#555555` | `#000000` | UI text/backdrop, spider art | COMMON/white, COMMON/silver, COMMON/ash, FOREST/void_holes |
| 1 | fire | `#ffffe0` | `#ff8c28` | `#dc3214` | `#640a00` | burn cards, kobold art | GAMEPLAY/fire_pale, GAMEPLAY/fire_light, GAMEPLAY/fire_mid, GAMEPLAY/fire_deep |
| 2 | iron_ice | `#ebf2fa` | `#8cb4d6` | `#46698a` | `#1b2b3a` | sword/freeze cards | GAMEPLAY/iron_pale, GAMEPLAY/iron_light, GAMEPLAY/iron_mid, GAMEPLAY/iron_deep |
| 3 | slime | `#ffffff` | `#89cc5e` | `#5c903a` | `#000000` | slime art, heal cards | COMMON/white, COMBAT/slime_combat, COMBAT/slime_outline_combat, FOREST/void_holes |
| 4 | poison | `#faf0fa` | `#be8cc8` | `#8c50a0` | `#3c1e50` | poison/dagger cards, dialogue paper | GAMEPLAY/mauve_pale, GAMEPLAY/mauve_light, GAMEPLAY/mauve_mid, GAMEPLAY/mauve_deep |
| 5 | wood | `#f5e6d2` | `#c48a48` | `#8a5222` | `#3d200a` | shield cards, mimic art | COMMON/parchment, GAMEPLAY/wood_light, GAMEPLAY/wood_mid, GAMEPLAY/wood_deep |
| 6 | gold | `#fffce0` | `#d7a726` | `#c88c08` | `#5a3a00` | bow cards | GAMEPLAY/gold_pale, CASTLE/gold, GAMEPLAY/gold_mid, GAMEPLAY/gold_shadow |
| 7 | dim | `#c8c8c8` | `#969696` | `#5a5a5a` | `#282828` | grey-out, bat/boss art | COMMON/gray_light, COMMON/gray_mid, COMMON/gray_dark, COMMON/gray_deep |

## Forest / field (`cgb_bg_palettes_forest`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | gray | `#ffffff` | `#aaaaaa` | `#555555` | `#000000` | misc gray | COMMON/white, COMMON/silver, COMMON/ash, FOREST/void_holes |
| 1 | fire | `#ffffe0` | `#ff8c28` | `#dc3214` | `#640a00` | campfire tiles | GAMEPLAY/fire_pale, GAMEPLAY/fire_light, GAMEPLAY/fire_mid, GAMEPLAY/fire_deep |
| 2 | iron_ice | `#ebf2fa` | `#8cb4d6` | `#46698a` | `#1b2b3a` | iron accents | GAMEPLAY/iron_pale, GAMEPLAY/iron_light, GAMEPLAY/iron_mid, GAMEPLAY/iron_deep |
| 3 | field | `#7bb660` | `#2a4f1a` | `#1d3e0f` | `#000000` | canopy + grass (UI_COLOR_FIELD) | FOREST/grass, FOREST/tree_leaves_terrain_outline_big_grass, FOREST/dark_tree_leaves, FOREST/void_holes |
| 4 | poison | `#faf0fa` | `#be8cc8` | `#8c50a0` | `#3c1e50` | poison accents | GAMEPLAY/mauve_pale, GAMEPLAY/mauve_light, GAMEPLAY/mauve_mid, GAMEPLAY/mauve_deep |
| 5 | wood | `#7bb660` | `#c48a48` | `#8a5222` | `#3d200a` | trunks/stumps, merchant NPC (UI_COLOR_WOOD) | FOREST/grass, GAMEPLAY/wood_light, GAMEPLAY/wood_mid, GAMEPLAY/wood_deep |
| 6 | gold | `#fffce0` | `#d7a726` | `#c88c08` | `#5a3a00` | gold accents, mayor NPC | GAMEPLAY/gold_pale, CASTLE/gold, GAMEPLAY/gold_mid, GAMEPLAY/gold_shadow |
| 7 | dim | `#c8c8c8` | `#969696` | `#5a5a5a` | `#26232e` | rocks (UI_COLOR_DIM) | COMMON/gray_light, COMMON/gray_mid, COMMON/gray_dark, FOREST/rocks_outline |

## Desolate (`cgb_bg_palettes_desolate`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | gray | `#ffffff` | `#aaaaaa` | `#555555` | `#000000` | misc gray | COMMON/white, COMMON/silver, COMMON/ash, FOREST/void_holes |
| 1 | campfire | `#938da1` | `#edc214` | `#d75014` | `#500a00` | campfire tiles | DESOLATE/ground, DESOLATE/flame_light, DESOLATE/flame_mid, DESOLATE/flame_deep |
| 2 | iron_ice | `#938da1` | `#8cb4d6` | `#46698a` | `#1b2b3a` | iron accents | DESOLATE/ground, GAMEPLAY/iron_light, GAMEPLAY/iron_mid, GAMEPLAY/iron_deep |
| 3 | flora | `#938da1` | `#746f80` | `#3f3a4a` | `#26232e` | flora accents | DESOLATE/ground, DESOLATE/flora_mid, DESOLATE/terrain_side, DESOLATE/tree_item_outlines_cracks |
| 4 | poison | `#938da1` | `#be8cc8` | `#8c50a0` | `#3c1e50` | poison accents | DESOLATE/ground, GAMEPLAY/mauve_light, GAMEPLAY/mauve_mid, GAMEPLAY/mauve_deep |
| 5 | deadwood | `#938da1` | `#8d754a` | `#6f5a34` | `#26232e` | dead trees (UI_COLOR_WOOD) | DESOLATE/ground, CASTLE/wood_light, CASTLE/wood_dark, DESOLATE/tree_item_outlines_cracks |
| 6 | gold | `#938da1` | `#d7a726` | `#8d754a` | `#321e0a` | gold accents, treasure chest | DESOLATE/ground, CASTLE/gold, CASTLE/wood_light, DESOLATE/gold_shadow |
| 7 | slate_rock | `#938da1` | `#837b96` | `#3f3a4a` | `#26232e` | slate ground (UI_COLOR_DIM) | DESOLATE/ground, DESOLATE/rocks_stone_shadow, DESOLATE/terrain_side, DESOLATE/tree_item_outlines_cracks |

## Castle (`cgb_bg_palettes_castle`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | stone | `#a99fc0` | `#b3b0b0` | `#828282` | `#2e2e2e` | stone floors/walls (UI_COLOR_NONE) | CASTLE/ground, CASTLE/stone_mid, CASTLE/wall_light, CASTLE/window_frame |
| 1 | curtain | `#a99fc0` | `#8b1b1b` | `#621212` | `#1e0000` | curtains | CASTLE/ground, CASTLE/curtain_light, CASTLE/curtain_dark, CASTLE/curtain_deep |
| 2 | iron | `#a99fc0` | `#8ca0b4` | `#465a6e` | `#1e2832` | iron accents | CASTLE/ground, CASTLE/steel_light, CASTLE/steel_mid, CASTLE/steel_deep |
| 3 | moss_green | `#a99fc0` | `#5a8c50` | `#28501e` | `#0a1e0a` | moss accents | CASTLE/ground, CASTLE/moss_light, CASTLE/moss_mid, CASTLE/moss_deep |
| 4 | poison | `#a99fc0` | `#be8cc8` | `#8c50a0` | `#3c1e50` | poison accents | CASTLE/ground, GAMEPLAY/mauve_light, GAMEPLAY/mauve_mid, GAMEPLAY/mauve_deep |
| 5 | wood_furn | `#a99fc0` | `#9e8e71` | `#6f5a34` | `#28190a` | furniture (UI_COLOR_WOOD) | CASTLE/ground, CASTLE/timber_light, CASTLE/wood_dark, CASTLE/timber_deep |
| 6 | gold | `#a99fc0` | `#d7a726` | `#a29271` | `#3c280a` | gold accents, chest | CASTLE/ground, CASTLE/gold, CASTLE/gold_mid, CASTLE/gold_shadow |
| 7 | dim_shadow | `#a99fc0` | `#828282` | `#565656` | `#232323` | shading (UI_COLOR_DIM) | CASTLE/ground, CASTLE/wall_light, CASTLE/wall_dark, CASTLE/shade_deep |

## Village / town (`cgb_bg_palettes_village`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | gray | `#b6a27e` | `#c8c8c8` | `#7d7d7d` | `#1e1e1e` | stonework (UI_COLOR_NONE) | TOWN/ground, TOWN/stone_light, TOWN/stone_mid, TOWN/stone_deep |
| 1 | fire | `#b6a27e` | `#ffc460` | `#dc6e20` | `#5a280a` | braziers/torches | TOWN/ground, TOWN/ember_light, TOWN/ember_mid, TOWN/ember_deep |
| 2 | iron | `#b6a27e` | `#96a0b4` | `#556982` | `#232d3c` | iron accents | TOWN/ground, TOWN/steel_light, TOWN/steel_mid, TOWN/steel_deep |
| 3 | dirt_floor | `#b6a27e` | `#8c7858` | `#604e34` | `#302418` | dirt ground (UI_COLOR_FIELD) | TOWN/ground, TOWN/dirt_light, TOWN/dirt_mid, TOWN/dirt_deep |
| 4 | mauve | `#b6a27e` | `#be8cc8` | `#8c50a0` | `#3c1e50` | mauve accents | TOWN/ground, GAMEPLAY/mauve_light, GAMEPLAY/mauve_mid, GAMEPLAY/mauve_deep |
| 5 | wood | `#b6a27e` | `#8d754a` | `#645233` | `#26180a` | houses/barrels, merchant NPC (UI_COLOR_WOOD) | TOWN/ground, TOWN/barrel_wood_fence, TOWN/wood_outlines, TOWN/wood_deep |
| 6 | cream | `#b6a27e` | `#f1cf91` | `#ccaa6c` | `#645233` | walls/roofs, mayor NPC | TOWN/ground, TOWN/house_wall, TOWN/house_roof_shade1, TOWN/wood_outlines |
| 7 | dim | `#b6a27e` | `#9e9480` | `#645842` | `#2a241a` | shading (UI_COLOR_DIM) | TOWN/ground, TOWN/shade_light, TOWN/shade_mid, TOWN/shade_deep |

## OBJ sprite ramps (`src/ui/ui.c`, slot 0 = COMMON white)

| Name | S0 | S1 | S2 | S3 | Serves | Refs |
|------|----|----|----|----|--------|------|
| grey | `#ffffff` | `#aaaaaa` | `#555555` | `#000000` | player, bats, UI sprites (OBJ 0) | COMMON/white, COMMON/silver, COMMON/ash, FOREST/void_holes |
| orange | `#ffffff` | `#f57137` | `#c34e1b` | `#8b1b1b` | town braziers, kobolds (OBJ 1) | COMMON/white, SPRITES/boss_eyes_scepter_glow, COMBAT/heart_fire_enemy_eyes, SPRITES/boss_eyes_scepter |
| brown | `#ffffff` | `#8d754a` | `#6f5a34` | `#3f3017` | hero, kobold bodies, chests (OBJ 2) | COMMON/white, SPRITES/wood_dog, SPRITES/chest_outline, SPRITES/npc |
| green | `#ffffff` | `#b4f578` | `#77e331` | `#5c903a` | overworld slimes (OBJ 3) | COMMON/white, SPRITES/slime_highlight, SPRITES/slime_light, SPRITES/slime_dark |

## Sheet anchors (`png2gb --anchor-color` → shade 0)

| Sheet | Anchor | Ref |
|-------|--------|-----|
| `forest-tile` | `#7bb660` | FOREST/grass |
| `desolate` | `#938da1` | DESOLATE/ground |
| `castle-tile` | `#a99fc0` | CASTLE/ground |
| `village-tile` | `#b6a27e` | TOWN/ground |
| `title-red` | `#ffffff` | COMMON/white |
| `npc_tiles` | `#f1eb03` | chroma-key, not a palette color |
| `enemy_sprites` | `#938da1` | DESOLATE/ground |

