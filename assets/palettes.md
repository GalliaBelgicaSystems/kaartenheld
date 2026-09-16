# Defined palettes (generated — do not edit by hand)

Source of truth: `assets/palette.txt`, read by
`tools/palette_txt.py`.  Regenerate with `make manifest`
(the `palette-check` gate fails on drift).  Every shade
cites the `palette.txt` reference it resolves from; artist
tutorial: `assets/palette_tutorial.md`.

## Base — battle / card UI (`cgb_bg_palettes`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | fight_card | `#ffffff` | `#dfbd8d` | `#e3ae63` | `#b09266` | card UI, spider art | COMBAT/background, COMBAT/card_background, COMBAT/card_decor, COMBAT/card_outline |
| 1 | fight_more | `#ffffff` | `#c34e1b` | `#844f4f` | `#4a2727` | burn cards, kobold art | COMBAT/background, COMBAT/heart_fire_enemy_eyes, COMBAT/goblin_combat_skin, COMBAT/goblin_combat_skin_dark_outlines |
| 2 | fight_again | `#ffffff` | `#8f8f8f` | `#565656` | `#5a4a3d` | sword/freeze cards | COMBAT/background, COMBAT/spider_combat_knife_blade_light, COMBAT/spider_combat_knife_blade_dark, COMBAT/spider_combat_knife_handle |
| 3 | fight_standard | `#ffffff` | `#89cc5e` | `#5c903a` | `#5c903a` | slime art, heal cards | COMBAT/background, COMBAT/slime_combat, COMBAT/slime_outline_combat, COMBAT/slime_outline_combat |
| 4 | fight_forever | `#ffffff` | `#7136c1` | `#565656` | `#3d3044` | poison cards | COMBAT/background, COMBAT/poison_icon, COMBAT/boss_combat_body_light, COMBAT/bat_combat_inner_wings |
| 5 | fight_still | `#ffffff` | `#8d754a` | `#755930` | `#6f5a34` | shield cards, mimic art | COMBAT/background, COMBAT/mimic_combat_body_light, COMBAT/numbers_icons, COMBAT/mimic_combat_body_dark |
| 6 | fight_card | `#ffffff` | `#dfbd8d` | `#e3ae63` | `#b09266` | bow cards (MISSING) | duplicates `fight_card` (FLORENT: author a real ramp for UI_COLOR_* card/UI spans) |
| 7 | fight_final | `#ffffff` | `#3f3017` | `#000000` | `#000000` | boss/bat art | COMBAT/background, COMBAT/mimic_combat_mouth, COMBAT/boss_spider_bat_combat_body, UNUSED |

## Forest / field (`cgb_bg_palettes_forest`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 1 | field_more | `#7bb660` | `#edc214` | `#d7a726` | `#7bb660` | forest fires | FOREST/grass, FOREST/fire_light, FOREST/fire_dark, UNUSED |
| 2 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 3 | field | `#7bb660` | `#2a4f1a` | `#1d3e0f` | `#000000` | field/ground + canopy | FOREST/grass, FOREST/tree_leaves_terrain_outline_big_grass, FOREST/dark_tree_leaves, FOREST/void_holes |
| 4 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 5 | field_too | `#7bb660` | `#937b4a` | `#614e27` | `#614e27` | trunks/stumps/merchant (override) | FOREST/grass, FOREST/tree_stump_light, FOREST/tree_stump_dark, UNUSED |
| 6 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 7 | field_again | `#7bb660` | `#a99fc0` | `#4a3b1c` | `#26232e` | rocks | FOREST/grass, FOREST/rocks, FOREST/tree_ladder_and_dirt, FOREST/rocks_outline_fire_base |

## Desolate (`cgb_bg_palettes_desolate`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | underworld_dark | `#938da1` | `#26232e` | `#000000` | `#000000` | dark ground (auto) | DESOLATE/ground, DESOLATE/tree_item_outlines_cracks_fire_base, DESOLATE/void, UNUSED (auto-assigned) |
| 1 | Underworld_light | `#938da1` | `#edc214` | `#d7a726` | `#938da1` | campfire | DESOLATE/ground, DESOLATE/fire_light, DESOLATE/fire_dark, UNUSED |
| 2 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 3 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 4 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 5 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 6 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | chest (MISSING) | UNUSED slot (magenta canary) |
| 7 | underworld | `#938da1` | `#a99fc0` | `#837b96` | `#3f3a4a` | slate ground | DESOLATE/ground, DESOLATE/rocks_stone_light, DESOLATE/rocks_stone_shadow, DESOLATE/terrain_side |

## Castle (`cgb_bg_palettes_castle`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | castle | `#d7d7d7` | `#828282` | `#565656` | `#2e2e2e` | stone | CASTLE/ground, CASTLE/wall_light, CASTLE/wall_dark, CASTLE/window_frame |
| 1 | castle_room | `#d7d7d7` | `#b3b0b0` | `#8b1b1b` | `#621212` | curtains | CASTLE/ground, CASTLE/stairs, CASTLE/curtain_light, CASTLE/curtain_dark |
| 2 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 3 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 4 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 5 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | furniture (MISSING) | UNUSED slot (magenta canary) |
| 6 | castle_hall | `#d7d7d7` | `#d7a726` | `#8d754a` | `#6f5a34` | gold/chest | CASTLE/ground, CASTLE/gold, CASTLE/wood_light, CASTLE/wood_dark |
| 7 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |

## Village / town (`cgb_bg_palettes_village`)

| # | Name | S0 | S1 | S2 | S3 | Serves | Refs |
|---|------|----|----|----|----|--------|------|
| 0 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 1 | city | `#b6a27e` | `#edc214` | `#d7a726` | `#26232e` | braziers | TOWN/ground, TOWN/fire_light, TOWN/fire_dark, TOWN/fire_base |
| 2 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 3 | city | `#b6a27e` | `#edc214` | `#d7a726` | `#26232e` | dirt ground (MISSING) | duplicates `city` (FLORENT: author a real ramp for npc overlay 0, npc overlay 1, npc overlay 4, npc overlay 5) |
| 4 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |
| 5 | village | `#b6a27e` | `#8d754a` | `#79643e` | `#645233` | houses/merchant | TOWN/ground, TOWN/barrel_wood_fence, TOWN/fence_light, TOWN/wood_outlines |
| 6 | town | `#b6a27e` | `#f1cf91` | `#b6b6b6` | `#ccaa6c` | walls/mayor | TOWN/ground, TOWN/house_wall, TOWN/puddle_stone_window, TOWN/house_roof_shade |
| 7 | unused | `#ff00ff` | `#ff00ff` | `#ff00ff` | `#ff00ff` | unused | UNUSED slot (magenta canary) |

## OBJ sprite ramps (`src/ui/ui.c`)

| Name | S0 | S1 | S2 | S3 | Serves | Refs |
|------|----|----|----|----|--------|------|
| grey | `#f1eb03` | `#b6b6b6` | `#565656` | `#000000` | UNUSED slot (no grey ramp shipped) | SPRITES/background, SPRITES/npc_beard, SPRITES/spider_boss_light, SPRITES/character_main_outline_bat |
| more_sprites | `#f1eb03` | `#ac9d23` | `#f57137` | `#8d754a` | town braziers (OBJ 1) | SPRITES/background, SPRITES/chest_lock, SPRITES/boss_eyes_scepter_glow, SPRITES/wood_dog |
| sprites_again | `#f1eb03` | `#673c3c` | `#8b1b1b` | `#3f3017` | kobolds/dogs/hero (OBJ 2) | SPRITES/background, SPRITES/enemy, SPRITES/dogtongue_ribbon, SPRITES/npc |
| sprites | `#f1eb03` | `#b6b6b6` | `#77e331` | `#5c903a` | overworld slimes (OBJ 3) | SPRITES/background, SPRITES/npc_beard, SPRITES/slime_light, SPRITES/slime_dark |

## Sheet anchors (`png2gb --anchor-color` → shade 0)

| Sheet | Anchor | Ref |
|-------|--------|-----|
| `forest-tile` | `#7bb660` | FOREST/grass |
| `desolate` | `#938da1` | DESOLATE/ground |
| `castle-tile` | `#d7d7d7` | CASTLE/ground |
| `village-tile` | `#b6a27e` | TOWN/ground |
| `title-red` | `#ffffff` | TITLE/title_bg |
| `npc_tiles` | `#f1eb03` | SPRITES/background |
| `enemy_sprites` | `#938da1` | DESOLATE/ground |

