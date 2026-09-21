import React, { useState, useEffect, useRef } from 'react';
import { EditorLevel, LevelExit, LevelNeighbors, LevelRegion, NEIGHBOR_DIRS, PlayerSpawn, normalizeNeighbors } from './model/Level';
import { LevelObject, OBJECT_TEMPLATES } from './model/Objects';
import { EditLayer, LayerPanel } from './LayerPanel';
import { BUILTIN_TILESETS, TileDefinition } from './model/Tileset';
import { BATTLE_IDS } from './model/Objects';
import { fetchEnemyTypeList, fetchEnemyType } from './io/combatArt';
import { fetchDialogueList } from './io/dialogue';
import { fetchShopList } from './io/shops';
import { fetchEntityTypeList, saveEntityType } from './io/entityTypes';
import { ExitConnector } from './ExitConnector';
import { EdgeConnector } from './EdgeConnector';
import { TutorialEditor } from './TutorialEditor';
import { fetchUsedActorIds } from './io/saveLevel';
import { FilterCombo } from './FilterCombo';


interface InspectorProps {
  level: EditorLevel;
  activeLayer: EditLayer;
  onSelectLayer: (layer: EditLayer) => void;
  showTerrain: boolean;
  onToggleShowTerrain: () => void;
  showExits: boolean;
  onToggleShowExits: () => void;
  showObjects: boolean;
  onToggleShowObjects: () => void;
  showRegions: boolean;
  onToggleShowRegions: () => void;
  selectedEntityIndex: number | null;
  onSelectEntityIndex: (index: number | null) => void;
  onUpdateLevelMeta: (updates: Partial<EditorLevel>) => void;
  onUpdateSpawn: (spawn: PlayerSpawn) => void;
  onAddExit: (exit: LevelExit) => void;
  onUpdateExit: (index: number, exit: LevelExit) => void;
  onDeleteExit: (index: number) => void;
  onUpdateNeighbors: (neighbors: LevelNeighbors) => void;
  onAddObject: (obj: LevelObject) => void;
  onUpdateObject: (index: number, obj: LevelObject) => void;
  onDeleteObject: (index: number) => void;
  onDuplicateObject?: (index: number) => void;
  onAddRegion: (region: LevelRegion) => void;
  onUpdateRegion: (index: number, region: LevelRegion) => void;
  onDeleteRegion: (index: number) => void;
  // All available level scenes, fed from App's catalogue (refreshed
  // from disk on boot and after every save). Required: the exit target
  // is always picked from real data, never typed blind. scene_id null
  // means unregistered (save the level to assign one).
  sceneOptions: Array<{ id: string; name: string; scene_id: number | null }>;
  /** Numeric scene id assigned to the open level (null = unregistered). */
  sceneId?: number | null;
  /** Delete the open level (retires its id, clears referring exits). */
  onDeleteLevel?: () => void;
  /** Remove every level file whose scene id is retired (tombstoned). */
  onCleanRetiredOrphans?: () => void;
}

// BGM preview files rendered by tools/render_music_preview.py
// (make music-preview) into tools/level_editor/public/audio/.
// Chiptune-only tracks (OVERWORLD/TITLE/VICTORY) have no preview.
const MUSIC_PREVIEW_FILES: Record<string, string> = {
  MUSIC_BATTLE: '/audio/battle.wav',
  MUSIC_DESOLATE: '/audio/desolate_landscape.wav',
  MUSIC_FOREST: '/audio/forest.wav',
  MUSIC_BOSS: '/audio/boss_fight.wav',
  MUSIC_TOWN: '/audio/village.wav',
  MUSIC_DUNGEON: '/audio/castle.wav',
};

const MusicPreviewToggle: React.FC<{ music: string; levelId: string }> = ({
  music,
  levelId,
}) => {
  const audioRef = useRef<HTMLAudioElement | null>(null);
  const [playing, setPlaying] = useState(false);
  const file = MUSIC_PREVIEW_FILES[music] || null;

  // Stop when switching levels or unmounting.
  useEffect(() => {
    setPlaying(false);
    if (audioRef.current) audioRef.current.pause();
  }, [levelId]);
  useEffect(() => {
    const audio = audioRef.current;
    return () => {
      if (audio) audio.pause();
    };
  }, []);

  // Follow BGM-track changes while playing.
  useEffect(() => {
    const audio = audioRef.current;
    if (!audio || !playing) return;
    if (file) {
      audio.src = file;
      audio.play().catch(() => setPlaying(false));
    } else {
      audio.pause();
      setPlaying(false);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [music]);

  const toggle = () => {
    const audio = audioRef.current;
    if (!audio || !file) return;
    if (playing) {
      audio.pause();
      setPlaying(false);
    } else {
      audio.src = file;
      audio.loop = true;
      audio.play().then(() => setPlaying(true)).catch(() => setPlaying(false));
    }
  };

  return (
    <div className="music-preview-row" style={{ marginTop: '6px', display: 'flex', gap: '8px', alignItems: 'center' }}>
      <button
        className="btn btn-sm"
        onClick={toggle}
        disabled={!file}
        title={file ? (playing ? 'Stop BGM preview' : 'Preview this level\'s BGM track') : 'No preview for chiptune tracks'}
      >
        {playing ? '⏹ Stop' : '▶ Preview'}
      </button>
      {!file && <span className="hint-text">no preview (chiptune track)</span>}
      <audio ref={audioRef} loop preload="none" />
    </div>
  );
};

export const Inspector: React.FC<InspectorProps> = ({
  level,
  activeLayer,
  onSelectLayer,
  showTerrain,
  onToggleShowTerrain,
  showExits,
  onToggleShowExits,
  showObjects,
  onToggleShowObjects,
  showRegions,
  onToggleShowRegions,
  selectedEntityIndex,
  onSelectEntityIndex,
  onUpdateLevelMeta,
  onUpdateSpawn,
  onAddExit,
  onUpdateExit,
  onDeleteExit,
  onUpdateNeighbors,
  onAddObject,
  onUpdateObject,
  onDeleteObject,
  onDuplicateObject,
  onAddRegion,
  onUpdateRegion,
  onDeleteRegion,
  sceneOptions,
  sceneId,
  onDeleteLevel,
  onCleanRetiredOrphans,
}) => {
  const isTitleScreen = !!(level.isScreen && (level.mapId === 'SCREEN_TITLE' || level.id === 'title'));
  const [tab, setTab] = useState<'context' | 'layers' | 'map' | 'title'>('context');

  useEffect(() => {
    if (isTitleScreen) {
      setTab('title');
    }
  }, [level.id, isTitleScreen]);
  const [previewTick, setPreviewTick] = useState<number>(0);
  useEffect(() => {
    const timer = setInterval(() => {
      setPreviewTick((t) => (t + 1) % 60);
    }, 250);
    return () => clearInterval(timer);
  }, []);

  // Collect all tiles from all tilesets with scoped IDs
  const allTilesWithScope = Object.values(BUILTIN_TILESETS).flatMap((ts) =>
    ts.tiles.map((t) => ({
      ...t,
      tilesetId: ts.id,
      tilesetLabel: ts.label,
      scopedId: `${ts.id}.${t.id}`,
    }))
  );

  const selectedExit = activeLayer === 'exits' && selectedEntityIndex !== null ? level.exits[selectedEntityIndex] : null;
  const selectedObject = activeLayer === 'objects' && selectedEntityIndex !== null ? level.objects[selectedEntityIndex] : null;
  const selectedRegion = activeLayer === 'regions' && selectedEntityIndex !== null ? level.regions[selectedEntityIndex] : null;

  // Enemy types with shared overworld art (id -> id): placements of these
  // types ignore per-instance sprite names (type-owned art wins in ROM).
  const [owEnemyIds, setOwEnemyIds] = useState<Set<string>>(new Set());
  const [enemyTypeList, setEnemyTypeList] = useState<Array<{ id: string; label: string }>>([]);
  const [dialogueList, setDialogueList] = useState<Array<{ id: string; label: string }>>([]);
  const [shopList, setShopList] = useState<Array<{ id: number; label: string; owns: number }>>([]);
  const [entityTypeList, setEntityTypeList] = useState<Array<{ id: string; label: string; entity_id: string }>>([]);
  const refreshEntityTypes = () => {
    fetchEntityTypeList().then((items) => {
      setEntityTypeList(items.map((e) => ({ id: e.id, label: e.label || e.id, entity_id: e.entity_id })));
    }).catch(() => undefined);
  };
  useEffect(() => {
    fetchEnemyTypeList().then((items) => {
      setOwEnemyIds(new Set(items.filter((e) => e.ow).map((e) => e.id)));
      setEnemyTypeList(items.map((e) => ({ id: e.id, label: e.label || e.id })));
    }).catch(() => undefined);
    fetchDialogueList().then((items) => {
      setDialogueList(items.map((d) => ({ id: 'DIALOGUE_ID_' + d.id.toUpperCase(), label: d.label || d.id })));
    }).catch(() => undefined);
    fetchShopList().then((items) => {
      setShopList(items.map((s) => ({ id: s.id, label: s.label || `Shop ${s.id}`, owns: s.buys })));
    }).catch(() => undefined);
    refreshEntityTypes();
  }, []);
  const selectedEnemyType = (() => {
    if (!selectedObject || (selectedObject.type !== 'enemy' && selectedObject.type !== 'npc')) return null;
    const props = selectedObject.properties || {};
    const explicit = props.enemy_type;
    if (explicit && owEnemyIds.has(explicit)) return explicit;
    const ent = props.entity_id || '';
    if (ent.startsWith('ENTITY_ID_')) {
      const conv = ent.slice('ENTITY_ID_'.length).toLowerCase();
      if (owEnemyIds.has(conv)) return conv;
    }
    return null;
  })();

  return (
    <div className="panel inspector-panel">
      <div className="inspector-tabs">
        <button
          className={`tab-btn ${tab === 'context' ? 'active' : ''}`}
          onClick={() => setTab('context')}
          title="Current layer properties and entity details"
        >
          {activeLayer === 'terrain'
            ? '🎨 Terrain'
            : activeLayer === 'spawn'
            ? '🚩 Spawn'
            : activeLayer === 'exits'
            ? '🚪 Exits'
            : activeLayer === 'objects'
            ? '👾 Objects'
            : '🏷️ Regions'}
        </button>
        <button
          className={`tab-btn ${tab === 'layers' ? 'active' : ''}`}
          onClick={() => setTab('layers')}
          title="Layers and edit modes"
        >
          📑 Layers
        </button>
        <button
          className={`tab-btn ${tab === 'map' ? 'active' : ''}`}
          onClick={() => setTab('map')}
          title="Map metadata and settings"
        >
          ⚙️ Map Info
        </button>
        {isTitleScreen && (
          <button
            className={`tab-btn ${tab === 'title' ? 'active' : ''}`}
            onClick={() => setTab('title')}
            title="Title Screen Layout, Big Image, Prompt & Credits"
          >
            👑 Title Studio
          </button>
        )}
      </div>

      <div className="panel-body inspector-content">
        {tab === 'layers' && (
          <div className="inspector-section">
            <LayerPanel
              activeLayer={activeLayer}
              onSelectLayer={onSelectLayer}
              showTerrain={showTerrain}
              onToggleShowTerrain={onToggleShowTerrain}
              showExits={showExits}
              onToggleShowExits={onToggleShowExits}
              showObjects={showObjects}
              onToggleShowObjects={onToggleShowObjects}
              showRegions={showRegions}
              onToggleShowRegions={onToggleShowRegions}
              exitCount={level.exits.length}
              objectCount={level.objects.length}
              regionCount={level.regions.length}
              embedded={true}
            />
          </div>
        )}

        {tab === 'map' && (
          <div className="inspector-section">
            <h4>Map Configuration</h4>
            <div className="form-group">
              <label>Scene ID</label>
              <input
                type="text"
                value={level.id}
                disabled={level.isScreen}
                onChange={(e) => onUpdateLevelMeta({ id: e.target.value.toLowerCase().replace(/[^a-z0-9_]/g, '') })}
              />
              {!level.isScreen && (
                <span style={{ fontSize: 11, color: '#888' }}>
                  {sceneId !== null && sceneId !== undefined
                    ? `scene id ${sceneId} — renaming on save keeps the id and rewires exits`
                    : 'not registered yet — saving assigns the next scene id'}
                </span>
              )}
            </div>
            <div className="form-group">
              <label>Scene Name</label>
              <input
                type="text"
                value={level.name}
                onChange={(e) => onUpdateLevelMeta({ name: e.target.value })}
              />
            </div>
            <div className="form-row">
              <div className="form-group">
                <label>Width (cols)</label>
                <input
                  type="number"
                  min="4"
                  max="40"
                  value={level.width}
                  onChange={(e) => onUpdateLevelMeta({ width: parseInt(e.target.value) || 20 })}
                />
              </div>
              <div className="form-group">
                <label>Height (rows)</label>
                <input
                  type="number"
                  min="4"
                  max="24"
                  value={level.height}
                  onChange={(e) => onUpdateLevelMeta({ height: parseInt(e.target.value) || 18 })}
                />
              </div>
            </div>
            <div className="form-group">
              <label>BGM Track</label>
              <select
                value={level.music}
                onChange={(e) => onUpdateLevelMeta({ music: e.target.value })}
              >
                <option value="MUSIC_OVERWORLD">MUSIC_OVERWORLD</option>
                <option value="MUSIC_TITLE">MUSIC_TITLE</option>
                <option value="MUSIC_TOWN">MUSIC_TOWN (Village.uge)</option>
                <option value="MUSIC_DUNGEON">MUSIC_DUNGEON (castle.uge)</option>
                <option value="MUSIC_BATTLE">MUSIC_BATTLE</option>
                <option value="MUSIC_DESOLATE">MUSIC_DESOLATE (desolate_landscape.uge)</option>
                <option value="MUSIC_FOREST">MUSIC_FOREST (Forest.uge)</option>
                <option value="MUSIC_BOSS">MUSIC_BOSS (Boss fight.uge)</option>
                </select>
                <MusicPreviewToggle music={level.music} levelId={level.id} />
              </div>
            <div className="form-group">
              <label>Engine Map ID</label>
              <input
                type="text"
                value={level.mapId}
                onChange={(e) => onUpdateLevelMeta({ mapId: e.target.value })}
              />
            </div>
            {!level.isScreen && sceneId !== null && sceneId !== undefined && onDeleteLevel && (
              <div className="form-group">
                <label style={{ color: '#a66' }}>Danger Zone</label>
                <button
                  className="btn btn-sm"
                  style={{ background: '#7a2020', color: '#fff' }}
                  onClick={onDeleteLevel}
                  title="Retires the scene id (never reused) and clears exits that target it"
                >
                  🗑 Delete Level
                </button>
              </div>
            )}
            {!level.isScreen && onCleanRetiredOrphans && (
              <div className="form-group">
                <button
                  className="btn btn-sm"
                  onClick={onCleanRetiredOrphans}
                  title="Delete every levels/<id>.json whose scene id is retired — a leftover file blocks Compile ROM and keeps its actor ids reserved"
                >
                  🧹 Clean retired orphans
                </button>
              </div>
            )}
          </div>
        )}


        {tab === 'title' && (
          <div className="inspector-section">
            <h4>👑 Title Screen Studio</h4>
            <p className="hint-text">
              Completely data-driven Title Screen: customize the Big Title Graphic, Game Title, Centered &ldquo;PRESS START&rdquo;, and Bottom-Row Credits.
            </p>

            <TutorialEditor />

            {/* Game Title & Subtitle */}
            <div className="form-group">
              <label style={{ fontWeight: 600 }}>🏷️ Game Title & Subtitle Lines</label>
              <input
                type="text"
                placeholder="Game Title"
                value={level.titleLayout?.title ?? 'Kaartenheld'}
                onChange={(e) =>
                  onUpdateLevelMeta({
                    titleLayout: {
                      ...(level.titleLayout || {}),
                      title: e.target.value,
                    },
                  })
                }
              />
              <label style={{ fontSize: 11, marginTop: 4 }}>Banner & Subtitle Lines (Row 1-5)</label>
              <textarea
                rows={5}
                style={{ fontFamily: 'monospace', fontSize: 11, width: '100%' }}
                value={(level.titleLayout?.logo?.lines ?? []).join('\n')}
                onChange={(e) =>
                  onUpdateLevelMeta({
                    titleLayout: {
                      ...(level.titleLayout || {}),
                      logo: {
                        ...(level.titleLayout?.logo || { x: 0, y: 1 }),
                        lines: e.target.value.split('\n'),
                      },
                    },
                  })
                }
              />
            </div>

            {/* Title Logo Bitmap (assets/title-red.png via make gfx) */}
            <div className="form-group" style={{ background: 'rgba(30, 41, 59, 0.4)', padding: 8, borderRadius: 4 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <label style={{ fontWeight: 600 }}>🏆 Title Logo Bitmap</label>
                <label style={{ display: 'flex', alignItems: 'center', gap: 4, fontSize: 12 }}>
                  <input
                    type="checkbox"
                    checked={level.titleLayout?.logoImage?.enabled ?? true}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          logoImage: {
                            ...(level.titleLayout?.logoImage || { x: 2, y: 1, width: 16, height: 3, palette: 1, url: '/tiles/title/logo.png' }),
                            enabled: e.target.checked,
                          },
                        },
                      })
                    }
                  />
                  Enabled
                </label>
              </div>
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: 6, margin: '6px 0' }}>
                <div>
                  <label style={{ fontSize: 11 }}>Col X</label>
                  <input
                    type="number"
                    min={0}
                    max={19}
                    value={level.titleLayout?.logoImage?.x ?? 2}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          logoImage: {
                            ...(level.titleLayout?.logoImage || { enabled: true, y: 1, width: 16, height: 3, palette: 1, url: '/tiles/title/logo.png' }),
                            x: parseInt(e.target.value) || 0,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Row Y</label>
                  <input
                    type="number"
                    min={0}
                    max={17}
                    value={level.titleLayout?.logoImage?.y ?? 1}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          logoImage: {
                            ...(level.titleLayout?.logoImage || { enabled: true, x: 2, width: 16, height: 3, palette: 1, url: '/tiles/title/logo.png' }),
                            y: parseInt(e.target.value) || 0,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Width</label>
                  <input
                    type="number"
                    min={1}
                    max={20}
                    value={level.titleLayout?.logoImage?.width ?? 16}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          logoImage: {
                            ...(level.titleLayout?.logoImage || { enabled: true, x: 2, y: 1, height: 3, palette: 1, url: '/tiles/title/logo.png' }),
                            width: parseInt(e.target.value) || 16,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Height</label>
                  <input
                    type="number"
                    min={1}
                    max={18}
                    value={level.titleLayout?.logoImage?.height ?? 3}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          logoImage: {
                            ...(level.titleLayout?.logoImage || { enabled: true, x: 2, y: 1, width: 16, palette: 1, url: '/tiles/title/logo.png' }),
                            height: parseInt(e.target.value) || 3,
                          },
                        },
                      })
                    }
                  />
                </div>
              </div>
              <div style={{ fontSize: 10, color: '#94a3b8' }}>
                Tiles from <code>assets/title-red.png</code> via <code>make gfx</code>
                (editor copies <code>public/tiles/title/logo.png</code>).
              </div>
            </div>

            {/* Subtitle (drawn under the logo bitmap) */}
            <div className="form-group">
              <label style={{ fontWeight: 600 }}>📝 Subtitle</label>
              <input
                type="text"
                placeholder="BATTLE DEMO"
                value={level.titleLayout?.subtitle?.text ?? 'BATTLE DEMO'}
                onChange={(e) =>
                  onUpdateLevelMeta({
                    titleLayout: {
                      ...(level.titleLayout || {}),
                      subtitle: {
                        ...(level.titleLayout?.subtitle || { y: 5, align: 'center' }),
                        text: e.target.value,
                      },
                    },
                  })
                }
              />
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gap: 6, marginTop: 4 }}>
                <div>
                  <label style={{ fontSize: 11 }}>Row Y</label>
                  <input
                    type="number"
                    min={0}
                    max={17}
                    value={level.titleLayout?.subtitle?.y ?? 5}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          subtitle: {
                            ...(level.titleLayout?.subtitle || { text: 'BATTLE DEMO', align: 'center' }),
                            y: parseInt(e.target.value) || 0,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Align</label>
                  <select
                    value={level.titleLayout?.subtitle?.align ?? 'center'}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          subtitle: {
                            ...(level.titleLayout?.subtitle || { text: 'BATTLE DEMO', y: 5 }),
                            align: e.target.value as 'left' | 'center' | 'right',
                          },
                        },
                      })
                    }
                  >
                    <option value="center">center</option>
                    <option value="left">left</option>
                    <option value="right">right</option>
                  </select>
                </div>
              </div>
            </div>

            {/* Big Title Graphic / Tile Image */}
            <div className="form-group" style={{ background: 'rgba(30, 41, 59, 0.4)', padding: 8, borderRadius: 4 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <label style={{ fontWeight: 600 }}>🖼️ Big Title Graphic / Multi-Tile Artwork</label>
                <label style={{ display: 'flex', alignItems: 'center', gap: 4, fontSize: 12 }}>
                  <input
                    type="checkbox"
                    checked={level.titleLayout?.graphic?.enabled ?? true}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { x: 2, y: 7, width: 16, height: 5, lines: [] }),
                            enabled: e.target.checked,
                          },
                        },
                      })
                    }
                  />
                  Enabled
                </label>
              </div>

              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: 6, margin: '6px 0' }}>
                <div>
                  <label style={{ fontSize: 11 }}>Col X</label>
                  <input
                    type="number"
                    min={0}
                    max={19}
                    value={level.titleLayout?.graphic?.x ?? 2}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { enabled: true, y: 7, width: 16, height: 5, lines: [] }),
                            x: parseInt(e.target.value) || 0,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Row Y</label>
                  <input
                    type="number"
                    min={0}
                    max={17}
                    value={level.titleLayout?.graphic?.y ?? 7}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { enabled: true, x: 2, width: 16, height: 5, lines: [] }),
                            y: parseInt(e.target.value) || 0,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Width</label>
                  <input
                    type="number"
                    min={1}
                    max={20}
                    value={level.titleLayout?.graphic?.width ?? 16}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { enabled: true, x: 2, y: 7, height: 5, lines: [] }),
                            width: parseInt(e.target.value) || 16,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Height</label>
                  <input
                    type="number"
                    min={1}
                    max={18}
                    value={level.titleLayout?.graphic?.height ?? 5}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { enabled: true, x: 2, y: 7, width: 16, lines: [] }),
                            height: parseInt(e.target.value) || 5,
                          },
                        },
                      })
                    }
                  />
                </div>
              </div>

              {/* Quick Graphic Templates */}
              <div style={{ margin: '4px 0 6px' }}>
                <label style={{ fontSize: 10, color: '#94a3b8' }}>Quick Templates:</label>
                <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap', marginTop: 2 }}>
                  <button
                    type="button"
                    className="btn btn-sm"
                    style={{ fontSize: 10, padding: '2px 6px' }}
                    onClick={() =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { enabled: true, x: 2, y: 7, width: 16, height: 5 }),
                            lines: [
                              '  /\\____/\\    ',
                              ' (  o  o  )   ',
                              ' (  ==0== )   ',
                              '  )      (    ',
                              ' (________)   ',
                            ],
                          },
                        },
                      })
                    }
                  >
                    🐉 Dragon / Beast
                  </button>
                  <button
                    type="button"
                    className="btn btn-sm"
                    style={{ fontSize: 10, padding: '2px 6px' }}
                    onClick={() =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { enabled: true, x: 2, y: 7, width: 16, height: 5 }),
                            lines: [
                              '    /\\    /\\    ',
                              '   /  \\  /  \\   ',
                              '  <====><====>  ',
                              '   \\  /  \\  /   ',
                              '    \\/    \\/    ',
                            ],
                          },
                        },
                      })
                    }
                  >
                    ⚔️ Cross Blades
                  </button>
                  <button
                    type="button"
                    className="btn btn-sm"
                    style={{ fontSize: 10, padding: '2px 6px' }}
                    onClick={() =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { enabled: true, x: 2, y: 7, width: 16, height: 5 }),
                            lines: [
                              '   |#|  |#|   ',
                              '  _|_|__|_|_  ',
                              ' |  _    _  | ',
                              ' | | |  | | | ',
                              ' |___|__|___| ',
                            ],
                          },
                        },
                      })
                    }
                  >
                    🏰 Citadel
                  </button>
                  <button
                    type="button"
                    className="btn btn-sm"
                    style={{ fontSize: 10, padding: '2px 6px' }}
                    onClick={() =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          graphic: {
                            ...(level.titleLayout?.graphic || { enabled: true, x: 2, y: 7, width: 16, height: 5 }),
                            lines: [
                              '      .---.     ',
                              '     /   . \\    ',
                              '    |  (o)  |___',
                              '  ~/         /  ',
                              '   \\________/   ',
                            ],
                          },
                        },
                      })
                    }
                  >
                    🐋 Whale Sigil
                  </button>
                </div>
              </div>

              <label style={{ fontSize: 11 }}>Tile / ASCII Artwork Lines</label>
              <textarea
                rows={5}
                style={{ fontFamily: 'monospace', fontSize: 11, width: '100%' }}
                value={(level.titleLayout?.graphic?.lines ?? []).join('\n')}
                onChange={(e) =>
                  onUpdateLevelMeta({
                    titleLayout: {
                      ...(level.titleLayout || {}),
                      graphic: {
                        ...(level.titleLayout?.graphic || { enabled: true, x: 2, y: 7, width: 16, height: 5 }),
                        lines: e.target.value.split('\n'),
                      },
                    },
                  })
                }
              />
            </div>

            {/* PRESS START Prompt */}
            <div className="form-group">
              <label style={{ fontWeight: 600 }}>🕹️ &ldquo;PRESS START&rdquo; Prompt</label>
              <div style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 2fr) minmax(0, 1fr) minmax(0, 1fr)', gap: 6 }}>
                <div>
                  <label style={{ fontSize: 11 }}>Text</label>
                  <input
                    type="text"
                    value={level.titleLayout?.prompt?.text ?? 'PRESS START'}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          prompt: {
                            ...(level.titleLayout?.prompt || { x: 4, y: 14, align: 'center' }),
                            text: e.target.value,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Row (Y)</label>
                  <input
                    type="number"
                    min={0}
                    max={17}
                    value={level.titleLayout?.prompt?.y ?? 14}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          prompt: {
                            ...(level.titleLayout?.prompt || { text: 'PRESS START', x: 4, align: 'center' }),
                            y: parseInt(e.target.value) || 0,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Align</label>
                  <select
                    value={level.titleLayout?.prompt?.align ?? 'center'}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          prompt: {
                            ...(level.titleLayout?.prompt || { text: 'PRESS START', x: 4, y: 14 }),
                            align: e.target.value as any,
                          },
                        },
                      })
                    }
                  >
                    <option value="center">Center</option>
                    <option value="left">Left</option>
                    <option value="right">Right</option>
                  </select>
                </div>
              </div>
            </div>

            {/* Bottom Row Credits / Author Info */}
            <div className="form-group" style={{ background: 'rgba(30, 41, 59, 0.4)', padding: 8, borderRadius: 4 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <label style={{ fontWeight: 600 }}>✍️ Bottom Row Credits / Author Attribution</label>
                <label style={{ display: 'flex', alignItems: 'center', gap: 4, fontSize: 12 }}>
                  <input
                    type="checkbox"
                    checked={level.titleLayout?.credits?.enabled ?? true}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          credits: {
                            ...(level.titleLayout?.credits || { text: 'GALLIA BELGICA', x: 2, y: 17, align: 'right' }),
                            enabled: e.target.checked,
                          },
                        },
                      })
                    }
                  />
                  Enabled
                </label>
              </div>

              <div style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 2fr) minmax(0, 1fr) minmax(0, 1fr)', gap: 6, marginTop: 4 }}>
                <div>
                  <label style={{ fontSize: 11 }}>Credits Text</label>
                  <input
                    type="text"
                    value={level.titleLayout?.credits?.text ?? 'GALLIA BELGICA'}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          credits: {
                            ...(level.titleLayout?.credits || { enabled: true, x: 2, y: 17, align: 'right' }),
                            text: e.target.value,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Row (Y)</label>
                  <input
                    type="number"
                    min={0}
                    max={17}
                    value={level.titleLayout?.credits?.y ?? 17}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          credits: {
                            ...(level.titleLayout?.credits || { enabled: true, text: 'GALLIA BELGICA', x: 2, align: 'right' }),
                            y: parseInt(e.target.value) || 17,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Align</label>
                  <select
                    value={level.titleLayout?.credits?.align ?? 'right'}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          credits: {
                            ...(level.titleLayout?.credits || { enabled: true, text: 'GALLIA BELGICA', x: 2, y: 17 }),
                            align: e.target.value as any,
                          },
                        },
                      })
                    }
                  >
                    <option value="right">Right</option>
                    <option value="center">Center</option>
                    <option value="left">Left</option>
                  </select>
                </div>
              </div>
            </div>

            {/* Menu Options */}
            <div className="form-group">
              <label style={{ fontWeight: 600 }}>📋 Menu Options (When Menu Opens)</label>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 6, marginBottom: 6 }}>
                <div>
                  <label style={{ fontSize: 11 }}>Menu Col (X)</label>
                  <input
                    type="number"
                    min={0}
                    max={19}
                    value={level.titleLayout?.menu?.x ?? 3}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          menu: {
                            ...(level.titleLayout?.menu || { caret_x: 3, first_row: 10, row_step: 2, options: [] }),
                            x: parseInt(e.target.value) || 0,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>First Row (Y)</label>
                  <input
                    type="number"
                    min={0}
                    max={17}
                    value={level.titleLayout?.menu?.first_row ?? 10}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          menu: {
                            ...(level.titleLayout?.menu || { x: 3, caret_x: 3, row_step: 2, options: [] }),
                            first_row: parseInt(e.target.value) || 0,
                          },
                        },
                      })
                    }
                  />
                </div>
                <div>
                  <label style={{ fontSize: 11 }}>Row Step</label>
                  <input
                    type="number"
                    min={1}
                    max={5}
                    value={level.titleLayout?.menu?.row_step ?? 2}
                    onChange={(e) =>
                      onUpdateLevelMeta({
                        titleLayout: {
                          ...(level.titleLayout || {}),
                          menu: {
                            ...(level.titleLayout?.menu || { x: 3, caret_x: 3, first_row: 10, options: [] }),
                            row_step: parseInt(e.target.value) || 1,
                          },
                        },
                      })
                    }
                  />
                </div>
              </div>

              <label style={{ fontSize: 11 }}>Menu Option Labels</label>
              <textarea
                rows={4}
                style={{ fontFamily: 'monospace', fontSize: 11, width: '100%' }}
                value={(level.titleLayout?.menu?.options ?? []).join('\n')}
                onChange={(e) =>
                  onUpdateLevelMeta({
                    titleLayout: {
                      ...(level.titleLayout || {}),
                      menu: {
                        ...(level.titleLayout?.menu || { x: 3, caret_x: 3, first_row: 10, row_step: 2 }),
                        options: e.target.value.split('\n'),
                      },
                    },
                  })
                }
              />
            </div>
          </div>
        )}

        {tab === 'context' && activeLayer === 'spawn' && (
          <div className="inspector-section">
            <h4>Player Spawn Position</h4>
            <p className="hint-text">Click anywhere on the map in Spawn mode to move player spawn.</p>
            <div className="form-row">
              <div className="form-group">
                <label>Spawn X</label>
                <input
                  type="number"
                  min="0"
                  max={level.width - 1}
                  value={level.spawn.x}
                  onChange={(e) =>
                    onUpdateSpawn({ ...level.spawn, x: parseInt(e.target.value) || 0, facing: level.spawn.facing || 'DOWN' })
                  }
                />
              </div>
              <div className="form-group">
                <label>Spawn Y</label>
                <input
                  type="number"
                  min="0"
                  max={level.height - 1}
                  value={level.spawn.y}
                  onChange={(e) =>
                    onUpdateSpawn({ ...level.spawn, y: parseInt(e.target.value) || 0, facing: level.spawn.facing || 'DOWN' })
                  }
                />
              </div>
            </div>
            <div className="form-group">
              <label>Initial Facing</label>
              <select
                value={level.spawn.facing || 'DOWN'}
                onChange={(e) => onUpdateSpawn({ ...level.spawn, facing: e.target.value })}
              >
                <option value="UP">UP / North</option>
                <option value="DOWN">DOWN / South</option>
                <option value="LEFT">LEFT / West</option>
                <option value="RIGHT">RIGHT / East</option>
              </select>
            </div>

            {/* Hero Animation Frames */}
            <div className="form-group animation-frames-group" style={{ marginTop: 12 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                <label style={{ margin: 0, fontWeight: 600 }}>🎞️ Hero Animation Frames</label>
                <button
                  type="button"
                  className="btn-tiny"
                  title="Quick-set Hero Frames 1 & 2"
                  onClick={() => {
                    onUpdateSpawn({
                      ...level.spawn,
                      animation_frames: [
                        'actors.actors_hero_frame_1',
                        'actors.actors_hero_frame_2',
                      ],
                    });
                  }}
                >
                  🧙 Hero (2 frames)
                </button>
              </div>

              {level.spawn.animation_frames && level.spawn.animation_frames.length > 0 ? (
                <>
                  {/* Live Preview */}
                  <div className="anim-preview-box">
                    {(() => {
                      const curKey = level.spawn.animation_frames[previewTick % level.spawn.animation_frames.length];
                      const t = allTilesWithScope.find((tile) => tile.scopedId === curKey || tile.id === curKey);
                      return (
                        <>
                          {t?.image_url ? (
                            <img src={t.image_url} alt="" style={{ width: 28, height: 28, imageRendering: 'pixelated' }} />
                          ) : (
                            <span style={{ fontSize: 22 }}>🧙</span>
                          )}
                          <span style={{ fontSize: 12, color: '#e6edf3' }}>
                            Playing frame {(previewTick % level.spawn.animation_frames.length) + 1} of {level.spawn.animation_frames.length} ({t?.label || curKey})
                          </span>
                        </>
                      );
                    })()}
                  </div>

                  <div className="frames-list" style={{ display: 'flex', flexDirection: 'column', gap: 4, marginBottom: 8 }}>
                    {level.spawn.animation_frames.map((frameId, fIdx) => {
                      const t = allTilesWithScope.find((tile) => tile.scopedId === frameId || tile.id === frameId);
                      return (
                        <div
                          key={fIdx}
                          className="frame-row"
                          style={{
                            display: 'flex',
                            alignItems: 'center',
                            gap: 8,
                            background: '#1c2430',
                            padding: '4px 8px',
                            borderRadius: 4,
                            border: '1px solid #2d3848',
                          }}
                        >
                          <span style={{ fontSize: 11, color: '#a0aec0', width: 45 }}>#{fIdx + 1}</span>
                          {t?.image_url && (
                            <img src={t.image_url} alt="" style={{ width: 20, height: 20, imageRendering: 'pixelated' }} />
                          )}
                          <span style={{ flex: 1, fontSize: 12, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                            {t?.label || frameId}
                          </span>
                          {fIdx > 0 && (
                            <button
                              type="button"
                              className="btn-icon-tiny"
                              title="Move Up"
                              onClick={() => {
                                const frames = [...level.spawn.animation_frames!];
                                const tmp = frames[fIdx - 1];
                                frames[fIdx - 1] = frames[fIdx];
                                frames[fIdx] = tmp;
                                onUpdateSpawn({ ...level.spawn, animation_frames: frames });
                              }}
                            >
                              ⬆️
                            </button>
                          )}
                          {level.spawn.animation_frames && fIdx < level.spawn.animation_frames.length - 1 && (
                            <button
                              type="button"
                              className="btn-icon-tiny"
                              title="Move Down"
                              onClick={() => {
                                const frames = [...level.spawn.animation_frames!];
                                const tmp = frames[fIdx + 1];
                                frames[fIdx + 1] = frames[fIdx];
                                frames[fIdx] = tmp;
                                onUpdateSpawn({ ...level.spawn, animation_frames: frames });
                              }}
                            >
                              ⬇️
                            </button>
                          )}
                          <button
                            type="button"
                            className="btn-icon-tiny"
                            title="Remove Frame"
                            onClick={() => {
                              const frames = level.spawn.animation_frames!.filter((_, i) => i !== fIdx);
                              onUpdateSpawn({
                                ...level.spawn,
                                animation_frames: frames.length > 0 ? frames : undefined,
                              });
                            }}
                          >
                            ❌
                          </button>
                        </div>
                      );
                    })}
                  </div>
                </>
              ) : (
                <div style={{ fontSize: 12, color: '#718096', marginBottom: 8, fontStyle: 'italic' }}>
                  No animation frames set (using default hero sprite).
                </div>
              )}

              <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
                <select id="new-spawn-frame-select" style={{ flex: 1 }} defaultValue="">
                  <option value="" disabled>-- Select tile to add as hero frame --</option>
                  {Object.values(BUILTIN_TILESETS).map((ts) => (
                    <optgroup key={ts.id} label={ts.label}>
                      {ts.tiles.map((tile) => (
                        <option key={`${ts.id}.${tile.id}`} value={`${ts.id}.${tile.id}`}>
                          {tile.label} ({tile.gb_constant})
                        </option>
                      ))}
                    </optgroup>
                  ))}
                </select>
                <button
                  type="button"
                  className="btn-secondary btn-sm"
                  onClick={() => {
                    const sel = document.getElementById('new-spawn-frame-select') as HTMLSelectElement;
                    if (!sel || !sel.value) return;
                    const chosen = sel.value;
                    const current = level.spawn.animation_frames || [];
                    onUpdateSpawn({
                      ...level.spawn,
                      animation_frames: [...current, chosen],
                    });
                  }}
                >
                  ➕ Add Frame
                </button>
              </div>

              {/* Visual Quick-Picker Palette */}
              <div style={{ marginTop: 8 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 4 }}>
                  <label style={{ fontSize: 11, color: '#a0aec0', margin: 0 }}>
                    Quick Visual Tile Picker (Click to append):
                  </label>
                  {level.spawn.animation_frames && level.spawn.animation_frames.length > 0 && (
                    <button
                      type="button"
                      className="btn-tiny"
                      style={{ color: '#f85149' }}
                      title="Clear animation frames and revert to default sprite"
                      onClick={() => onUpdateSpawn({ ...level.spawn, animation_frames: undefined })}
                    >
                      🗑️ Clear
                    </button>
                  )}
                </div>
                <div
                  style={{
                    display: 'grid',
                    gridTemplateColumns: 'repeat(auto-fill, minmax(34px, 1fr))',
                    gap: 4,
                    maxHeight: 110,
                    overflowY: 'auto',
                    background: '#141a22',
                    padding: 4,
                    borderRadius: 4,
                    border: '1px solid #2d3848',
                  }}
                >
                  {allTilesWithScope
                    .filter((t) => t.category === 'npc' || t.category === 'enemy' || t.tilesetId === level.tileset)
                    .map((tile) => (
                      <button
                        key={tile.scopedId}
                        type="button"
                        title={`Click to add ${tile.label}`}
                        style={{
                          background: '#1c2430',
                          border: '1px solid #2d3848',
                          borderRadius: 4,
                          padding: 2,
                          cursor: 'pointer',
                          display: 'flex',
                          alignItems: 'center',
                          justifyContent: 'center',
                        }}
                        onClick={() => {
                          const current = level.spawn.animation_frames || [];
                          onUpdateSpawn({
                            ...level.spawn,
                            animation_frames: [...current, tile.scopedId],
                          });
                        }}
                      >
                        <img
                          src={tile.image_url}
                          alt={tile.label}
                          width={26}
                          height={26}
                          style={{ imageRendering: 'pixelated' }}
                        />
                      </button>
                    ))}
                </div>
              </div>
            </div>
          </div>
        )}

        {tab === 'context' && activeLayer === 'terrain' && (
          <div className="inspector-section">
            <h4>Terrain Painter</h4>
            <p className="hint-text">Select a tile from the palette and click or drag on the map.</p>
            <div className="stats-box">
              <div className="stat-item">
                <span className="stat-label">Map Size:</span>
                <span className="stat-value">{level.width} × {level.height} tiles</span>
              </div>
              <div className="stat-item">
                <span className="stat-label">Active Tileset:</span>
                <span className="stat-value">{level.tileset}</span>
              </div>
            </div>
          </div>
        )}

        {tab === 'context' && activeLayer === 'exits' && (
          <div className="inspector-section">
            {(() => {
              const entry = sceneOptions.find((s) => s.id === level.id);
              if (entry && entry.scene_id !== null) return null;
              return (
                <div
                  className="banner-warn"
                  style={{
                    background: '#3a2b00',
                    border: '1px solid #a80',
                    borderRadius: 4,
                    padding: 8,
                    marginBottom: 8,
                    fontSize: 12,
                  }}
                >
                  This level has no scene id yet — the ROM cannot compile
                  it. Save the level to register one automatically, then
                  recompile.
                </div>
              );
            })()}
            <div className="section-header-row">
              <h4>Exits ({level.exits.length})</h4>
              <button
                className="btn btn-sm btn-primary"
                onClick={() => {
                  onAddExit({
                    x: 12,
                    y: 0,
                    target_scene: 'mountain_pass',
                    target_x: 12,
                    target_y: 10,
                    direction: 'NORTH',
                    tile_char: '>',
                  });
                  onSelectEntityIndex(level.exits.length);
                }}
              >
                ➕ Add Exit
              </button>
            </div>

            {selectedExit && selectedEntityIndex !== null ? (
              <div className="entity-editor-box">
                <h5>Edit Selected Exit</h5>
                <div className="form-row">
                  <div className="form-group">
                    <label>Gate X</label>
                    <input
                      type="number"
                      value={selectedExit.x}
                      onChange={(e) =>
                        onUpdateExit(selectedEntityIndex, { ...selectedExit, x: parseInt(e.target.value) || 0 })
                      }
                    />
                  </div>
                  <div className="form-group">
                    <label>Gate Y</label>
                    <input
                      type="number"
                      value={selectedExit.y}
                      onChange={(e) =>
                        onUpdateExit(selectedEntityIndex, { ...selectedExit, y: parseInt(e.target.value) || 0 })
                      }
                    />
                  </div>
                </div>

                <div className="form-group">
                  <label>Target Scene</label>
                  <FilterCombo
                    items={sceneOptions.map((s) => ({
                      value: s.id,
                      label: `${s.name} (${s.id}.json)`,
                    }))}
                    value={selectedExit.target_scene}
                    onPick={(v) =>
                      onUpdateExit(selectedEntityIndex, { ...selectedExit, target_scene: v })
                    }
                    staleLabel={(v) => `${v} (unknown — pick a scene below)`}
                    placeholder="Filter scenes..."
                  />
                </div>

                <div className="form-row">
                  <div className="form-group">
                    <label>Target Spawn X</label>
                    <input
                      type="number"
                      value={selectedExit.target_x}
                      onChange={(e) =>
                        onUpdateExit(selectedEntityIndex, { ...selectedExit, target_x: parseInt(e.target.value) || 0 })
                      }
                    />
                  </div>
                  <div className="form-group">
                    <label>Target Spawn Y</label>
                    <input
                      type="number"
                      value={selectedExit.target_y}
                      onChange={(e) =>
                        onUpdateExit(selectedEntityIndex, { ...selectedExit, target_y: parseInt(e.target.value) || 0 })
                      }
                    />
                  </div>
                </div>

                <div className="form-row">
                  <div className="form-group">
                    <label>Direction</label>
                    <select
                      value={selectedExit.direction || 'SOUTH'}
                      onChange={(e) =>
                        onUpdateExit(selectedEntityIndex, {
                          ...selectedExit,
                          direction: e.target.value,
                          tile_char: e.target.value === 'NORTH' || e.target.value === 'EAST' ? '>' : '<',
                        })
                      }
                    >
                      <option value="NORTH">North (Up)</option>
                      <option value="SOUTH">South (Down)</option>
                      <option value="EAST">East (Right)</option>
                      <option value="WEST">West (Left)</option>
                    </select>
                  </div>
                  <div className="form-group">
                    <label>Glyph</label>
                    <input
                      type="text"
                      maxLength={1}
                      value={selectedExit.tile_char || '>'}
                      onChange={(e) =>
                        onUpdateExit(selectedEntityIndex, { ...selectedExit, tile_char: e.target.value })
                      }
                    />
                  </div>
                </div>

                <div className="btn-group-row">
                  <button
                    className="btn btn-sm btn-danger"
                    onClick={() => onDeleteExit(selectedEntityIndex)}
                  >
                    🗑️ Delete Exit
                  </button>
                  <button
                    className="btn btn-sm"
                    onClick={() => onSelectEntityIndex(null)}
                  >
                    Done
                  </button>
                </div>
              </div>
            ) : (
              <div className="entity-list">
                {level.exits.map((ex, idx) => (
                  <div
                    key={idx}
                    className="entity-list-item"
                    onClick={() => onSelectEntityIndex(idx)}
                  >
                    <span className="item-title">
                      🚪 ({ex.x},{ex.y}) → <strong>{ex.target_scene}</strong>
                    </span>
                    <span className="item-sub">spawn ({ex.target_x},{ex.target_y})</span>
                  </div>
                ))}
              </div>
            )}
            <ExitConnector levelId={level.id} exits={level.exits} />
            <div className="section-header-row" style={{ marginTop: 12 }}>
              <h4>Edge neighbors</h4>
            </div>
            <div style={{ fontSize: 12, opacity: 0.75, marginBottom: 8 }}>
              Whole-edge links: stepping onto a non-wall cell of a linked
              border crosses to that scene (mirrored entry). A point exit
              on the same cell wins. Paint open ground on the border (or
              leave it unpainted); walls stay walls.
            </div>
            {(() => {
              const neighbors = normalizeNeighbors(level.neighbors);
              const opposite: Record<string, string> = {
                north: 'south', south: 'north', east: 'west', west: 'east',
              };
              return (
                <div>
                  {NEIGHBOR_DIRS.map((direction) => (
                    <div className="form-group" key={direction}>
                      <label style={{ textTransform: 'capitalize' }}>{direction}</label>
                      <FilterCombo
                        items={[
                          { value: '', label: '(no link)' },
                          ...sceneOptions
                            .filter((s) => s.id !== level.id)
                            .map((s) => ({
                              value: s.id,
                              label: `${s.name} (${s.id}.json)`,
                            })),
                        ]}
                        value={neighbors[direction] || ''}
                        onPick={(v) =>
                          onUpdateNeighbors({ ...neighbors, [direction]: v })
                        }
                        staleLabel={(v) => `${v} (unknown — pick a scene below)`}
                        placeholder="Filter scenes..."
                      />
                      {neighbors[direction] ? (
                        <div className="item-sub">
                          {sceneOptions.some((s) => s.id === neighbors[direction])
                            ? `→ ${neighbors[direction]} (set its ${opposite[direction]} back for a return)`
                            : `⚠️ '${neighbors[direction]}' is not a known scene`}
                        </div>
                      ) : null}
                    </div>
                  ))}
                </div>
              );
            })()}
            <EdgeConnector level={level} />
          </div>
        )}

        {tab === 'context' && activeLayer === 'objects' && (
          <div className="inspector-section">
            <div className="section-header-row">
              <h4>Objects ({level.objects.length})</h4>
              <button
                className="btn btn-sm btn-primary"
                onClick={() => {
                  const t = OBJECT_TEMPLATES[0];
                  onAddObject({
                    id: `obj_${Date.now().toString().slice(-4)}`,
                    type: t.type,
                    position: { x: Math.floor(level.width / 2), y: Math.floor(level.height / 2) },
                    properties: { ...t.defaultProps },
                  });
                  onSelectEntityIndex(level.objects.length);
                }}
              >
                ➕ Add Object
              </button>
            </div>

            {selectedObject && selectedEntityIndex !== null ? (
              <div className="entity-editor-box">
                <h5>Edit Object</h5>
                <div className="form-group">
                  <label>Object ID</label>
                  <input
                    type="text"
                    value={selectedObject.id}
                    onChange={(e) =>
                      onUpdateObject(selectedEntityIndex, { ...selectedObject, id: e.target.value })
                    }
                  />
                </div>

                <div className="form-group">
                  <label>Type</label>
                  <select
                    value={selectedObject.type}
                    onChange={(e) => {
                      const newType = e.target.value as any;
                      const tmpl = OBJECT_TEMPLATES.find((t) => t.type === newType);
                      onUpdateObject(selectedEntityIndex, {
                        ...selectedObject,
                        type: newType,
                        properties: tmpl ? { ...tmpl.defaultProps } : selectedObject.properties,
                      });
                    }}
                  >
                    {OBJECT_TEMPLATES.map((t) => (
                      <option key={t.type} value={t.type}>
                        {t.icon} {t.label}
                      </option>
                    ))}
                  </select>
                </div>

                <div className="form-row">
                  <div className="form-group">
                    <label>Position X</label>
                    <input
                      type="number"
                      value={selectedObject.position.x}
                      onChange={(e) =>
                        onUpdateObject(selectedEntityIndex, {
                          ...selectedObject,
                          position: { ...selectedObject.position, x: parseInt(e.target.value) || 0 },
                        })
                      }
                    />
                  </div>
                  <div className="form-group">
                    <label>Position Y</label>
                    <input
                      type="number"
                      value={selectedObject.position.y}
                      onChange={(e) =>
                        onUpdateObject(selectedEntityIndex, {
                          ...selectedObject,
                          position: { ...selectedObject.position, y: parseInt(e.target.value) || 0 },
                        })
                      }
                    />
                  </div>
                </div>

                <div className="form-group">
                  <label>Display Name</label>
                  <input
                    type="text"
                    value={selectedObject.properties?.display_name || ''}
                    onChange={(e) =>
                      onUpdateObject(selectedEntityIndex, {
                        ...selectedObject,
                        properties: { ...selectedObject.properties, display_name: e.target.value },
                      })
                    }
                  />
                </div>

                {/* NPC object identity: the entity id is required for the
                    object to become an engine actor (entity-less objects
                    compile as decoration).  Art Type reuses the shared
                    enemy-types registry — SPRITE_KIND_ENEMY works for
                    statics too (sprite_tile_for is hostile-agnostic). */}
                {['npc', 'item', 'signpost'].includes(selectedObject.type) && (
                  <>
                    <div className="form-group">
                      <label>Entity ID (required for engine actors)</label>
                      <FilterCombo
                        items={[
                          { value: '', label: '(none — decoration, no interaction)' },
                          ...entityTypeList.map((t) => ({
                            value: t.entity_id,
                            label: `${t.label} (${t.entity_id})`,
                          })),
                        ]}
                        value={selectedObject.properties?.entity_id || ''}
                        onPick={(v) => {
                          const props = { ...(selectedObject.properties || {}) };
                          if (v) props.entity_id = v; else delete props.entity_id;
                          onUpdateObject(selectedEntityIndex, { ...selectedObject, properties: props });
                        }}
                        staleLabel={(v) => `${v} (unknown type — pick below or create one)`}
                        placeholder="Filter entity types..."
                      />
                      <button
                        className="btn btn-sm"
                        style={{ marginTop: 4 }}
                        onClick={async () => {
                          const raw = window.prompt('New entity type id (lowercase, underscores):', 'new_npc');
                          if (!raw) return;
                          const id = raw.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');
                          if (!id) return;
                          const label = window.prompt('Display label:', id.replace(/_/g, ' ')) || id;
                          try {
                            await saveEntityType('entity_types', {
                              id, label, kind: 'npc',
                              name: label.toUpperCase().slice(0, 20),
                              visual: (label[0] || 'N').toUpperCase(),
                              sprite_kind: 'tile',
                            });
                            refreshEntityTypes();
                            const props = {
                              ...(selectedObject.properties || {}),
                              entity_id: 'ENTITY_ID_' + id.toUpperCase(),
                            };
                            onUpdateObject(selectedEntityIndex, { ...selectedObject, properties: props });
                          } catch (err: any) {
                            window.alert(`Create failed: ${err.message}`);
                          }
                        }}
                      >
                        ＋ New entity type...
                      </button>
                      <span style={{ fontSize: 11, color: '#777' }}>
                        New types get an ENTITY_ID_* automatically on the next compile.
                      </span>
                    </div>
                    <div className="form-group">
                      <label>Art Type (shared sprite from the type registry)</label>
                      <select
                        value={selectedEnemyType || ''}
                        onChange={(e) => {
                          const props = { ...(selectedObject.properties || {}) };
                          if (!e.target.value) {
                            delete props.enemy_type;
                          } else {
                            props.enemy_type = e.target.value;
                          }
                          onUpdateObject(selectedEntityIndex, { ...selectedObject, properties: props });
                        }}
                      >
                        <option value="">(none — tile/ASCII fallback)</option>
                        {enemyTypeList.map((t) => (
                          <option key={t.id} value={t.id}>{t.label} ({t.id})</option>
                        ))}
                      </select>
                    </div>
                  </>
                )}

                {selectedObject.type === 'enemy' && (
                  <>
                    <div className="form-group">
                      <label>AI Pattern</label>
                      <select
                        value={selectedObject.properties?.ai || 'AI_PATROL_CROSS'}
                        onChange={(e) =>
                          onUpdateObject(selectedEntityIndex, {
                            ...selectedObject,
                            properties: { ...selectedObject.properties, ai: e.target.value },
                          })
                        }
                      >
<option value="AI_NONE">AI_NONE</option>
                      <option value="AI_PATROL_CROSS">AI_PATROL_CROSS</option>
                      <option value="AI_PATROL_CIRCLE">AI_PATROL_CIRCLE</option>
                      <option value="AI_CHASE">AI_CHASE</option>
                      <option value="AI_PATROL_VERT">AI_PATROL_VERT (up/down 3 tiles)</option>
                      </select>
                    </div>
                    <div className="form-group">
                      <label>Enemy Type</label>
                      <select
                        value={selectedEnemyType || ''}
                        onChange={(e) => {
                          const typeId = e.target.value;
                          const props = { ...(selectedObject.properties || {}) };
                          if (!typeId) {
                            delete props.enemy_type;
                            onUpdateObject(selectedEntityIndex, { ...selectedObject, properties: props });
                            return;
                          }
                          props.enemy_type = typeId;
                          if (!props.entity_id) props.entity_id = `ENTITY_ID_${typeId.toUpperCase()}`;
                          // Full-actor prefill from the type JSON (always
                          // overwrite): display name, battle routing,
                          // stats, AI, gold, flags -- the complete row
                          // compile.py needs for a hostile actor.  Every
                          // field stays editable afterward.
                          const nextObj: LevelObject = { ...selectedObject, properties: props };
                          props.facing = props.facing || 'DOWN';
                          if (!props.flags) props.flags = ['HOSTILE', 'BLOCKING', 'INTERACTABLE'];
                          props.visual = (props.visual as string) ||
                            ((typeId[0] || 'E').toUpperCase());
                          onUpdateObject(selectedEntityIndex, nextObj);
                          const objIndex = selectedEntityIndex;
                          const localUsed = new Set<number>(
                            level.objects
                              .map((o) => (o.properties || {}).actor_id as number)
                              .filter((n) => typeof n === 'number' && n > 0));
                          // Stats/battle come from the type JSON and the
                          // actor_id is assigned across ALL scenes
                          // (ActorIds must be unique scene-to-scene, so a
                          // within-level pick collides with e.g. the
                          // forest slime at compile).  One async merge
                          // (monotonic max-used + 1); the captured index
                          // keeps a click-away in the fetch window from
                          // updating the wrong object.
                          Promise.all([
                            fetchEnemyType(typeId).catch(() => null),
                            fetchUsedActorIds().catch(() => null),
                          ]).then(([t, usedList]) => {
                            const cur = { ...props };
                            if (t) {
                              cur.display_name = t.name || typeId.toUpperCase();
                              cur.battle = t.battle_id || cur.battle;
                              cur.hp = t.hp ?? 1;
                              cur.max_hp = t.max_hp ?? t.hp ?? 1;
                              cur.gold_reward = t.gold_reward ?? 0;
                              if (t.reward_currency) cur.reward_currency = t.reward_currency;
                              if (Array.isArray(t.ai_types) && t.ai_types.length > 0) {
                                cur.ai = t.ai_types[0];
                              }
                              if (t.label) cur.visual = (t.label[0] || 'E').toUpperCase();
                            }
                            if (!cur.actor_id) {
                              const used = new Set<number>(localUsed);
                              (usedList || []).forEach((u) => used.add(u.id));
                              let next = 1;
                              used.forEach((v) => { if (v >= next) next = v + 1; });
                              cur.actor_id = next;
                            }
                            onUpdateObject(objIndex, { ...nextObj, properties: cur });
                          });
                        }}
                      >
                        <option value="">-- choose enemy type --</option>
                        {enemyTypeList.map((t) => (
                          <option key={t.id} value={t.id}>{t.label} ({t.id})</option>
                        ))}
                      </select>
                      <div style={{ fontSize: 12, color: '#555', marginTop: 4 }}>
                        Sprite/art is configured in the Enemies view (art-only) — this dropdown picks which enemy type the placement is.
                      </div>
                    </div>
                    {/* Per-instance actor overrides: stats seeded from
                        the enemy type defaults, editable here per
                        placement.  Art/category stay type-owned (Enemies
                        view). */}
                    <div className="form-row">
                      <div className="form-group">
                        <label>HP</label>
                        <input
                          type="number"
                          min="0"
                          value={selectedObject.properties?.hp ?? 0}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              properties: { ...selectedObject.properties, hp: parseInt(e.target.value) || 0 },
                            })
                          }
                        />
                      </div>
                      <div className="form-group">
                        <label>Max HP</label>
                        <input
                          type="number"
                          min="0"
                          value={selectedObject.properties?.max_hp ?? 0}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              properties: { ...selectedObject.properties, max_hp: parseInt(e.target.value) || 0 },
                            })
                          }
                        />
                      </div>
                      <div className="form-group">
                        <label>Gold Reward</label>
                        <input
                          type="number"
                          min="0"
                          value={selectedObject.properties?.gold_reward ?? 0}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              properties: { ...selectedObject.properties, gold_reward: parseInt(e.target.value) || 0 },
                            })
                          }
                        />
                      </div>
                    </div>
                    <div className="form-row">
                      <div className="form-group">
                        <label>Battle</label>
                        <select
                          value={selectedObject.properties?.battle || 'BATTLE_NONE'}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              properties: { ...selectedObject.properties, battle: e.target.value },
                            })
                          }
                        >
                          {BATTLE_IDS.map((b) => (
                            <option key={b} value={b}>{b}</option>
                          ))}
                          {!BATTLE_IDS.includes(selectedObject.properties?.battle) && selectedObject.properties?.battle && (
                            <option value={selectedObject.properties.battle}>
                              {selectedObject.properties.battle} (custom)
                            </option>
                          )}
                        </select>
                      </div>
                      <div className="form-group">
                        <label>Facing</label>
                        <select
                          value={selectedObject.properties?.facing || 'DOWN'}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              properties: { ...selectedObject.properties, facing: e.target.value },
                            })
                          }
                        >
                          <option value="UP">UP / North</option>
                          <option value="DOWN">DOWN / South</option>
                          <option value="LEFT">LEFT / West</option>
                          <option value="RIGHT">RIGHT / East</option>
                        </select>
                      </div>
                    </div>
                    <div className="form-row">
                      <div className="form-group">
                        <label>Visual Glyph</label>
                        <input
                          type="text"
                          maxLength={1}
                          value={selectedObject.properties?.visual || ''}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              properties: { ...selectedObject.properties, visual: e.target.value },
                            })
                          }
                        />
                      </div>
                      <div className="form-group">
                        <label>Actor ID (0 = auto)</label>
                        <input
                          type="number"
                          min="0"
                          value={selectedObject.properties?.actor_id ?? 0}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              properties: { ...selectedObject.properties, actor_id: parseInt(e.target.value) || 0 },
                            })
                          }
                        />
                      </div>
                    </div>
                    <div className="form-group">
                      <label>
                        <input
                          type="checkbox"
                          checked={!!selectedObject.properties?.solo}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              properties: { ...selectedObject.properties, solo: e.target.checked },
                            })
                          }
                        />{' '}
                        Solo (engage alone, no trio clones)
                      </label>
                    </div>
                  </>
                )}

                {['npc', 'item', 'signpost'].includes(selectedObject.type) && (
                  <div className="form-group">
                    <label>Dialogue ID</label>
                    <FilterCombo
                      items={[
                        { value: '', label: '(no dialogue)' },
                        ...dialogueList.map((d) => ({
                          value: d.id,
                          label: `${d.label} (${d.id})`,
                        })),
                      ]}
                      value={selectedObject.properties?.dialogue || ''}
                      onPick={(v) =>
                        onUpdateObject(selectedEntityIndex, {
                          ...selectedObject,
                          properties: { ...selectedObject.properties, dialogue: v },
                        })
                      }
                      staleLabel={(v) => `${v} (unknown — pick below)`}
                      placeholder="Filter dialogues..."
                    />
                  </div>
                )}

                {selectedObject.type === 'npc' && (
                  <div className="form-group">
                    <label>Shop (stock list)</label>
                    <FilterCombo
                      items={[
                        { value: '', label: '(no shop)' },
                        ...shopList.map((s) => ({
                          value: String(s.id),
                          label: `${s.label} (id ${s.id})${s.owns ? ' · merchant' : ''}`,
                        })),
                      ]}
                      value={selectedObject.properties?.shop != null ? String(selectedObject.properties.shop) : ''}
                      onPick={(v) => {
                        const next = { ...selectedObject.properties };
                        if (v === '') delete next.shop;
                        else next.shop = Number(v);
                        onUpdateObject(selectedEntityIndex, { ...selectedObject, properties: next });
                      }}
                      staleLabel={(v) => `shop ${v} (unknown — pick below)`}
                      placeholder="Filter shops..."
                    />
                  </div>
                )}

                {/* Sprite/Tile Configuration (hidden for enemies: art is
                    configured in the dedicated Enemies view) */}
                {selectedObject.type !== 'enemy' && (
                <div className="inspector-section">
                  <h5>🎨 Sprite/Tile Configuration</h5>
                  {selectedEnemyType && (
                    <div style={{ fontSize: 12, background: '#eef6ee', border: '1px solid #9b9', padding: 6, marginBottom: 8 }}>
                      Overworld art controlled by enemy type <code>{selectedEnemyType}</code> (Enemies view) — one
                      shared sprite everywhere this enemy appears. Per-instance names below are ignored for it.
                    </div>
                  )}
                  
                  <div className="form-group">
                    <label>Overworld Sprite</label>
                    {(() => {
                      const currentVal = selectedObject.overworld_sprite || '';
                      const matched = allTilesWithScope.find(
                        (t) => t.scopedId === currentVal || t.id === currentVal
                      );
                      const selectVal = matched ? matched.scopedId : currentVal;

                      return (
                        <select
                          value={selectVal}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              overworld_sprite: e.target.value,
                            })
                          }
                        >
                          <option value="">-- None --</option>
                          {selectVal && !allTilesWithScope.some((t) => t.scopedId === selectVal) && (
                            <option value={selectVal}>{selectVal} (custom)</option>
                          )}
                          {Object.values(BUILTIN_TILESETS).map((ts) => (
                            <optgroup key={ts.id} label={ts.label}>
                              {ts.tiles.map((tile) => (
                                <option key={`${ts.id}.${tile.id}`} value={`${ts.id}.${tile.id}`}>
                                  {tile.label} ({tile.gb_constant})
                                </option>
                              ))}
                            </optgroup>
                          ))}
                        </select>
                      );
                    })()}
                  </div>

                  {/* Animation Frames (Multi-frame Sequence) */}
                  <div className="form-group animation-frames-group" style={{ marginTop: 8 }}>
                    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 6 }}>
                      <label style={{ margin: 0, fontWeight: 600 }}>🎞️ Animation Frames</label>
                      <div className="preset-buttons" style={{ display: 'flex', gap: 4 }}>
                        <button
                          type="button"
                          className="btn-tiny"
                          title="Set Fireplace Animation (2 frames)"
                          onClick={() => {
                            const newFrames = [
                              'desolate_landscape.desolate_fire_01',
                              'desolate_landscape.desolate_fire_02',
                            ];
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              animation_frames: newFrames,
                              overworld_sprite: newFrames[0],
                            });
                          }}
                        >
                          🔥 Fire
                        </button>
                        <button
                          type="button"
                          className="btn-tiny"
                          title="Set Kobold Animation (2 frames)"
                          onClick={() => {
                            const newFrames = [
                              'desolate_landscape.desolate_kobold_01',
                              'desolate_landscape.desolate_kobold_02',
                            ];
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              animation_frames: newFrames,
                              overworld_sprite: newFrames[0],
                            });
                          }}
                        >
                          👾 Kobold
                        </button>
                        <button
                          type="button"
                          className="btn-tiny"
                          title="Set Hero Animation (2 frames)"
                          onClick={() => {
                            const newFrames = [
                              'desolate_landscape.desolate_hero_01',
                              'desolate_landscape.desolate_hero_02',
                            ];
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              animation_frames: newFrames,
                              overworld_sprite: newFrames[0],
                            });
                          }}
                        >
                          🧙 Hero
                        </button>
                      </div>
                    </div>

                    {/* Live Preview Box */}
                    {selectedObject.animation_frames && selectedObject.animation_frames.length > 0 && (
                      <div className="anim-preview-box">
                        {(() => {
                          const curKey = selectedObject.animation_frames[previewTick % selectedObject.animation_frames.length];
                          const t = allTilesWithScope.find((tile) => tile.scopedId === curKey || tile.id === curKey);
                          return (
                            <>
                              {t?.image_url ? (
                                <img src={t.image_url} alt="" style={{ width: 28, height: 28, imageRendering: 'pixelated' }} />
                              ) : (
                                <span style={{ fontSize: 22 }}>👾</span>
                              )}
                              <span style={{ fontSize: 12, color: '#e6edf3' }}>
                                Playing frame {(previewTick % selectedObject.animation_frames.length) + 1} of {selectedObject.animation_frames.length} ({t?.label || curKey})
                              </span>
                            </>
                          );
                        })()}
                      </div>
                    )}

                    {/* Frames list */}
                    {selectedObject.animation_frames && selectedObject.animation_frames.length > 0 ? (
                      <div className="frames-list" style={{ display: 'flex', flexDirection: 'column', gap: 4, marginBottom: 8 }}>
                        {selectedObject.animation_frames.map((frameId, fIdx) => {
                          const t = allTilesWithScope.find((tile) => tile.scopedId === frameId || tile.id === frameId);
                          return (
                            <div
                              key={fIdx}
                              className="frame-row"
                              style={{
                                display: 'flex',
                                alignItems: 'center',
                                gap: 8,
                                background: '#1c2430',
                                padding: '4px 8px',
                                borderRadius: 4,
                                border: '1px solid #2d3848',
                              }}
                            >
                              <span style={{ fontSize: 11, color: '#a0aec0', width: 45 }}>
                                #{fIdx + 1}
                              </span>
                              {t?.image_url && (
                                <img
                                  src={t.image_url}
                                  alt=""
                                  style={{ width: 20, height: 20, imageRendering: 'pixelated' }}
                                />
                              )}
                              <span style={{ flex: 1, fontSize: 12, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                                {t?.label || frameId}
                              </span>
                              {fIdx > 0 && (
                                <button
                                  type="button"
                                  className="btn-icon-tiny"
                                  title="Move Up"
                                  onClick={() => {
                                    const frames = [...selectedObject.animation_frames!];
                                    const tmp = frames[fIdx - 1];
                                    frames[fIdx - 1] = frames[fIdx];
                                    frames[fIdx] = tmp;
                                    onUpdateObject(selectedEntityIndex, {
                                      ...selectedObject,
                                      animation_frames: frames,
                                      overworld_sprite: frames[0],
                                    });
                                  }}
                                >
                                  ⬆️
                                </button>
                              )}
                              {selectedObject.animation_frames && fIdx < selectedObject.animation_frames.length - 1 && (
                                <button
                                  type="button"
                                  className="btn-icon-tiny"
                                  title="Move Down"
                                  onClick={() => {
                                    const frames = [...selectedObject.animation_frames!];
                                    const tmp = frames[fIdx + 1];
                                    frames[fIdx + 1] = frames[fIdx];
                                    frames[fIdx] = tmp;
                                    onUpdateObject(selectedEntityIndex, {
                                      ...selectedObject,
                                      animation_frames: frames,
                                      overworld_sprite: frames[0],
                                    });
                                  }}
                                >
                                  ⬇️
                                </button>
                              )}
                              <button
                                type="button"
                                className="btn-icon-tiny"
                                title="Remove Frame"
                                onClick={() => {
                                  const frames = selectedObject.animation_frames!.filter((_, i) => i !== fIdx);
                                  onUpdateObject(selectedEntityIndex, {
                                    ...selectedObject,
                                    animation_frames: frames.length > 0 ? frames : undefined,
                                    overworld_sprite: frames.length > 0 ? frames[0] : selectedObject.overworld_sprite,
                                  });
                                }}
                              >
                                ❌
                              </button>
                            </div>
                          );
                        })}
                      </div>
                    ) : (
                      <div style={{ fontSize: 12, color: '#718096', marginBottom: 8, fontStyle: 'italic' }}>
                        No animation frames set (using static overworld sprite).
                      </div>
                    )}

                    {/* Add frame selector */}
                    <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
                      <select
                        id="new-object-frame-select"
                        style={{ flex: 1 }}
                        defaultValue=""
                      >
                        <option value="" disabled>-- Select tile to add as frame --</option>
                        {Object.values(BUILTIN_TILESETS).map((ts) => (
                          <optgroup key={ts.id} label={ts.label}>
                            {ts.tiles.map((tile) => (
                              <option key={`${ts.id}.${tile.id}`} value={`${ts.id}.${tile.id}`}>
                                {tile.label} ({tile.gb_constant})
                              </option>
                            ))}
                          </optgroup>
                        ))}
                      </select>
                      <button
                        type="button"
                        className="btn-secondary btn-sm"
                        onClick={() => {
                          const sel = document.getElementById('new-object-frame-select') as HTMLSelectElement;
                          if (!sel || !sel.value) return;
                          const chosen = sel.value;
                          const current = selectedObject.animation_frames || (selectedObject.overworld_sprite ? [selectedObject.overworld_sprite] : []);
                          const newFrames = [...current, chosen];
                          onUpdateObject(selectedEntityIndex, {
                            ...selectedObject,
                            animation_frames: newFrames,
                            overworld_sprite: newFrames[0],
                          });
                        }}
                      >
                        ➕ Add Frame
                      </button>
                    </div>

                    {/* Visual Quick-Picker Palette */}
                    <div style={{ marginTop: 8 }}>
                      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 4 }}>
                        <label style={{ fontSize: 11, color: '#a0aec0', margin: 0 }}>
                          Quick Visual Tile Picker (Click to append):
                        </label>
                        {selectedObject.animation_frames && selectedObject.animation_frames.length > 0 && (
                          <button
                            type="button"
                            className="btn-tiny"
                            style={{ color: '#f85149' }}
                            title="Clear animation frames and revert to static sprite"
                            onClick={() =>
                              onUpdateObject(selectedEntityIndex, {
                                ...selectedObject,
                                animation_frames: undefined,
                              })
                            }
                          >
                            🗑️ Clear
                          </button>
                        )}
                      </div>
                      <div
                        style={{
                          display: 'grid',
                          gridTemplateColumns: 'repeat(auto-fill, minmax(34px, 1fr))',
                          gap: 4,
                          maxHeight: 120,
                          overflowY: 'auto',
                          background: '#141a22',
                          padding: 4,
                          borderRadius: 4,
                          border: '1px solid #2d3848',
                        }}
                      >
                        {allTilesWithScope
                          .filter(
                            (t) =>
                              t.category === 'npc' ||
                              t.category === 'enemy' ||
                              t.category === 'object' ||
                              t.tilesetId === level.tileset
                          )
                          .map((tile) => (
                            <button
                              key={tile.scopedId}
                              type="button"
                              title={`Click to add ${tile.label}`}
                              style={{
                                background: '#1c2430',
                                border: '1px solid #2d3848',
                                borderRadius: 4,
                                padding: 2,
                                cursor: 'pointer',
                                display: 'flex',
                                alignItems: 'center',
                                justifyContent: 'center',
                              }}
                              onClick={() => {
                                const current =
                                  selectedObject.animation_frames ||
                                  (selectedObject.overworld_sprite ? [selectedObject.overworld_sprite] : []);
                                const newFrames = [...current, tile.scopedId];
                                onUpdateObject(selectedEntityIndex, {
                                  ...selectedObject,
                                  animation_frames: newFrames,
                                  overworld_sprite: newFrames[0],
                                });
                              }}
                            >
                              <img
                                src={tile.image_url}
                                alt={tile.label}
                                width={26}
                                height={26}
                                style={{ imageRendering: 'pixelated' }}
                              />
                            </button>
                          ))}
                      </div>
                    </div>
                  </div>

                  <div className="form-group">
                    <label>Battle Sprite</label>
                    {(() => {
                      const currentVal = selectedObject.battle_sprite || '';
                      const matched = allTilesWithScope.find(
                        (t) => t.scopedId === currentVal || t.id === currentVal
                      );
                      const selectVal = matched ? matched.scopedId : currentVal;

                      return (
                        <select
                          value={selectVal}
                          onChange={(e) =>
                            onUpdateObject(selectedEntityIndex, {
                              ...selectedObject,
                              battle_sprite: e.target.value,
                            })
                          }
                        >
                          <option value="">-- None --</option>
                          {selectVal && !allTilesWithScope.some((t) => t.scopedId === selectVal) && (
                            <option value={selectVal}>{selectVal} (custom)</option>
                          )}
                          {Object.values(BUILTIN_TILESETS).map((ts) => {
                            const enemyTiles = ts.tiles.filter((t) => t.category === 'enemy');
                            if (enemyTiles.length === 0) return null;
                            return (
                              <optgroup key={ts.id} label={ts.label}>
                                {enemyTiles.map((tile) => (
                                  <option key={`${ts.id}.${tile.id}`} value={`${ts.id}.${tile.id}`}>
                                    {tile.label} ({tile.gb_constant})
                                  </option>
                                ))}
                              </optgroup>
                            );
                          })}
                        </select>
                      );
                    })()}
                  </div>

                  <div className="form-group">
                    <label>Battle Name</label>
                    <input
                      type="text"
                      value={selectedObject.battle_name || selectedObject.properties?.display_name || ''}
                      onChange={(e) =>
                        onUpdateObject(selectedEntityIndex, {
                          ...selectedObject,
                          battle_name: e.target.value,
                        })
                      }
                      placeholder="Name shown in battle UI"
                    />
                  </div>

                  <div className="form-row">
                    <div className="form-group">
                      <label>Sprite Width (tiles)</label>
                      <input
                        type="number"
                        min="1"
                        max="4"
                        value={selectedObject.properties?.sprite_width || 1}
                        onChange={(e) =>
                          onUpdateObject(selectedEntityIndex, {
                            ...selectedObject,
                            properties: { ...selectedObject.properties, sprite_width: parseInt(e.target.value) || 1 },
                          })
                        }
                      />
                    </div>
                    <div className="form-group">
                      <label>Sprite Height (tiles)</label>
                      <input
                        type="number"
                        min="1"
                        max="4"
                        value={selectedObject.properties?.sprite_height || 1}
                        onChange={(e) =>
                          onUpdateObject(selectedEntityIndex, {
                            ...selectedObject,
                            properties: { ...selectedObject.properties, sprite_height: parseInt(e.target.value) || 1 },
                          })
                        }
                      />
                    </div>
                  </div>

                  <div className="form-group">
                    <label>Battle ID</label>
                    <input
                      type="text"
                      value={selectedObject.properties?.battle || ''}
                      onChange={(e) =>
                        onUpdateObject(selectedEntityIndex, {
                          ...selectedObject,
                          properties: { ...selectedObject.properties, battle: e.target.value },
                        })
                      }
                      placeholder="e.g., BATTLE_SLIME, BATTLE_BOSS"
                    />
                  </div>
                </div>
                )}

                {['npc', 'item', 'signpost'].includes(selectedObject.type) && (
                  <div className="form-group">
                    <label>Dialogue ID</label>
                    <FilterCombo
                      items={[
                        { value: '', label: '(no dialogue)' },
                        ...dialogueList.map((d) => ({
                          value: d.id,
                          label: `${d.label} (${d.id})`,
                        })),
                      ]}
                      value={selectedObject.properties?.dialogue || ''}
                      onPick={(v) =>
                        onUpdateObject(selectedEntityIndex, {
                          ...selectedObject,
                          properties: { ...selectedObject.properties, dialogue: v },
                        })
                      }
                      staleLabel={(v) => `${v} (unknown — pick below)`}
                      placeholder="Filter dialogues..."
                    />
                  </div>
                )}

                <div className="btn-group-row">
                  {onDuplicateObject && (
                    <button
                      className="btn btn-sm btn-primary"
                      onClick={() => onDuplicateObject(selectedEntityIndex)}
                      title="Clone / Duplicate object (Ctrl+D)"
                    >
                      📋 Clone
                    </button>
                  )}
                  <button
                    className="btn btn-sm btn-danger"
                    onClick={() => onDeleteObject(selectedEntityIndex)}
                  >
                    🗑️ Delete
                  </button>
                  <button
                    className="btn btn-sm"
                    onClick={() => onSelectEntityIndex(null)}
                  >
                    Done
                  </button>
                </div>
              </div>
            ) : (
              <div className="entity-list">
                {level.objects.map((obj, idx) => (
                  <div
                    key={idx}
                    className="entity-list-item"
                    onClick={() => onSelectEntityIndex(idx)}
                  >
                    <span className="item-title">
                      👾 <strong>{obj.id}</strong> ({obj.type})
                    </span>
                    <span className="item-sub">pos ({obj.position.x},{obj.position.y})</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}

        {tab === 'context' && activeLayer === 'regions' && (
          <div className="inspector-section">
            <div className="section-header-row">
              <h4>Regions ({level.regions.length})</h4>
              <button
                className="btn btn-sm btn-primary"
                onClick={() => {
                  onAddRegion({
                    id: `region_${Date.now().toString().slice(-4)}`,
                    bounds: { x: 2, y: 2, width: 6, height: 6 },
                    description: 'A newly defined semantic zone.',
                    gameplay: { purpose: 'exploration', difficulty: 1 },
                  });
                  onSelectEntityIndex(level.regions.length);
                }}
              >
                ➕ Add Region
              </button>
            </div>

            {selectedRegion && selectedEntityIndex !== null ? (
              <div className="entity-editor-box">
                <h5>Edit Region</h5>
                <div className="form-group">
                  <label>Region ID</label>
                  <input
                    type="text"
                    value={selectedRegion.id}
                    onChange={(e) =>
                      onUpdateRegion(selectedEntityIndex, { ...selectedRegion, id: e.target.value })
                    }
                  />
                </div>

                <div className="form-row">
                  <div className="form-group">
                    <label>X</label>
                    <input
                      type="number"
                      value={selectedRegion.bounds.x}
                      onChange={(e) =>
                        onUpdateRegion(selectedEntityIndex, {
                          ...selectedRegion,
                          bounds: { ...selectedRegion.bounds, x: parseInt(e.target.value) || 0 },
                        })
                      }
                    />
                  </div>
                  <div className="form-group">
                    <label>Y</label>
                    <input
                      type="number"
                      value={selectedRegion.bounds.y}
                      onChange={(e) =>
                        onUpdateRegion(selectedEntityIndex, {
                          ...selectedRegion,
                          bounds: { ...selectedRegion.bounds, y: parseInt(e.target.value) || 0 },
                        })
                      }
                    />
                  </div>
                  <div className="form-group">
                    <label>Width</label>
                    <input
                      type="number"
                      value={selectedRegion.bounds.width}
                      onChange={(e) =>
                        onUpdateRegion(selectedEntityIndex, {
                          ...selectedRegion,
                          bounds: { ...selectedRegion.bounds, width: parseInt(e.target.value) || 1 },
                        })
                      }
                    />
                  </div>
                  <div className="form-group">
                    <label>Height</label>
                    <input
                      type="number"
                      value={selectedRegion.bounds.height}
                      onChange={(e) =>
                        onUpdateRegion(selectedEntityIndex, {
                          ...selectedRegion,
                          bounds: { ...selectedRegion.bounds, height: parseInt(e.target.value) || 1 },
                        })
                      }
                    />
                  </div>
                </div>

                <div className="form-group">
                  <label>Semantic Description (LLM)</label>
                  <textarea
                    rows={3}
                    value={selectedRegion.description || ''}
                    onChange={(e) =>
                      onUpdateRegion(selectedEntityIndex, {
                        ...selectedRegion,
                        description: e.target.value,
                      })
                    }
                  />
                </div>

                <div className="form-row">
                  <div className="form-group">
                    <label>Purpose</label>
                    <input
                      type="text"
                      value={selectedRegion.gameplay?.purpose || 'exploration'}
                      onChange={(e) =>
                        onUpdateRegion(selectedEntityIndex, {
                          ...selectedRegion,
                          gameplay: { ...selectedRegion.gameplay, purpose: e.target.value },
                        })
                      }
                    />
                  </div>
                  <div className="form-group">
                    <label>Difficulty</label>
                    <input
                      type="number"
                      min="0"
                      max="10"
                      value={selectedRegion.gameplay?.difficulty || 1}
                      onChange={(e) =>
                        onUpdateRegion(selectedEntityIndex, {
                          ...selectedRegion,
                          gameplay: { ...selectedRegion.gameplay, difficulty: parseInt(e.target.value) || 1 },
                        })
                      }
                    />
                  </div>
                </div>

                <div className="btn-group-row">
                  <button
                    className="btn btn-sm btn-danger"
                    onClick={() => onDeleteRegion(selectedEntityIndex)}
                  >
                    🗑️ Delete
                  </button>
                  <button
                    className="btn btn-sm"
                    onClick={() => onSelectEntityIndex(null)}
                  >
                    Done
                  </button>
                </div>
              </div>
            ) : (
              <div className="entity-list">
                {level.regions.map((reg, idx) => (
                  <div
                    key={idx}
                    className="entity-list-item"
                    onClick={() => onSelectEntityIndex(idx)}
                  >
                    <span className="item-title">
                      🏷️ <strong>{reg.id}</strong> ({reg.gameplay?.purpose || 'zone'})
                    </span>
                    <span className="item-sub">
                      [{reg.bounds.x},{reg.bounds.y}] {reg.bounds.width}×{reg.bounds.height}
                    </span>
                  </div>
                ))}
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
};
