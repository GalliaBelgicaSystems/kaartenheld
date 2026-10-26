/** Battle HUD skin + battle-screen layout disk I/O via the vite dev API.
 *  Mirrors io/cardSkin.ts: all functions throw on transport failure. */
import type { CardTypeSkin } from './cardSkin';

/** The battle HUD skin (screens/battle_hud.json). */
export interface BattleHud {
  $schema?: string;
  id: string;
  label: string;
  hp: CardTypeSkin;
  ap: CardTypeSkin;
  deck: CardTypeSkin;
  bar: {
    filled: string;
    empty: string;
    color: string;
    row: number;
    width: number;
  };
}

/** hud_layout keys of screens/battle/<id>.json (BattleScreenDef tail).
 *  Order mirrors battle_compile.py HUD_STRUCT_FIELDS. */
export interface HudLayout {
  turn_banner_row: number;
  enemy_hp_row: number;
  enemy_sprite_row: number;
  enemy_cursor_row: number;
  enemy_col_start: number;
  enemy_col_step: number;
  hero_label_row: number;
  hero_label_col: number;
  hero_hp_row: number;
  hero_hp_col: number;
  deck_row: number;
  deck_col: number;
  ap_row: number;
  ap_col: number;
  combo_row: number;
  cards_row: number;
  card_cursor_row: number;
  card_desc_row: number;
  timer_row: number;
  timer_col: number;
  timer_width: number;
  enemy_row_start: number;
  enemy_row_step: number;
  combo_row_start: number;
  combo_row_step: number;
  caret_x: number;
}

export interface BattleScreen {
  id: string;
  label: string;
  max_enemies: number;
  allowed_categories: string[];
  enemy_positions: Array<{ x: number; y: number }>;
  timer_config: { overworld_ticks: number; battle_ticks: number };
  hud_layout: HudLayout;
}

/** Exactly two battle screens: default (3-enemy standard) and boss
 *  (single centered enemy, art up to 3x3 — used for bosses and
 *  solo-flagged minibosses). */
export const BATTLE_SCREEN_IDS = ['default', 'boss'] as const;

/** HUD icon choices (battle_hud.schema.json iconSkin enum).  Names are the
 *  slugified combat-tileset description entries (assets/
 *  combat-tileset-description.csv) — the same set battle_compile.py
 *  ICON_TILES compiles and checks against extracted PNGs.  'amulet' is
 *  atlas-only and keeps its plain name; the atlas-only coin is not
 *  offered (no combat tile). */
export const HUD_ICON_NAMES: string[] = [
  'combat_hp_icon', 'combat_ap_icon', 'combat_deck_icon',
  'combat_sword_icon', 'combat_shield_icon', 'combat_bow_icon',
  'combat_dagger_icon', 'combat_ring_icon', 'amulet',
  'combat_top_right_fire_card', 'combat_top_right_ice_card', 'combat_top_right_poison_card',
];

/** Timer-bar segment tile choices (any compiled HUD/card icon tile plus
 *  the two dedicated bar segments, VRAM 117/127). */
export const BAR_TILE_NAMES: string[] = [
  'combat_timer_bar_filled', 'combat_timer_bar_empty', ...HUD_ICON_NAMES,
];

export async function fetchBattleHud(): Promise<BattleHud> {
  const res = await fetch('/api/battle-hud');
  if (!res.ok) throw new Error(`battle-hud returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'battle-hud failed');
  return body.data as BattleHud;
}

export async function saveBattleHud(hud: BattleHud): Promise<void> {
  const res = await fetch('/api/save-battle-hud', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ data: hud }),
  });
  if (!res.ok) throw new Error(`save battle-hud returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save battle-hud failed');
}

export async function fetchBattleScreen(id: string): Promise<BattleScreen> {
  const res = await fetch(`/api/battle-screen?id=${encodeURIComponent(id)}`);
  if (!res.ok) throw new Error(`battle-screen ${id} returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || `battle-screen ${id} failed`);
  return body.data as BattleScreen;
}

export async function saveBattleScreen(id: string, data: BattleScreen): Promise<void> {
  const res = await fetch('/api/save-battle-screen', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id, data }),
  });
  if (!res.ok) throw new Error(`save battle-screen ${id} returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || `save battle-screen ${id} failed`);
}
