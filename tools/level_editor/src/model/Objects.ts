export type ObjectType =
  | 'player_spawn'
  | 'npc'
  | 'enemy'
  | 'chest'
  | 'door'
  | 'warp'
  | 'trigger'
  | 'item'
  | 'signpost';

// Valid BattleId enum values (src/world/actor.h).  Keep in sync when a
// battle id is added on the ROM side.
export const BATTLE_IDS = [
  'BATTLE_NONE',
  'BATTLE_SLIME',
  'BATTLE_BAT',
  'BATTLE_SLIME_TRIO',
  'BATTLE_MIMIC',
  'BATTLE_KOBOLD',
  'BATTLE_SPIDER',
] as const;

// Valid AI kinds (src/world/actor.h AIKind).
export const AI_IDS = [
  'AI_NONE',
  'AI_PATROL_CROSS',
  'AI_PATROL_CIRCLE',
  'AI_CHASE',
  'AI_PATROL_VERT',
] as const;

export interface LevelObject {
  id: string;
  type: ObjectType;
  position: {
    x: number;
    y: number;
  };
  properties?: Record<string, any>;
  // Sprite/tile configuration for cross-context reuse (overworld + battle)
  overworld_sprite?: string;  // tileset.tile_name for overworld rendering
  animation_frames?: string[]; // Array of tileset.tile_name frames for animation
  animation_speed?: number;   // Animation ticks per frame (default 16 or 250ms)
  battle_sprite?: string;     // tileset.tile_name for battle rendering
  battle_name?: string;       // name shown in battle UI (overrides display_name)
  sprite_width?: number;      // Width in tiles (multi-tile preview objects, 1-4)
  sprite_height?: number;     // Height in tiles (multi-tile preview objects, 1-4)
}

export interface ObjectTemplate {
  type: ObjectType;
  label: string;
  defaultId: string;
  color: string;
  icon: string;
  defaultProps: Record<string, any>;
}

export const OBJECT_TEMPLATES: ObjectTemplate[] = [
  {
    type: 'npc',
    label: 'NPC / Villager',
    defaultId: 'npc_villager',
    color: '#3498db',
    icon: '👤',
    defaultProps: {
      display_name: 'VILLAGER',
      dialogue: 'DIALOGUE_ID_MAYOR_GREETING',
      overworld_sprite: 'interior.villager',
      battle_sprite: '',
      battle_name: 'VILLAGER'
    }
  },
  {
    type: 'enemy',
    label: 'Enemy',
    defaultId: 'enemy',
    color: '#e74c3c',
    icon: '👾',
    defaultProps: {
      display_name: 'ENEMY',
      battle: 'BATTLE_NONE',
      ai: 'AI_PATROL_CROSS'
    }
  },
  {
    type: 'item',
    label: 'Item Pickup',
    defaultId: 'item_pickup',
    color: '#f1c40f',
    icon: '✨',
    defaultProps: {
      display_name: 'AMULET',
      dialogue: 'DIALOGUE_ID_AMULET_NOTHING'
    }
  },
  {
    type: 'signpost',
    label: 'Signpost',
    defaultId: 'signpost_hint',
    color: '#e67e22',
    icon: '🪧',
    defaultProps: {
      display_name: 'SIGNPOST',
      dialogue: 'DIALOGUE_ID_SIGNPOST'
    }
  },
  {
    type: 'chest',
    label: 'Treasure Chest',
    defaultId: 'chest_gold',
    color: '#9b59b6',
    icon: '📦',
    defaultProps: {
      item_id: 'ITEM_HERB',
      gold: 10
    }
  },
  {
    type: 'trigger',
    label: 'Event Trigger',
    defaultId: 'trigger_event',
    color: '#1abc9c',
    icon: '⚡',
    defaultProps: {
      event_id: 'EVENT_TOWN_ARRIVAL'
    }
  }
];
