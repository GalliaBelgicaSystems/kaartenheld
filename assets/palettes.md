# Defined palettes (generated — do not edit by hand)

Source of truth: `assets/palette.txt`, read by
`tools/palette_txt.py`.  Regenerate with `make manifest`
(the `palette-check` gate fails on drift).  Every shade
cites the `palette.txt` reference it resolves from; artist
tutorial: `assets/palette_tutorial.md`.

## Base — battle / card UI (`cgb_bg_palettes`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | fight1 | `#ffffff` | `#dfbd8d` | `#e3ae63` | `#b09266` | card UI, spider art | COMBAT/background, COMBAT/card_background, COMBAT/card_gold, COMBAT/card_outline |
| 1 | fightgoblin | `#ffffff` | `#c34e1b` | `#844f4f` | `#4a2727` | burn cards, kobold art | COMBAT/background, COMBAT/heart_fire_enemy_eyes, COMBAT/goblin_combat_skin, COMBAT/goblin_combat_skin_dark_outlines |
| 2 | fightspider | `#ffffff` | `#8f8f8f` | `#5a4a3d` | `#000000` | sword/freeze cards | COMBAT/background, COMBAT/spider_combat_knife_blade_light, COMBAT/spider_combat_knife_handle, COMBAT/boss_spider_bat_combat_body |
| 3 | fightslime | `#ffffff` | `#89cc5e` | `#5c903a` | `#000000` | slime art, heal cards | COMBAT/background, COMBAT/slime_combat, COMBAT/slime_outline_combat, COMBAT/boss_spider_bat_combat_body |
| 4 | fight5 | `#ffffff` | `#dfbd8d` | `#b09266` | `#7136c1` | poison cards | COMBAT/background, COMBAT/card_background, COMBAT/card_outline, COMBAT/poison_icon |
| 5 | fightmimic | `#ffffff` | `#8d754a` | `#6f5a34` | `#3f3017` | shield cards, mimic art | COMBAT/background, COMBAT/mimic_combat_body_light, COMBAT/mimic_combat_body_dark, COMBAT/mimic_combat_mouth |
| 6 | fight_text | `#ffffff` | `#8f8f8f` | `#565656` | `#000000` | text (black ink) | COMBAT/background, COMBAT/spider_combat_knife_blade_light, COMBAT/boss_combat_body_light, COMBAT/boss_spider_bat_combat_body |
| 7 | fightboss | `#ffffff` | `#8d754a` | `#565656` | `#000000` | boss/bat art | COMBAT/background, COMBAT/mimic_combat_body_light, COMBAT/boss_combat_body_light, COMBAT/boss_spider_bat_combat_body |

## Forest / field (`cgb_bg_palettes_forest`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | field1 | `#7bb660` | `#2a4f1a` | `#4a3b1c` | `#000000` | unused | FOREST/grass, FOREST/tree_leaves_terrain_outline_big_grass, FOREST/tree_ladder_and_dirt, FOREST/void_holes |
| 1 | field6 | `#7bb660` | `#edc214` | `#d7a726` | `#26232e` | forest fires | FOREST/grass, FOREST/fire_light, FOREST/fire_dark, FOREST/rocks_outline_fire_base |
| 2 | field4 | `#7bb660` | `#937b4a` | `#2a4f1a` | `#4a3b1c` | unused | FOREST/grass, FOREST/tree_stump_light, FOREST/tree_leaves_terrain_outline_big_grass, FOREST/tree_ladder_and_dirt |
| 3 | field2 | `#7bb660` | `#2a4f1a` | `#1d3e0f` | `#4a3b1c` | field/ground + canopy | FOREST/grass, FOREST/tree_leaves_terrain_outline_big_grass, FOREST/dark_tree_leaves, FOREST/tree_ladder_and_dirt |
| 4 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 5 | field3 | `#7bb660` | `#937b4a` | `#614e27` | `#4a3b1c` | trunks/stumps/merchant (override) | FOREST/grass, FOREST/tree_stump_light, FOREST/tree_stump_dark, FOREST/tree_ladder_and_dirt |
| 6 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 7 | field5 | `#7bb660` | `#a99fc0` | `#26232e` | `#26232e` | rocks | FOREST/grass, FOREST/rocks, FOREST/rocks_outline_fire_base, UNUSED |

## Desolate (`cgb_bg_palettes_desolate`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | desolate2 | `#a99fc0` | `#938da1` | `#837b96` | `#26232e` | dark ground (auto) | DESOLATE/rocks_stone_light, DESOLATE/ground, DESOLATE/rocks_stone_shadow, DESOLATE/tree_item_outlines_cracks_fire_base |
| 1 | desolate3 | `#938da1` | `#edc214` | `#d7a726` | `#26232e` | campfire | DESOLATE/ground, DESOLATE/fire_light, DESOLATE/fire_dark, DESOLATE/tree_item_outlines_cracks_fire_base |
| 2 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 3 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 4 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 5 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 6 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | chest (MISSING) | UNUSED slot (magenta canary) |
| 7 | desolate1 | `#938da1` | `#3f3a4a` | `#26232e` | `#000000` | slate ground | DESOLATE/ground, DESOLATE/terrain_side, DESOLATE/tree_item_outlines_cracks_fire_base, DESOLATE/void |

## Castle (`cgb_bg_palettes_castle`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | castle4 | `#d7d7d7` | `#828282` | `#565656` | `#2e2e2e` | stone | CASTLE/ground, CASTLE/wall_light, CASTLE/wall_dark, CASTLE/window_frame |
| 1 | castle3 | `#d7d7d7` | `#d7a726` | `#8b1b1b` | `#621212` | curtains | CASTLE/ground, CASTLE/gold, CASTLE/curtain_light, CASTLE/curtain_dark |
| 2 | castle1 | `#d7d7d7` | `#b3b0b0` | `#828282` | `#565656` | unused | CASTLE/ground, CASTLE/stairs, CASTLE/wall_light, CASTLE/wall_dark |
| 3 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 4 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 5 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | furniture (MISSING) | UNUSED slot (magenta canary) |
| 6 | castle2 | `#d7d7d7` | `#8d754a` | `#6f5a34` | `#6f5a34` | gold/chest | CASTLE/ground, CASTLE/wood_light, CASTLE/wood_dark, UNUSED |
| 7 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |

## Village / town (`cgb_bg_palettes_village`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | town4 | `#b6a27e` | `#b6b6b6` | `#939393` | `#000000` | unused | TOWN/ground, TOWN/puddle_stone, TOWN/stone_dark, TOWN/holes |
| 1 | town3 | `#b6a27e` | `#edc214` | `#d7a726` | `#26232e` | braziers | TOWN/ground, TOWN/fire_light, TOWN/fire_dark, TOWN/fire_base |
| 2 | town5 | `#b6a27e` | `#b6b6b6` | `#939393` | `#645233` | unused | TOWN/ground, TOWN/puddle_stone, TOWN/stone_dark, TOWN/wood_outlines_rubble |
| 3 | town6 | `#b6a27e` | `#b6b6b6` | `#8d754a` | `#645233` | dirt ground (MISSING) | TOWN/ground, TOWN/puddle_stone, TOWN/barrel_wood_fence, TOWN/wood_outlines_rubble |
| 4 | town7 | `#f1cf91` | `#645233` | `#000000` | `#000000` | unused | TOWN/house_wall, TOWN/wood_outlines_rubble, TOWN/holes, UNUSED |
| 5 | town1 | `#b6a27e` | `#8d754a` | `#79643e` | `#645233` | houses/merchant | TOWN/ground, TOWN/barrel_wood_fence, TOWN/fence_light, TOWN/wood_outlines_rubble |
| 6 | town2 | `#b6a27e` | `#f1cf91` | `#ccaa6c` | `#645233` | walls/mayor | TOWN/ground, TOWN/house_wall, TOWN/house_roof_shade, TOWN/wood_outlines_rubble |
| 7 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |

## OBJ sprite ramps (`src/ui/ui.c`)

| Name | S0 | S1 | S2 | S3 | Serves | Refs |
|------|----|----|----|----|--------|------|
| sprites | `#f1eb03` | `#673c3c` | `#565656` | `#000000` | overworld slimes (OBJ 3) | SPRITES/background, SPRITES/enemy, SPRITES/spider_boss_light, SPRITES/character_main_outline_bat |
| sprites5 | `#f1eb03` | `#ac9d23` | `#8d754a` | `#6f5a34` |  | SPRITES/background, SPRITES/chest_lock, SPRITES/wood_dog, SPRITES/chest_outline |
| sprites8 | `#f1eb03` | `#b6b6b6` | `#8b1b1b` | `#3f3017` |  | SPRITES/background, SPRITES/npc_beard, SPRITES/boss_eyes_scepter, SPRITES/npc |
| sprites11 | `#f1eb03` | `#77e331` | `#5c903a` | `#5c903a` |  | SPRITES/background, SPRITES/slime_light, SPRITES/slime_dark, UNUSED |
| battle_slime | `#f1eb03` | `#89cc5e` | `#5c903a` | `#000000` | battle slimes OAM pilot (OBJ 4) | SPRITES/background, SPRITES/battle_slime_body, SPRITES/battle_slime_edge, SPRITES/character_main_outline_bat |
| sprites2 | `#f1eb03` | `#8b1b1b` | `#565656` | `#000000` |  | SPRITES/background, SPRITES/boss_eyes_scepter, SPRITES/spider_boss_light, SPRITES/character_main_outline_bat |
| sprites6 | `#f1eb03` | `#8d754a` | `#6f5a34` | `#3f3017` |  | SPRITES/background, SPRITES/wood_dog, SPRITES/chest_outline, SPRITES/npc |
| battle_kobold | `#f1eb03` | `#844f4f` | `#4a2727` | `#4a2727` |  | SPRITES/background, SPRITES/battle_kobold_skin, SPRITES/battle_kobold_dark, SPRITES/battle_kobold_dark |

## Sheet background convention (index 0 of the tile's ramp → shade 0)

No tool pins a color to shade 0 by guessing: every tile encodes
through its explicitly assigned ramp (tileset JSON `palette`,
combat-art `palette`/`obj_palette`, enemy `overworld.palette`),
and entry 0 of that ramp is shade 0. Sheet background cells use
the ramp's entry-0 color (`ANCHORS` below records the convention
per sheet for the artist). Off-ramp pixels fail loudly.

| Sheet | Anchor | Ref |
|-------|--------|-----|
| `forest-tile` | `#7bb660` | FOREST/grass |
| `desolate` | `#938da1` | DESOLATE/ground |
| `castle-tile` | `#d7d7d7` | CASTLE/ground |
| `village-tile` | `#b6a27e` | TOWN/ground |
| `title-red` | `#ffffff` | TITLE/title_bg |
| `npc_tiles` | `#f1eb03` | SPRITES/background |
| `enemy_sprites` | `#938da1` | DESOLATE/ground |

