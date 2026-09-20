/** Combat-art + enemy-type disk I/O via the vite dev API.
 *  Mirrors io/serverLevels.ts: all functions throw on transport failure so
 *  callers can fall back to bundled behavior. */

/** One combat-art set (screens/combat_art/*.json).  Cells are curated tile
 *  names (tools/level_editor/public/tiles/combat/<name>.png, see
 *  tools/compose_battle_sprites.py LAYOUT) or null (blank cell).
 *  frame1 omitted repeats frame0. */
export interface CombatArtSet {
  $schema?: string;
  id: string;
  label: string;
  order: number;
  width: number;
  height: number;
  palette: number;
  frame0: Array<string | null>;
  frame1?: Array<string | null>;
}

export interface CombatArtListItem {
  id: string;
  label: string;
  order: number;
  width: number;
  height: number;
}

export interface EnemyTypeListItem {
  id: string;
  label: string;
  category: string;
  art: string | null;
  ow: boolean;
}

/** Tiles present in the composed battle sheet (assets/battle_sprites.png).
 *  MUST stay in sync with tools/compose_battle_sprites.py LAYOUT: only
 *  these names resolve to sheet cells for the ROM.  Names are the CSV slugs
 *  (import_tileset.py --tileset-id combat): sliced fresh from
 *  assets/combat-tile.png, never hand-renamed.  Extra PNGs in
 *  public/tiles/combat/ need a LAYOUT entry + compose + make gfx first. */
export const SHEET_TILE_NAMES: string[] = [
  'combat_top_left_slime', 'combat_top_middle_slime', 'combat_top_right_slime',
  'combat_bottom_left_slime', 'combat_bottom_middle_slime', 'combat_bottom_right_slime',
  'combat_top_left_slime_2', 'combat_top_middle_slime_2', 'combat_top_right_slime_2',
  'combat_top_left_bat', 'combat_top_middle_bat', 'combat_top_right_bat',
  'combat_bottom_left_bat', 'combat_bottom_middle_bat', 'combat_bottom_right_bat',
  'combat_top_left_boss', 'combat_top_middle_boss', 'combat_top_right_boss',
  'combat_left_middle_boss', 'combat_middle_center_boss', 'combat_middle_right_boss',
  'combat_bottom_left_boss', 'combat_bottom_middle_boss', 'combat_bottom_right_boss',
  'combat_top_left_mimic', 'combat_top_middle_mimic', 'combat_top_right_mimic',
  'combat_bottom_left_mimic', 'combat_bottom_middle_mimic', 'combat_bottom_right_mimic',
  'combat_left_middle_boss_2', 'combat_middle_center_boss_2', 'combat_middle_right_boss_2',
];

/** Every combat tile (tools/level_editor/public/tiles/combat/*.png)
 *  that the Combat Art Studio brush exposes.  A superset of
 *  SHEET_TILE_NAMES: tiles missing from the composed sheet (cards, icons,
 *  HUD, digits, statuses, kobold, spider, boss glow-eyes) are paintable but
 *  are flagged "not compiled to ROM" until a LAYOUT entry + compose +
 *  make gfx is added.  Names are CSV slugs, kept in sync with the directory
 *  listing; the source tileset (assets/combat-tile.png, 16x5 = 80 tiles,
 *  minus 11 blank cells) is described in
 *  assets/combat-tileset-description.csv. */
export const COMBAT_BRUSH_NAMES: string[] = [
  ...SHEET_TILE_NAMES,
  'combat_sword_icon', 'combat_bow_icon', 'combat_shield_icon',
  'combat_top_left_card_corner', 'combat_top_middle_card', 'combat_top_right_card_corner',
  'combat_left_card_side', 'combat_right_card_side',
  'combat_bottom_left_card_corner', 'combat_bottom_middle_card', 'combat_bottom_right_card_corner',
  'combat_center_card',
  'combat_one_icon', 'combat_two_icon', 'combat_three_icon', 'combat_four_icon',
  'combat_top_right_poison_card', 'combat_top_right_fire_card', 'combat_top_right_ice_card',
  'combat_arrow_pointing_up', 'combat_hp_icon', 'combat_ap_icon',
  'combat_deck_icon', 'combat_hero_icon',
  'combat_top_left_kobold', 'combat_top_middle_kobold', 'combat_top_right_kobold',
  'combat_bottom_left_kobold', 'combat_bottom_middle_kobold', 'combat_bottom_right_kobold',
  'combat_top_left_spider', 'combat_top_middle_spider', 'combat_top_right_spider',
  'combat_bottom_left_spider', 'combat_bottom_middle_spider', 'combat_bottom_right_spider',
  'combat_left_middle_boss_2', 'combat_middle_center_boss_2', 'combat_middle_right_boss_2',
];

export const COMBAT_TILE_URL = (name: string) => `/tiles/combat/${name}.png`;

export const MAX_ART_W = 6;
export const MAX_ART_H = 4;

export function frameCells(set: CombatArtSet, frame: 0 | 1): Array<string | null> {
  if (frame === 1 && set.frame1) return set.frame1;
  return set.frame0;
}

export async function fetchCombatArtList(): Promise<CombatArtListItem[]> {
  const res = await fetch('/api/combat-art');
  if (!res.ok) throw new Error(`combat-art list returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'combat-art list failed');
  return (body.items || []).sort((a: CombatArtListItem, b: CombatArtListItem) => a.order - b.order);
}

export async function fetchCombatArtSet(id: string): Promise<CombatArtSet> {
  const res = await fetch(`/api/combat-art-set?id=${encodeURIComponent(id)}`);
  if (!res.ok) throw new Error(`combat-art ${id} returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || `combat-art ${id} failed`);
  return body.data as CombatArtSet;
}

export async function saveCombatArtSet(set: CombatArtSet): Promise<void> {
  const res = await fetch('/api/save-combat-art', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: set.id, data: set }),
  });
  if (!res.ok) throw new Error(`save combat-art returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save combat-art failed');
}

export async function fetchEnemyTypeList(): Promise<EnemyTypeListItem[]> {
  const res = await fetch('/api/enemy-types');
  if (!res.ok) throw new Error(`enemy-types list returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'enemy-types list failed');
  return body.items || [];
}

export async function fetchEnemyType(id: string): Promise<any> {
  const res = await fetch(`/api/enemy-type?id=${encodeURIComponent(id)}`);
  if (!res.ok) throw new Error(`enemy-type ${id} returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || `enemy-type ${id} failed`);
  return body.data;
}

export async function saveEnemyType(id: string, data: any): Promise<void> {
  const res = await fetch('/api/save-enemy-type', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id, data }),
  });
  if (!res.ok) throw new Error(`save enemy-type returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save enemy-type failed');
}
