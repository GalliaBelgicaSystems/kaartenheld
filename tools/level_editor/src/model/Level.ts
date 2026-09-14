import { LevelObject } from './Objects';
import { getTileset } from './Tileset';

export interface LevelExit {
  x: number;
  y: number;
  target_scene: string;
  target_x: number;
  target_y: number;
  direction?: 'NORTH' | 'SOUTH' | 'EAST' | 'WEST' | string;
  tile_char?: string;
}

/** Whole-edge map links (the "ocean"): stepping onto a non-wall cell of
 *  a linked border crosses to the neighbor scene with a mirrored entry
 *  spawn. Empty string = no link. A point exit on the same cell wins. */
export interface LevelNeighbors {
  north?: string;
  south?: string;
  east?: string;
  west?: string;
}

export const NEIGHBOR_DIRS = ['north', 'south', 'east', 'west'] as const;

export function normalizeNeighbors(raw: any): LevelNeighbors {
  const src = (raw && typeof raw === 'object' ? raw : {}) as Record<string, unknown>;
  const out: LevelNeighbors = {};
  for (const d of NEIGHBOR_DIRS) {
    const v = src[d];
    out[d] = typeof v === 'string' ? v : '';
  }
  return out;
}

export interface LevelRegion {
  id: string;
  bounds: {
    x: number;
    y: number;
    width: number;
    height: number;
  };
  description?: string;
  gameplay?: {
    purpose?: string;
    difficulty?: number;
  };
}

export interface PlayerSpawn {
  x: number;
  y: number;
  facing?: 'UP' | 'DOWN' | 'LEFT' | 'RIGHT' | string;
  animation_frames?: string[];
  animation_speed?: number;
}

export interface MapMetadata {
  width: number;
  height: number;
  tileset: string;
  music?: string;
  map_id?: string;
}

export interface TerrainBlock {
  x: number;
  y: number;
  width: number;
  height: number;
  tile: string;
  comment?: string;
}

export interface LevelData {
  id: string;
  name: string;
  map: MapMetadata;
  layers: {
    terrain: string[][] | TerrainBlock[];
    blocks?: TerrainBlock[];
  };
  player: {
    spawn: PlayerSpawn;
  };
  exits: LevelExit[];
  objects: LevelObject[];
  regions: LevelRegion[];
}

export interface EditorLevel {
  id: string;
  name: string;
  width: number;
  height: number;
  tileset: string;
  music: string;
  mapId: string;
  grid: string[][]; // tile names, e.g. "tree", "floor"
  // Level's default_walkable (ground for unpainted cells), preserved
  // verbatim through load/save round-trips.
  defaultWalkable?: string;
  // Terrain representation fidelity (JSON is the source of truth both
  // ways): the grid above is the editing model, but on save the level is
  // written back in the form it was loaded in -- unless the terrain was
  // actually painted (terrainDirty), in which case the edited grid is
  // emitted (the compiler accepts both forms; decompile normalizes grids
  // back to verbatim blocks).  View-only saves are therefore byte-stable.
  terrainSource: 'blocks' | 'grid';
  terrainBlocks: TerrainBlock[];
  terrainDirty: boolean;
  spawn: PlayerSpawn;
  exits: LevelExit[];
  /** Whole-edge links; absent on screens (levels only). */
  neighbors?: LevelNeighbors;
  objects: LevelObject[];
  regions: LevelRegion[];
  isScreen?: boolean;
  battleHudLayout?: {
    turn_banner_row?: number;
    enemy_hp_row?: number;
    enemy_sprite_row?: number;
    enemy_cursor_row?: number;
    enemy_col_start?: number;
    enemy_col_step?: number;
    hero_label_row?: number;
    hero_label_col?: number;
    hero_hp_row?: number;
    hero_hp_col?: number;
    deck_row?: number;
    deck_col?: number;
    ap_row?: number;
    ap_col?: number;
    combo_row?: number;
    cards_row?: number;
    card_cursor_row?: number;
    card_desc_row?: number;
    timer_row?: number;
    timer_col?: number;
    timer_width?: number;
  };
  titleLayout?: {
    title?: string;
    logo?: { x: number; y: number; lines: string[] };
    logoImage?: { enabled: boolean; url?: string; x: number; y: number; width: number; height: number; palette?: number };
    subtitle?: { text: string; y: number; align?: 'left' | 'center' | 'right' };
    graphic?: { enabled: boolean; x: number; y: number; width: number; height: number; lines: string[] };
    prompt?: { text: string; x: number; y: number; align?: 'left' | 'center' | 'right' };
    credits?: { enabled: boolean; text: string; x: number; y: number; align?: 'left' | 'center' | 'right' };
    menu?: { x: number; caret_x: number; first_row: number; row_step: number; options: string[] };
  };
  originalScreenData?: any;
}

export function titleScreenToEditor(data: any): EditorLevel {
  const w = data.grid_width || 20;
  const h = data.grid_height || 18;
  const tilesetId = 'interior';
  const grid: string[][] = [];

  for (let y = 0; y < h; y++) {
    const row: string[] = [];
    for (let x = 0; x < w; x++) {
      if (y === 0 || y === h - 1 || x === 0 || x === w - 1) {
        row.push('wall');
      } else {
        row.push('floor');
      }
    }
    grid.push(row);
  }

  const objects: LevelObject[] = [
    {
      id: 'title_logo',
      type: 'npc',
      position: { x: data.logo?.x ?? 2, y: data.logo?.y ?? 2 },
      properties: {
        sprite_width: 2,
        sprite_height: 1,
        display_name: data.logo?.lines?.[0]?.trim() || data.title || 'KAARTENHELD',
        dialogue_id: data.logo?.lines ? data.logo.lines.join('\n') : data.title
      }
    },
    {
      id: 'title_prompt',
      type: 'npc',
      position: { x: data.prompt?.x ?? 5, y: data.prompt?.y ?? 16 },
      properties: {
        display_name: data.prompt?.text || 'PRESS START',
        dialogue_id: 'Press START'
      }
    }
  ];

  if (Array.isArray(data.menu?.options)) {
    data.menu.options.forEach((opt: string, idx: number) => {
      const optY = (data.menu.first_row ?? 10) + idx * (data.menu.row_step ?? 2);
      objects.push({
        id: `title_menu_opt_${idx + 1}`,
        type: 'npc',
        position: { x: data.menu.x ?? 3, y: optY },
        properties: {
          display_name: opt,
          dialogue_id: `Option ${idx + 1}: ${opt}`
        }
      });
    });
  }

  const regions: LevelRegion[] = [
    {
      id: 'logo_banner',
      bounds: { x: 1, y: 1, width: 18, height: 6 },
      description: 'Title Screen Logo & Subtitle'
    },
    {
      id: 'menu_zone',
      bounds: { x: 2, y: 9, width: 16, height: 8 },
      description: 'Title Menu Options'
    }
  ];

  return {
    id: 'title',
    name: data.title ? `Title Screen (${data.title})` : 'Title Screen',
    width: w,
    height: h,
    tileset: tilesetId,
    music: 'MUSIC_TITLE',
    mapId: 'SCREEN_TITLE',
    grid,
    terrainSource: 'grid',
    terrainBlocks: [],
    terrainDirty: false,
    spawn: { x: data.menu?.caret_x ?? 3, y: data.menu?.first_row ?? 10, facing: 'RIGHT' },
    exits: [],
    objects,
    regions,
    isScreen: true,
    titleLayout: {
      title: data.title || 'Kaartenheld',
      logo: data.logo ? JSON.parse(JSON.stringify(data.logo)) : { x: 0, y: 1, lines: [] },
      logoImage: data.logo_image ? JSON.parse(JSON.stringify(data.logo_image)) : { enabled: false, url: '/tiles/title/logo.png', x: 2, y: 1, width: 16, height: 3, palette: 1 },
      subtitle: data.subtitle ? JSON.parse(JSON.stringify(data.subtitle)) : { text: 'BATTLE DEMO', y: 5, align: 'center' },
      graphic: data.graphic ? JSON.parse(JSON.stringify(data.graphic)) : { enabled: true, x: 2, y: 7, width: 16, height: 5, lines: [] },
      prompt: data.prompt ? JSON.parse(JSON.stringify(data.prompt)) : { text: 'PRESS START', x: 4, y: 14, align: 'center' },
      credits: data.credits ? JSON.parse(JSON.stringify(data.credits)) : { enabled: false, text: 'GALLIA BELGICA', x: 6, y: 17, align: 'right' },
      menu: data.menu ? JSON.parse(JSON.stringify(data.menu)) : { x: 3, caret_x: 3, first_row: 10, row_step: 2, options: [] },
    },
    originalScreenData: data
  };
}

export function battleScreenToEditor(data: any): EditorLevel {
  const w = data.grid_width || 20;
  const h = data.grid_height || 18;
  const tilesetId = 'dungeon';
  const grid: string[][] = [];

  for (let y = 0; y < h; y++) {
    const row: string[] = [];
    for (let x = 0; x < w; x++) {
      if (y === 0 || y === h - 1 || x === 0 || x === w - 1) {
        row.push('wall_top');
      } else {
        row.push('floor');
      }
    }
    grid.push(row);
  }

  const objects: LevelObject[] = [];
  const enemiesList = Array.isArray(data.enemies)
    ? data.enemies
    : Array.isArray(data.enemy_positions)
    ? data.enemy_positions.map((p: any, i: number) => ({ label: `ENEMY_${i+1}`, x: p.x, y: p.y, hp: 10, max_hp: 10 }))
    : [];

  enemiesList.forEach((e: any, idx: number) => {
    // Enemy objects keep the JSON's true x/y: saves write positions back
    // verbatim, so a no-touch save must not move them (the old 3+idx*5
    // placement rewrote enemy_positions x on every save).
    const ex = typeof e.x === 'number' ? e.x : 3 + idx * 5;
    const ey = typeof e.y === 'number' ? e.y : (2 + idx);
    objects.push({
      id: `battle_enemy_${idx + 1}`,
      type: 'enemy',
      overworld_sprite: 'desolate_landscape.desolate_kobold_01',
      battle_sprite: (e.label || 'slime').toLowerCase(),
      battle_name: e.label || `ENEMY ${idx + 1}`,
      position: { x: ex, y: ey },
      properties: {
        entity_id: `ENTITY_ID_${(e.label || 'SLIME').toUpperCase()}`,
        display_name: `${e.label || 'ENEMY'} (${e.hp || 10}/${e.max_hp || 10} HP)`,
        battle: 'BATTLE_DEFAULT'
      }
    });
  });

  const regions: LevelRegion[] = [
    {
      id: 'enemy_hud',
      bounds: { x: 1, y: 1, width: 18, height: 4 },
      description: 'Enemy Roster & Target Status'
    },
    {
      id: 'deck_hud',
      bounds: { x: 1, y: 6, width: 18, height: 5 },
      description: 'Hand Deck: 5 Cards (FIRE_SLASH, WATER_GUARD, HEAL, SHIELD, ENERGY)'
    },
    {
      id: 'combo_hud',
      bounds: { x: 1, y: 12, width: 18, height: 2 },
      description: 'Poker Hand Combos (Pair, Flush, Straight)'
    },
    {
      id: 'timer_hud',
      bounds: { x: 1, y: 15, width: 18, height: 2 },
      description: 'AP Turn Timer Bar'
    }
  ];

  return {
    id: data.id || 'battle',
    name: data.title || data.label || 'Battle Screen',
    width: w,
    height: h,
    tileset: tilesetId,
    music: 'MUSIC_BATTLE',
    mapId: 'SCREEN_BATTLE',
    grid,
    terrainSource: 'grid',
    terrainBlocks: [],
    terrainDirty: false,
    spawn: { x: data.hud_layout?.caret_x ?? 2, y: data.hud_layout?.timer_row ?? 15, facing: 'RIGHT' },
    exits: [],
    objects,
    regions,
    isScreen: true,
    battleHudLayout: {
      turn_banner_row: data.hud_layout?.turn_banner_row ?? 0,
      enemy_hp_row: data.hud_layout?.enemy_hp_row ?? 1,
      enemy_sprite_row: data.hud_layout?.enemy_sprite_row ?? 2,
      enemy_cursor_row: data.hud_layout?.enemy_cursor_row ?? 4,
      enemy_col_start: data.hud_layout?.enemy_col_start ?? 0,
      enemy_col_step: data.hud_layout?.enemy_col_step ?? 7,
      hero_label_row: data.hud_layout?.hero_label_row ?? 6,
      hero_label_col: data.hud_layout?.hero_label_col ?? 1,
      hero_hp_row: data.hud_layout?.hero_hp_row ?? 6,
      hero_hp_col: data.hud_layout?.hero_hp_col ?? 13,
      deck_row: data.hud_layout?.deck_row ?? 7,
      deck_col: data.hud_layout?.deck_col ?? 1,
      ap_row: data.hud_layout?.ap_row ?? 7,
      ap_col: data.hud_layout?.ap_col ?? 13,
      combo_row: data.hud_layout?.combo_row ?? 9,
      cards_row: data.hud_layout?.cards_row ?? 10,
      card_cursor_row: data.hud_layout?.card_cursor_row ?? 14,
      card_desc_row: data.hud_layout?.card_desc_row ?? 15,
      timer_row: data.hud_layout?.timer_row ?? 16,
      timer_col: data.hud_layout?.timer_col ?? 0,
      timer_width: data.hud_layout?.timer_width ?? 20,
    },
    originalScreenData: data
  };
}
export function createEmptyEditorLevel(id: string = 'new_scene', tilesetId: string = 'forest', width: number = 20, height: number = 18): EditorLevel {
  const grid: string[][] = [];
  const tileset = getTileset(tilesetId);
  // Fresh maps fill with the plain ground (first tile with 'plain' in the
  // id), matching the level JSON default_walkable rule.  Tiles without any
  // plain tile fall back to the first walkable tile.
  const plainFloor = tileset.tiles.find(t => t.id.includes('plain'))?.id
    || tileset.tiles.find(t => t.walkable)?.id || tileset.tiles[0]?.id || 'floor';

  for (let y = 0; y < height; y++) {
    const row: string[] = [];
    for (let x = 0; x < width; x++) {
      row.push(plainFloor);
    }
    grid.push(row);
  }

  return {
    id,
    name: id.replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase()),
    width,
    height,
    tileset: tilesetId,
    music: 'MUSIC_OVERWORLD',
    mapId: `MAP_${id.toUpperCase()}`,
    grid,
    terrainSource: 'grid',
    terrainBlocks: [],
    terrainDirty: false,
    defaultWalkable: `${tilesetId}.${plainFloor}`,
    spawn: { x: Math.floor(width / 2), y: Math.floor(height / 2), facing: 'DOWN' },
    exits: [],
    objects: [],
    regions: []
  };
}

export function levelDataToEditor(data: any): EditorLevel {
  if (data.logo && data.menu) {
    return titleScreenToEditor(data);
  }
  if ((data.enemies && data.hud_layout) || (data.enemy_positions && data.hud_layout)) {
    return battleScreenToEditor(data);
  }

  const w = data.map?.width || 20;
  const h = data.map?.height || 18;
  const tilesetId = data.map?.tileset || 'forest';
  const tileset = getTileset(tilesetId);
  // Gap fill follows the level's default_walkable (else first plain tile,
  // else first walkable): the same ground the ROM renders for unpainted
  // cells, so the canvas matches the game.
  const dwShort = typeof data.default_walkable === 'string' && data.default_walkable.includes('.')
    ? data.default_walkable.split('.')[1]
    : data.default_walkable;
  const defaultFloor = (dwShort && tileset.tiles.some(t => t.id === dwShort))
    ? dwShort
    : (tileset.tiles.find(t => t.id.includes('plain'))?.id
      || tileset.tiles.find(t => t.walkable)?.id || tileset.tiles[0]?.id || 'floor');

  const grid: string[][] = [];
  for (let y = 0; y < h; y++) {
    const row: string[] = [];
    for (let x = 0; x < w; x++) {
      row.push(defaultFloor);
    }
    grid.push(row);
  }

  const rawTerrain = data.layers?.terrain;
  let terrainSource: 'blocks' | 'grid' = 'grid';
  let terrainBlocks: TerrainBlock[] = [];
  if (Array.isArray(rawTerrain)) {
    if (rawTerrain.length === 0) {
      // Empty list is the blocks form with zero blocks (not an empty
      // grid): untouched saves write it back verbatim instead of
      // materializing a full default-floor grid.
      terrainSource = 'blocks';
      terrainBlocks = [];
    } else if (Array.isArray(rawTerrain[0])) {
      // 2D grid format
      const rawGrid = rawTerrain as string[][];
      for (let y = 0; y < Math.min(h, rawGrid.length); y++) {
        for (let x = 0; x < Math.min(w, rawGrid[y].length); x++) {
          const rawTile = rawGrid[y][x] || defaultFloor;
          const cleanTile = rawTile.includes('.') ? rawTile.split('.')[1] : rawTile;
          grid[y][x] = cleanTile;
        }
      }
    } else {
      // Block format: expand into the editing grid but keep the original
      // blocks (with comments) so an untouched save writes them back verbatim.
      terrainSource = 'blocks';
      const blocks = rawTerrain as TerrainBlock[];
      terrainBlocks = blocks.map((b) => ({ ...b }));
      for (const b of blocks) {
        const cleanTile = b.tile.includes('.') ? b.tile.split('.')[1] : b.tile;
        for (let cy = b.y; cy < Math.min(h, b.y + b.height); cy++) {
          for (let cx = b.x; cx < Math.min(w, b.x + b.width); cx++) {
            grid[cy][cx] = cleanTile;
          }
        }
      }
    }
  }

  return {
    id: data.id || 'scene',
    name: data.name || data.id || 'Scene',
    width: w,
    height: h,
    tileset: tilesetId,
    music: data.map.music || 'MUSIC_OVERWORLD',
    mapId: data.map.map_id || `MAP_${(data.id || 'scene').toUpperCase()}`,
    grid,
    terrainSource,
    terrainBlocks,
    terrainDirty: false,
    defaultWalkable: typeof data.default_walkable === 'string' ? data.default_walkable : undefined,
    spawn: data.player?.spawn || { x: Math.floor(w / 2), y: Math.floor(h / 2), facing: 'DOWN' },
    exits: data.exits || [],
    neighbors: normalizeNeighbors(data.neighbors),
    objects: data.objects || [],
    regions: data.regions || []
  };
}

export function editorToLevelData(lvl: EditorLevel): any {
  if (lvl.isScreen && lvl.originalScreenData) {
    const screenData = JSON.parse(JSON.stringify(lvl.originalScreenData));
    if (lvl.id === 'title') {
      const match = lvl.name.match(/^Title Screen \((.*)\)$/);
      if (match) screenData.title = match[1];
      if (lvl.titleLayout) {
        if (lvl.titleLayout.title) screenData.title = lvl.titleLayout.title;
        if (lvl.titleLayout.logo) screenData.logo = lvl.titleLayout.logo;
        if (lvl.titleLayout.logoImage) screenData.logo_image = lvl.titleLayout.logoImage;
        if (lvl.titleLayout.subtitle) screenData.subtitle = lvl.titleLayout.subtitle;
        if (lvl.titleLayout.graphic) screenData.graphic = lvl.titleLayout.graphic;
        if (lvl.titleLayout.prompt) screenData.prompt = lvl.titleLayout.prompt;
        if (lvl.titleLayout.credits) screenData.credits = lvl.titleLayout.credits;
        if (lvl.titleLayout.menu) screenData.menu = lvl.titleLayout.menu;
      }
    } else {
      if (lvl.battleHudLayout) {
        screenData.hud_layout = {
          ...(screenData.hud_layout || {}),
          ...lvl.battleHudLayout,
        };
      }
      const enemyObjs = (lvl.objects || []).filter((o) => o.type === 'enemy');
      if (enemyObjs.length > 0 && Array.isArray(screenData.enemy_positions)) {
        screenData.enemy_positions = enemyObjs.map((e) => ({ x: e.position.x, y: e.position.y }));
      }
    }
    return screenData;
  }

  return {
    id: lvl.id,
    name: lvl.name,
    map: {
      width: lvl.width,
      height: lvl.height,
      tileset: lvl.tileset,
      music: lvl.music,
      map_id: lvl.mapId
    },
    // Preserved verbatim: the single source of truth for unpainted cells.
    ...(lvl.defaultWalkable ? { default_walkable: lvl.defaultWalkable } : {}),
    layers: {
      // Write back the as-loaded form unless the terrain was painted, so
      // view-only saves never reformat the committed files (see
      // terrainSource/terrainDirty above).
      terrain: (!lvl.terrainDirty && lvl.terrainSource === 'blocks')
        ? lvl.terrainBlocks.map((b) => ({ ...b }))
        : lvl.grid.map(row => row.map(t => `${lvl.tileset}.${t}`))
    },
    player: {
      spawn: lvl.spawn
    },
    exits: lvl.exits,
    neighbors: {
      north: lvl.neighbors?.north || '',
      south: lvl.neighbors?.south || '',
      east: lvl.neighbors?.east || '',
      west: lvl.neighbors?.west || '',
    },
    objects: lvl.objects,
    regions: lvl.regions
  };
}
