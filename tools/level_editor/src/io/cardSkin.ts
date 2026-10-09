/** Battle hand-card skin disk I/O via the vite dev API (singleton record,
 *  screens/cards_skin.json).  Mirrors io/combatArt.ts: all functions throw
 *  on transport failure so callers can show an error. */

/** One battle-card-type visual (BattleCardType order: sword, shield, bow,
 *  heal, dagger).  icon/color are the JSON names battle_compile.py
 *  resolves (ICON_TILES / SKIN_COLORS). */
export interface CardTypeSkin {
  icon: string;
  color: string;
}

/** The battle hand-card skin (screens/cards_skin.json): box geometry, per
 *  type weapon icon + CGB palette, per element-status icon + palette. */
export interface CardSkin {
  $schema?: string;
  id: string;
  label: string;
  box: { w: number; h: number };
  types: Record<'sword' | 'shield' | 'bow' | 'heal' | 'dagger', CardTypeSkin>;
  elements: Record<'fire' | 'ice' | 'poison', CardTypeSkin>;
}

/** Fixed VRAM icon catalog — names are the slugified combat-tileset
 *  description entries (assets/combat-tileset-description.csv -> the
 *  public/tiles/combat slugs); battle_compile.py ICON_TILES enforces the
 *  same set (and checks each slug has an extracted PNG).  Values resolve
 *  to the fixed VRAM tiles ui_init loads (ui.h UI_TILE_CARD_* 104-112).
 *  'amulet' is the one atlas-only icon (no combat tileset entry) and
 *  previews as a text chip. */
export const CARD_ICON_NAMES: string[] = [
  'combat_sword_icon', 'combat_shield_icon', 'combat_bow_icon',
  'combat_dagger_icon', 'combat_ring_icon', 'amulet',
  'combat_top_right_fire_card', 'combat_top_right_ice_card', 'combat_top_right_poison_card',
];

/** CGB BG palette names (UI_COLOR_* 0-7). */
export const CARD_COLOR_NAMES: string[] = [
  'none', 'fire', 'iron', 'field', 'poison', 'wood', 'gold', 'dim',
];

/** Editor preview colors per UI_COLOR name (approximates the ROM CRAM). */
export const CARD_COLOR_HEX: Record<string, string> = {
  none: '#ffffff',
  fire: '#c34e1b',
  iron: '#5a6e8c',
  field: '#7bb660',
  poison: '#7136c1',
  wood: '#b08a5a',
  gold: '#e0a939',
  dim: '#9a9a9a',
};

/** Preview tile PNGs (public/tiles/combat), keyed by the slugified icon
 *  names.  'amulet' is atlas-only (no combat slug), so it previews as a
 *  text chip; every other slug has a real tile image. */
export const CARD_ICON_URL: Record<string, string | null> = {
  combat_sword_icon: '/tiles/combat/combat_sword_icon.png',
  combat_shield_icon: '/tiles/combat/combat_shield_icon.png',
  combat_bow_icon: '/tiles/combat/combat_bow_icon.png',
  combat_dagger_icon: '/tiles/combat/combat_dagger_icon.png',
  combat_ring_icon: '/tiles/combat/combat_ring_icon.png',
  combat_top_right_fire_card: '/tiles/combat/combat_top_right_fire_card.png',
  combat_top_right_ice_card: '/tiles/combat/combat_top_right_ice_card.png',
  combat_top_right_poison_card: '/tiles/combat/combat_top_right_poison_card.png',
  combat_hp_icon: '/tiles/combat/combat_hp_icon.png',
  combat_ap_icon: '/tiles/combat/combat_ap_icon.png',
  combat_deck_icon: '/tiles/combat/combat_deck_icon.png',
  amulet: null,
};

/** Card frame tiles (assets/card_frames.png, tools/compose_card_frames.py
 *  LAYOUT order TL TM TR / L C R / BL BM BR). */
export const CARD_FRAME_URLS: string[] = [
  '/tiles/combat/combat_top_left_card_corner.png',
  '/tiles/combat/combat_top_middle_card.png',
  '/tiles/combat/combat_top_right_card_corner.png',
  '/tiles/combat/combat_left_card_side.png',
  '/tiles/combat/combat_center_card.png',
  '/tiles/combat/combat_right_card_side.png',
  '/tiles/combat/combat_bottom_left_card_corner.png',
  '/tiles/combat/combat_bottom_middle_card.png',
  '/tiles/combat/combat_bottom_right_card_corner.png',
];

export async function fetchCardSkin(): Promise<CardSkin> {
  const res = await fetch('/api/card-skin');
  if (!res.ok) throw new Error(`card-skin returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'card-skin failed');
  return body.data as CardSkin;
}

export async function saveCardSkin(skin: CardSkin): Promise<void> {
  const res = await fetch('/api/save-card-skin', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ data: skin }),
  });
  if (!res.ok) throw new Error(`save card-skin returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save card-skin failed');
}
