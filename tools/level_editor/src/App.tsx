import React, { useState, useEffect, useCallback } from 'react';
import './App.css';
import { EditorLevel, LevelExit, LevelNeighbors, LevelRegion, createEmptyEditorLevel, levelDataToEditor, normalizeNeighbors } from './model/Level';
import { LevelObject } from './model/Objects';
import { Toolbar, ToolType } from './Toolbar';
import { EditLayer } from './LayerPanel';
import { TilesetPalette } from './TilesetPalette';
import { Inspector } from './Inspector';
import { MapCanvas } from './MapCanvas';
import { downloadLevelJson, saveLevelToServer, deleteLevel, cleanRetiredOrphans, compileRom, runGame, fetchUsedActorIds } from './io/saveLevel';
import { fetchLevelList, fetchLevelData, refreshTilesetsFromServer } from './io/serverLevels';
import { fetchEnemyTypeList } from './io/combatArt';
import { fetchEntityTypeList } from './io/entityTypes';
import { FilterCombo } from './FilterCombo';
import { promptLoadLevelFile } from './io/loadLevel';
import { BUILTIN_TILESETS, getTileset, TileDefinition } from './model/Tileset';
import { TilesetReviewer } from './TilesetReviewer';
import { CombatArtStudio } from './CombatArtStudio';
import { EnemyManager } from './EnemyManager';
import { HeroManager } from './HeroManager';
import { BattleManager } from './BattleManager';
import { DialogueManager } from './DialogueManager';
import { ShopManager } from './ShopManager';
import { PaletteManager } from './PaletteManager';
import { SfxTesterModal } from './SfxTester';

// Built-in levels from repository
import forestData from '../../../levels/forest.json';
import fieldData from '../../../levels/field.json';
import southFieldData from '../../../levels/south_field.json';
import townData from '../../../levels/town.json';
import mountainPassData from '../../../levels/mountain_pass.json';
import castleData from '../../../levels/castle.json';
import titleData from '../../../screens/title.json';

interface ExistingLevelItem {
  id: string;
  name: string;
  data: any;
  category?: 'levels' | 'screens';
  /** Assigned scene id from the disk catalogue; null/undefined means
   * unregistered (save the level to assign one). */
  scene_id?: number | null;
}

const EXISTING_LEVELS: ExistingLevelItem[] = [
  { id: 'forest', name: forestData.name || 'Forest', data: forestData, category: 'levels' },
  { id: 'field', name: fieldData.name || 'Field', data: fieldData, category: 'levels' },
  { id: 'south_field', name: southFieldData.name || 'Desolate South Field', data: southFieldData, category: 'levels' },
  { id: 'town', name: townData.name || 'Town', data: townData, category: 'levels' },
  { id: 'mountain_pass', name: mountainPassData.name || 'Mountain Pass', data: mountainPassData, category: 'levels' },
  { id: 'castle', name: castleData.name || 'Castle', data: castleData, category: 'levels' },
  { id: 'title', name: 'Title Screen', data: titleData, category: 'screens' },
];

export const App: React.FC = () => {
  const [level, setLevel] = useState<EditorLevel>(() => levelDataToEditor(forestData as any));
  const [currentLevelId, setCurrentLevelId] = useState<string>('forest');
  // Disk-backed catalogue (refreshed from /api/levels on mount and after
  // saves, so hand-edited JSON shows up without an editor rebuild).
  // Bundled static imports above remain the fallback when the dev API is
  // unreachable (e.g. a built preview bundle).
  const [levelItems, setLevelItems] = useState<ExistingLevelItem[]>(EXISTING_LEVELS);
  const [diskLive, setDiskLive] = useState<boolean>(false);

  // Tool & Layer State
  const [activeTool, setActiveTool] = useState<ToolType>('brush');
  const [activeLayer, setActiveLayer] = useState<EditLayer>('terrain');
  const [selectedTileId, setSelectedTileId] = useState<string>('tree');
  const [selectedEntityIndex, setSelectedEntityIndex] = useState<number | null>(null);
  const [clonePattern, setClonePattern] = useState<string[][] | null>(null);

  // View Options
  const [zoom, setZoom] = useState<number>(1);
  const [showGrid, setShowGrid] = useState<boolean>(true);
  const [showCollision, setShowCollision] = useState<boolean>(false);
  const [showTerrain, setShowTerrain] = useState<boolean>(true);
  const [showExits, setShowExits] = useState<boolean>(true);
  const [showObjects, setShowObjects] = useState<boolean>(true);
  const [showRegions, setShowRegions] = useState<boolean>(true);

  // Undo / Redo Stacks
  const [history, setHistory] = useState<EditorLevel[]>([]);
  const [redoStack, setRedoStack] = useState<EditorLevel[]>([]);

  // Modals
  const [showValidateModal, setShowValidateModal] = useState<boolean>(false);
  // Actor ids claimed by OTHER scenes (levels/*.json on disk).  Makes
  // the browser validation catch the cross-scene uniqueness rule the
  // toolchain enforces at compile; refreshed on level load and each
  // time the Validate modal opens.
  const [crossActorIds, setCrossActorIds] = useState<Array<{ id: number; level: string }>>([]);
  const refreshCrossActorIds = useCallback(() => {
    fetchUsedActorIds(level.id).then(setCrossActorIds).catch(() => setCrossActorIds([]));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [level.id]);
  useEffect(() => {
    refreshCrossActorIds();
  }, [refreshCrossActorIds]);
  const [showDescribeModal, setShowDescribeModal] = useState<boolean>(false);
  const [showSoundTestModal, setShowSoundTestModal] = useState<boolean>(false);
  const [showTilesetReviewer, setShowTilesetReviewer] = useState<boolean>(false);
  const [showCombatArt, setShowCombatArt] = useState<boolean>(false);
  // Enemies view (art-only): dropdown value 'enemy:<id>' swaps the main
  // area to the EnemyManager; the level underneath is left untouched.
  const [enemyView, setEnemyView] = useState<string | null>(null);
  const [heroView, setHeroView] = useState<boolean>(false);
  // Dialogue view (screens/dialogue/*.json): text-only editor; the level
  // underneath is left untouched.
  const [dialogueView, setDialogueView] = useState<string | null>(null);
  // Shop view (screens/shops/<id>.json): stock lists referenced by NPCs.
  const [shopView, setShopView] = useState<boolean>(false);
  // Palette view: preview/assign the engine CGB ramps to tiles + enemies.
  const [paletteView, setPaletteView] = useState<boolean>(false);
  // Battle view (screens/battle_hud.json + battle/<id>.json layout +
  // cards_skin.json) — the whole battle-time view, editable from here.
  const [cardView, setCardView] = useState<boolean>(false);
  const [enemyItems, setEnemyItems] = useState<Array<{ id: string; name: string }>>([]);
  // Known ENTITY_ID_* values (entity-type registry); used by browser
  // validation so it never says "valid" for an id the compiler rejects.
  const [entityTypeIds, setEntityTypeIds] = useState<Set<string>>(new Set());
  const [describeFormat, setDescribeFormat] = useState<'markdown' | 'json'>('markdown');

  // Compilation & Run State
  const [isCompiling, setIsCompiling] = useState<boolean>(false);
  const [isRunning, setIsRunning] = useState<boolean>(false);
  const [notification, setNotification] = useState<{ message: string; type: 'success' | 'error' | 'info' } | null>(null);

  useEffect(() => {
    if (notification) {
      const timer = setTimeout(() => setNotification(null), 4000);
      return () => clearTimeout(timer);
    }
  }, [notification]);

  // Load the enemy catalogue for the Enemies dropdown group (best
  // effort: the manager falls back to its own fetch when offline).
  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const items = await fetchEnemyTypeList();
        if (cancelled) return;
        setEnemyItems(items.map((e) => ({ id: e.id, name: e.label || e.id })));
      } catch {
        // Offline: EnemyManager shows its own load error.
      }
      try {
        const types = await fetchEntityTypeList();
        if (cancelled) return;
        setEntityTypeIds(new Set(types.map((t) => t.entity_id)));
      } catch {
        // Offline: entity-id membership checks are skipped (compile still gates).
      }
    })();
    return () => { cancelled = true; };
  }, []);

  // Load the level catalogue + tilesets from disk once on mount.
  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const [items, changedTilesets] = await Promise.all([
          fetchLevelList(),
          refreshTilesetsFromServer().catch(() => [] as string[]),
        ]);
        if (cancelled) return;
        const byId = new Map(EXISTING_LEVELS.map((l) => [l.id, l]));
        const fresh: ExistingLevelItem[] = [];
        for (const it of items) {
          try {
            const data = await fetchLevelData(it.category, it.id);
            fresh.push({ id: it.id, name: it.name, data, category: it.category,
                         scene_id: it.scene_id ?? null });
          } catch {
            const b = byId.get(it.id);
            if (b) fresh.push(b);
          }
        }
        for (const b of EXISTING_LEVELS) {
          if (!fresh.some((f) => f.id === b.id)) fresh.push(b);
        }
        setLevelItems(fresh);
        setDiskLive(true);
        // Reload the boot level from disk so hand edits show immediately,
        // and re-render with the refreshed tilesets.
        const boot = fresh.find((f) => f.id === 'forest');
        if (boot) {
          setLevel(levelDataToEditor(boot.data));
          setCurrentLevelId('forest');
        } else if (changedTilesets.length > 0) {
          setLevel((prev) => ({ ...prev }));
        }
      } catch {
        // Dev API unreachable: keep the bundled snapshot.
      }
    })();
    return () => { cancelled = true; };
  }, []);

  const handleSaveToServer = async () => {
    setNotification({ message: 'Saving level to disk...', type: 'info' });
    // currentLevelId is the id the level was loaded under; if the user
    // edited the Scene ID it differs, which signals a rename (the server
    // preserves the numeric scene id and rewires exits).
    const previousId = currentLevelId && currentLevelId !== level.id ? currentLevelId : null;
    const res = await saveLevelToServer(level, previousId);
    if (res.success) {
      if (previousId) setCurrentLevelId(level.id);
      setNotification({ message: `Successfully saved ${level.id} to ${res.path}!`, type: 'success' });
      // Refresh the catalogue so newly created ids appear without a rebuild.
      try {
        const items = await fetchLevelList();
        const known = new Set(levelItems.map((f) => f.id));
        const added: ExistingLevelItem[] = [];
        for (const it of items) {
          if (!known.has(it.id)) {
            try {
              added.push({ id: it.id, name: it.name, data: await fetchLevelData(it.category, it.id), category: it.category,
                           scene_id: it.scene_id ?? null });
            } catch {
              // Leave it out; the next save retries.
            }
          }
        }
        if (added.length > 0) setLevelItems((prev) => [...prev, ...added]);
        setDiskLive(true);
      } catch {
        // Catalogue refresh is best-effort; the save itself succeeded.
      }
    } else {
      setNotification({ message: `Save failed: ${res.error}`, type: 'error' });
    }
  };

  const handleDeleteLevel = async () => {
    if (level.isScreen) return;
    if (!confirm(
      `Delete level '${level.id}'?\n\n` +
      `Its scene id is retired (never reused) and any exit in other levels ` +
      `that targets it is cleared. This cannot be undone.`
    )) return;
    const res = await deleteLevel(level.id);
    if (!res.success) {
      setNotification({ message: `Delete failed: ${res.error}`, type: 'error' });
      return;
    }
    setNotification({
      message: `Deleted '${level.id}' (scene id retired${res.cleared ? `, ${res.cleared} exit(s) cleared` : ''}).`,
      type: 'success',
    });
    try {
      const items = await fetchLevelList();
      const remaining = items.filter(
        (it) => it.category === 'levels' && it.id !== level.id);
      const next = remaining[0];
      if (next) {
        const data = await fetchLevelData('levels', next.id);
        pushState(levelDataToEditor(data));
        setCurrentLevelId(next.id);
      } else {
        handleCreateNewLevel();
      }
      const fresh: ExistingLevelItem[] = [];
      for (const it of items) {
        if (it.id === level.id && it.category === 'levels') continue;
        try {
          fresh.push({ id: it.id, name: it.name, data: await fetchLevelData(it.category, it.id), category: it.category,
                       scene_id: it.scene_id ?? null });
        } catch { /* skip unreadable */ }
      }
      setLevelItems(fresh);
    } catch {
      // Catalogue refresh is best-effort; the delete itself succeeded.
    }
  };

  // One-click repair for a delete whose unlink did not stick: removes every
  // levels/<id>.json whose id is retired.  Such a file hard-fails Compile
  // ROM and keeps its actor ids reserved (see the Inspector banner).
  const handleCleanRetiredOrphans = async () => {
    const res = await cleanRetiredOrphans();
    if (!res.success) {
      setNotification({ message: `Clean retired orphans failed: ${res.error}`, type: 'error' });
      return;
    }
    const removed = res.removed || [];
    setNotification({
      message: removed.length
        ? `Removed retired orphan file(s): ${removed.join(', ')}. Recompile.`
        : 'No retired orphan files found.',
      type: removed.length ? 'success' : 'info',
    });
    try {
      const items = await fetchLevelList();
      const fresh: ExistingLevelItem[] = [];
      for (const it of items) {
        try {
          fresh.push({ id: it.id, name: it.name, data: await fetchLevelData(it.category, it.id), category: it.category,
                       scene_id: it.scene_id ?? null });
        } catch { /* skip unreadable */ }
      }
      setLevelItems(fresh);
    } catch {
      // best-effort refresh
    }
  };

  const handleCompileRom = async () => {
    setIsCompiling(true);
    setNotification({ message: 'Saving level & compiling Game Boy ROMs (make debug + release, parallel)...', type: 'info' });
    const previousId = currentLevelId && currentLevelId !== level.id ? currentLevelId : null;
    const saved = await saveLevelToServer(level, previousId);
    if (!saved.success) {
      setIsCompiling(false);
      setNotification({ message: `Save failed, ROMs not compiled: ${saved.error}`, type: 'error' });
      return;
    }
    if (previousId) setCurrentLevelId(level.id);
    const res = await compileRom();
    setIsCompiling(false);
    if (res.success) {
      setNotification({ message: 'ROMs Built Successfully! (debug + release)', type: 'success' });
    } else {
      setNotification({ message: `ROM Compilation Failed: ${res.error}`, type: 'error' });
      alert(`Compilation failed:\n\n${res.error}\n\n${res.log || ''}`);
    }
  };

  const handleRunGame = async () => {
    setIsRunning(true);
    setNotification({ message: 'Launching emulator...', type: 'info' });
    const res = await runGame();
    setIsRunning(false);
    if (res.success) {
      if (res.stale && res.stale.length > 0) {
        setNotification({ message: `Launched emulator on desktop - WARNING: ROM is stale (newer content: ${res.stale.join(', ')}). Compile ROM first!`, type: 'error' });
      } else {
        setNotification({ message: res.message || 'Launched emulator on desktop!', type: 'success' });
      }
    } else {
      setNotification({ message: `Launch failed: ${res.error}`, type: 'error' });
      alert(`Failed to launch emulator:\n\n${res.error}`);
    }
  };

  // Push state to undo history
  const pushState = useCallback((newLevel: EditorLevel) => {
    setHistory((prev) => [...prev.slice(-30), JSON.parse(JSON.stringify(level))]);
    setRedoStack([]);
    setLevel(newLevel);
  }, [level]);

  // Undo / Redo
  const handleUndo = () => {
    if (history.length === 0) return;
    const previous = history[history.length - 1];
    setHistory((prev) => prev.slice(0, -1));
    setRedoStack((prev) => [JSON.parse(JSON.stringify(level)), ...prev]);
    setLevel(previous);
  };

  const handleRedo = () => {
    if (redoStack.length === 0) return;
    const next = redoStack[0];
    setRedoStack((prev) => prev.slice(1));
    setHistory((prev) => [...prev, JSON.parse(JSON.stringify(level))]);
    setLevel(next);
  };

  // Keyboard Shortcuts
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement || e.target instanceof HTMLSelectElement) {
        return;
      }

      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
        e.preventDefault();
        if (e.shiftKey) {
          handleRedo();
        } else {
          handleUndo();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') {
        e.preventDefault();
        handleRedo();
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 's') {
        e.preventDefault();
        downloadLevelJson(level);
      } else if (e.key.toLowerCase() === 'b') {
        setActiveTool('brush');
      } else if (e.key.toLowerCase() === 'e') {
        setActiveTool('eraser');
      } else if (e.key.toLowerCase() === 'r') {
        setActiveTool('rect');
      } else if (e.key.toLowerCase() === 'g') {
        setActiveTool('fill');
      } else if (e.key.toLowerCase() === 'i') {
        setActiveTool('eyedropper');
      } else if (e.key.toLowerCase() === 'v') {
        setActiveTool('select');
      } else if (e.key.toLowerCase() === 'c') {
        setActiveTool('clone');
      } else if (e.key === 'Escape' && activeTool === 'clone') {
        setClonePattern(null);
        setNotification({ message: 'Clone buffer cleared. Drag a box to copy tiles.', type: 'info' });
      } else if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'd') {
        e.preventDefault();
        handleDuplicateSelectedEntity();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [level, history, redoStack]);

  // Select level from dropdown (fresh disk content first, bundled fallback).
  const handleSelectLevel = async (selectedId: string) => {
    if (selectedId === '__new__') {
      handleCreateNewLevel();
      return;
    }
    if (selectedId === 'hero') {
      setHeroView(true);
      setEnemyView(null);
      setCardView(false);
      setDialogueView(null);
      setShopView(false);
      setPaletteView(false);
      setSelectedEntityIndex(null);
      return;
    }
    if (selectedId === 'cards') {
      setCardView(true);
      setEnemyView(null);
      setHeroView(false);
      setDialogueView(null);
      setShopView(false);
      setPaletteView(false);
      setSelectedEntityIndex(null);
      return;
    }
    if (selectedId === 'shops') {
      setShopView(true);
      setEnemyView(null);
      setHeroView(false);
      setCardView(false);
      setDialogueView(null);
      setPaletteView(false);
      setSelectedEntityIndex(null);
      return;
    }
    if (selectedId === 'palettes') {
      setPaletteView(true);
      setEnemyView(null);
      setHeroView(false);
      setCardView(false);
      setDialogueView(null);
      setShopView(false);
      setSelectedEntityIndex(null);
      return;
    }
    if (selectedId === 'dialogues' || selectedId.startsWith('dialogue:')) {
      setDialogueView(selectedId.startsWith('dialogue:')
        ? selectedId.slice('dialogue:'.length) : '');
      setEnemyView(null);
      setHeroView(false);
      setCardView(false);
      setShopView(false);
      setPaletteView(false);
      setSelectedEntityIndex(null);
      return;
    }
    if (selectedId.startsWith('enemy:')) {
      setEnemyView(selectedId.slice('enemy:'.length));
      setShopView(false);
      setPaletteView(false);
      setSelectedEntityIndex(null);
      return;
    }
    setEnemyView(null);
    setHeroView(false);
    setCardView(false);
    setDialogueView(null);
    setShopView(false);
    setPaletteView(false);

    const found = levelItems.find((l) => l.id === selectedId);
    if (found) {
      let data = found.data;
      if (diskLive) {
        try {
          data = await fetchLevelData(found.category || 'levels', selectedId);
        } catch {
          // Fall back to the catalogue snapshot.
        }
      }
      const parsed = levelDataToEditor(data);
      pushState(parsed);
      setCurrentLevelId(selectedId);
      setSelectedEntityIndex(null);
    }
  };

  // Create new level
  const handleCreateNewLevel = () => {
    const id = prompt('Enter new level ID (e.g. desert_ruins):', 'new_level');
    if (!id) return;
    const cleanId = id.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_') || 'new_level';
    const empty = createEmptyEditorLevel(cleanId, 'forest', 20, 18);
    empty.name = cleanId.replace(/_/g, ' ').replace(/\b\w/g, (c) => c.toUpperCase());
    pushState(empty);
    setCurrentLevelId(cleanId);
    setSelectedEntityIndex(null);
  };

  // Drawing Actions (all terrain mutations flag terrainDirty so the save
  // path emits the edited grid instead of the as-loaded block form).
  const handleTilePainted = (x: number, y: number, tileId: string) => {
    if (level.grid[y]?.[x] === tileId) return;
    const newGrid = level.grid.map((row, ry) =>
      ry === y ? row.map((col, rx) => (rx === x ? tileId : col)) : [...row]
    );
    pushState({ ...level, grid: newGrid, terrainDirty: true });
  };

  const handleRectPainted = (rx: number, ry: number, rw: number, rh: number, tileId: string) => {
    const newGrid = level.grid.map((row, y) =>
      row.map((col, x) => (x >= rx && x < rx + rw && y >= ry && y < ry + rh ? tileId : col))
    );
    pushState({ ...level, grid: newGrid, terrainDirty: true });
  };

  const handleClonePatternCaptured = (pattern: string[][]) => {
    if (!pattern || pattern.length === 0 || pattern[0].length === 0) return;
    setClonePattern(pattern);
    const pw = pattern[0].length;
    const ph = pattern.length;
    setNotification({
      message: `Copied ${pw}×${ph} tile area! Click to stamp • Shift+Drag to copy a new area.`,
      type: 'info'
    });
  };

  const handleStampPattern = (startX: number, startY: number, pattern: string[][]) => {
    if (!pattern || pattern.length === 0 || pattern[0].length === 0) return;
    const pw = pattern[0].length;
    const ph = pattern.length;

    let changed = false;
    for (let py = 0; py < ph; py++) {
      for (let px = 0; px < pw; px++) {
        const tx = startX + px;
        const ty = startY + py;
        if (tx >= 0 && tx < level.width && ty >= 0 && ty < level.height) {
          if (level.grid[ty]?.[tx] !== pattern[py][px]) {
            changed = true;
            break;
          }
        }
      }
      if (changed) break;
    }
    if (!changed) return;

    const newGrid = level.grid.map((row, y) =>
      row.map((col, x) => {
        if (x >= startX && x < startX + pw && y >= startY && y < startY + ph) {
          const px = x - startX;
          const py = y - startY;
          return pattern[py]?.[px] ?? col;
        }
        return col;
      })
    );
    pushState({ ...level, grid: newGrid, terrainDirty: true });
  };

  const handleDuplicateSelectedEntity = () => {
    if (selectedEntityIndex === null) return;
    if (selectedEntityIndex >= 0 && selectedEntityIndex < level.objects.length) {
      const orig = level.objects[selectedEntityIndex];
      const copy = {
        ...JSON.parse(JSON.stringify(orig)),
        id: `${orig.id}_copy`,
        position: {
          x: Math.min(orig.position.x + 1, level.width - 1),
          y: Math.min(orig.position.y + 1, level.height - 1)
        }
      };
      // A deep copy clones properties.actor_id verbatim, which collides
      // with the original (actor ids are globally unique; validate.py and
      // the Validate modal both reject duplicates).  Mint a fresh id for
      // the copy: one past the highest id used in this level or any other
      // scene on disk.  Unset (0/missing) stays unset -- it cannot collide.
      // entity_id is the shared enemy *type* and is correctly kept as-is.
      const origAid = (orig.properties as any)?.actor_id;
      let dupNote = '';
      if (typeof origAid === 'number' && Number.isInteger(origAid) && origAid > 0) {
        let peak = 0;
        for (const o of level.objects) {
          const a = (o.properties as any)?.actor_id;
          if (typeof a === 'number' && Number.isInteger(a) && a > peak) peak = a;
        }
        for (const c of crossActorIds) {
          if (c.id > peak) peak = c.id;
        }
        if (peak >= 65535) {
          (copy.properties as any).actor_id = 0;
          dupNote = ' (actor_id pool exhausted, reset to unset!)';
        } else {
          (copy.properties as any).actor_id = peak + 1;
          dupNote = ` (actor_id ${peak + 1})`;
        }
      }
      const newObjs = [...level.objects, copy];
      pushState({ ...level, objects: newObjs });
      setSelectedEntityIndex(newObjs.length - 1);
      setNotification({ message: `Duplicated object "${orig.id}"!${dupNote}`, type: 'success' });
    }
  };

  const handleFill = (startX: number, startY: number, newTileId: string) => {
    const targetTileId = level.grid[startY]?.[startX];
    if (!targetTileId || targetTileId === newTileId) return;

    const newGrid = level.grid.map((row) => [...row]);
    const queue: Array<[number, number]> = [[startX, startY]];
    const visited = new Set<string>();

    while (queue.length > 0) {
      const [cx, cy] = queue.pop()!;
      const key = `${cx},${cy}`;
      if (visited.has(key)) continue;
      visited.add(key);

      if (cx < 0 || cx >= level.width || cy < 0 || cy >= level.height) continue;
      if (newGrid[cy][cx] !== targetTileId) continue;

      newGrid[cy][cx] = newTileId;

      queue.push([cx + 1, cy]);
      queue.push([cx - 1, cy]);
      queue.push([cx, cy + 1]);
      queue.push([cx, cy - 1]);
    }

    pushState({ ...level, grid: newGrid, terrainDirty: true });
  };

  // Spawn Move
  const handleSpawnMoved = (x: number, y: number) => {
    pushState({ ...level, spawn: { ...level.spawn, x, y } });
  };

  // Object Operations
  const handleObjectMoved = (index: number, x: number, y: number) => {
    const newObjs = [...level.objects];
    if (newObjs[index]) {
      newObjs[index] = { ...newObjs[index], position: { x, y } };
      pushState({ ...level, objects: newObjs });
    }
  };

  const handleAddObject = (obj: LevelObject) => {
    pushState({ ...level, objects: [...level.objects, obj] });
  };

  const handleUpdateObject = (index: number, obj: LevelObject) => {
    const newObjs = [...level.objects];
    newObjs[index] = obj;
    pushState({ ...level, objects: newObjs });
  };

  const handleDeleteObject = (index: number) => {
    const newObjs = level.objects.filter((_, i) => i !== index);
    pushState({ ...level, objects: newObjs });
    setSelectedEntityIndex(null);
  };

  // Exit Operations
  const handleExitMoved = (index: number, x: number, y: number) => {
    const newExits = [...level.exits];
    if (newExits[index]) {
      newExits[index] = { ...newExits[index], x, y };
      pushState({ ...level, exits: newExits });
    }
  };

  const handleAddExit = (exit: LevelExit) => {
    pushState({ ...level, exits: [...level.exits, exit] });
  };

  const handleUpdateExit = (index: number, exit: LevelExit) => {
    const newExits = [...level.exits];
    newExits[index] = exit;
    pushState({ ...level, exits: newExits });
  };

  const handleDeleteExit = (index: number) => {
    const newExits = level.exits.filter((_, i) => i !== index);
    pushState({ ...level, exits: newExits });
    setSelectedEntityIndex(null);
  };

  const handleUpdateNeighbors = (neighbors: LevelNeighbors) => {
    pushState({ ...level, neighbors: { ...normalizeNeighbors(level.neighbors), ...neighbors } });
  };

  // Region Operations
  const handleAddRegion = (region: LevelRegion) => {
    pushState({ ...level, regions: [...level.regions, region] });
  };

  const handleUpdateRegion = (index: number, region: LevelRegion) => {
    const newRegions = [...level.regions];
    newRegions[index] = region;
    pushState({ ...level, regions: newRegions });
  };

  const handleDeleteRegion = (index: number) => {
    const newRegions = level.regions.filter((_, i) => i !== index);
    pushState({ ...level, regions: newRegions });
    setSelectedEntityIndex(null);
  };

  // Level Meta Updates
  const handleUpdateLevelMeta = (updates: Partial<EditorLevel>) => {
    let newGrid = level.grid;
    const newW = updates.width !== undefined ? updates.width : level.width;
    const newH = updates.height !== undefined ? updates.height : level.height;

    if (newW !== level.width || newH !== level.height) {
      const ts = getTileset(level.tileset);
      const dwShort = level.defaultWalkable?.includes('.')
        ? level.defaultWalkable.split('.')[1]
        : level.defaultWalkable;
      const defaultFloor = (dwShort && ts.tiles.some((t: TileDefinition) => t.id === dwShort))
        ? dwShort
        : (ts.tiles.find((t: TileDefinition) => t.id.includes('plain'))?.id
          || ts.tiles.find((t: TileDefinition) => t.walkable)?.id || ts.tiles[0]?.id || 'floor');
      newGrid = [];
      for (let y = 0; y < newH; y++) {
        const row: string[] = [];
        for (let x = 0; x < newW; x++) {
          row.push(level.grid[y]?.[x] || defaultFloor);
        }
        newGrid.push(row);
      }
    }

    // A resize rebuilds the grid (dirty); pure meta edits keep the form.
    const resized = newW !== level.width || newH !== level.height;
    pushState({ ...level, ...updates, grid: newGrid, terrainDirty: level.terrainDirty || resized });
  };

  // Validate Logic
  const runValidation = () => {
    const passed: string[] = [];
    const errors: string[] = [];
    const warnings: string[] = [];

    // Dimensions
    if (level.width >= 4 && level.width <= 40 && level.height >= 4 && level.height <= 24) {
      passed.push('Schema valid (Dimensions <= 40x24)');
    } else {
      errors.push(`Dimensions ${level.width}x${level.height} out of engine bounds (max 40x24)`);
    }

    // Tileset
    const ts = BUILTIN_TILESETS[level.tileset];
    if (ts) {
      passed.push(`Tiles valid (Tileset '${level.tileset}')`);
    } else {
      errors.push(`Unknown tileset '${level.tileset}'`);
    }

    // Spawn
    if (level.spawn.x >= 0 && level.spawn.x < level.width && level.spawn.y >= 0 && level.spawn.y < level.height) {
      const spawnTile = level.grid[level.spawn.y]?.[level.spawn.x];
      const tDef = ts?.tiles.find((t) => t.id === spawnTile);
      if (tDef && !tDef.walkable) {
        errors.push(`Player spawn at (${level.spawn.x}, ${level.spawn.y}) is blocked by solid tile '${spawnTile}'`);
      } else {
        passed.push('Player spawn valid & walkable');
        passed.push('Collision valid');
      }
    } else {
      errors.push(`Player spawn (${level.spawn.x}, ${level.spawn.y}) is outside map bounds`);
    }

    // Exits
    let exitsOk = true;
    level.exits.forEach((ex, i) => {
      if (ex.x < 0 || ex.x >= level.width || ex.y < 0 || ex.y >= level.height) {
        errors.push(`Exit #${i + 1} position (${ex.x}, ${ex.y}) out of map bounds`);
        exitsOk = false;
      }
    });
    if (exitsOk) passed.push('Exits valid');

    // Exit visibility: an exit keeps its cell art, so a trigger on plain
    // ground is invisible in-game (mirror of collision.py is_portal_art).
    // The validator warns the same; a solid gate is NOT blocked (the
    // trigger fires before walkability).
    const PORTAL_KEYWORDS = ['stair', 'door', 'gate', 'portal', 'exit', 'cave', 'warp', 'ladder'];
    level.exits.forEach((ex, i) => {
      if (ex.x < 0 || ex.x >= level.width || ex.y < 0 || ex.y >= level.height) return;
      const tileId = level.grid[ex.y]?.[ex.x] ?? '';
      const tDef = ts?.tiles.find((t) => t.id === tileId);
      const visible = !!tDef && (tDef.category === 'exit'
        || PORTAL_KEYWORDS.some((k) => tileId.toLowerCase().includes(k)));
      if (!visible) {
        warnings.push(
          `Exit #${i + 1} at (${ex.x},${ex.y}) sits on '${tileId || 'unpainted ground'}', ` +
          `which does not read as a portal — the trigger is invisible in-game. ` +
          `Paint the tileset's exit/stairs tile.`);
      }
    });

    // Edge neighbors (mirror of validate.py: unknown targets fail loud).
    {
      const neighbors = normalizeNeighbors(level.neighbors);
      const knownIds = new Set(levelItems.filter((l) => l.category === 'levels').map((l) => l.id));
      let neighborsOk = true;
      for (const d of ['north', 'south', 'east', 'west'] as const) {
        const t = (neighbors[d] || '').trim();
        if (t && !knownIds.has(t)) {
          errors.push(`Neighbor '${d}' target '${t}' is not a known level`);
          neighborsOk = false;
        }
      }
      if (neighborsOk) passed.push('Neighbors valid');
    }

    // Objects
    let objectsOk = true;
    const seenIds = new Set<string>();
    const seenActorIds = new Map<number, string>();
    level.objects.forEach((obj, i) => {
      const oid = obj.id || `#${i + 1}`;
      if (!obj.id) {
        errors.push(`Object #${i + 1} has empty ID`);
        objectsOk = false;
      } else if (seenIds.has(obj.id)) {
        errors.push(`Duplicate object ID: '${obj.id}'`);
        objectsOk = false;
      } else {
        seenIds.add(obj.id);
      }

      // Property parity with tools/level_compiler/validate.py (the
      // toolchain runs the same rules inside make's levels target, so
      // the browser check must not say green for something Compile ROM
      // rejects).
      const props = (obj.properties || {}) as Record<string, unknown>;
      const otype = obj.type;
      if (!props.entity_id) {
        if (otype === 'enemy') {
          // compile.py hard-errors on an enemy with no entity id
          // (actor_interaction emits COMBAT; validate.py requires the id).
          errors.push(`Enemy object '${oid}' has no entity_id (pick an Enemy Type)`);
          objectsOk = false;
        } else {
          warnings.push(
            `Object '${oid}' has no entity_id: kept verbatim, ignored by compile`);
          (['actor_id', 'hp', 'max_hp', 'battle', 'ai'] as const).forEach((key) => {
            if (key in props) {
              errors.push(`Object '${oid}' has no entity_id but carries actor slot '${key}'`);
              objectsOk = false;
            }
          });
        }
      } else {
        if (entityTypeIds.size > 0 && !entityTypeIds.has(props.entity_id as string)) {
          errors.push(
            `Object '${oid}' entity_id '${props.entity_id}' is not a known entity type ` +
            `— create it with the Entity ID picker's "New entity type"`);
          objectsOk = false;
        }
        const aid = typeof props.actor_id === 'number' && Number.isInteger(props.actor_id)
          ? props.actor_id
          : 0;
        if (aid < 0 || aid > 65535) {
          errors.push(`Object '${oid}' has invalid actor_id '${props.actor_id}' (0..65535)`);
          objectsOk = false;
        } else if (aid > 0) {
          // 0 = unset (toolchain default); only real ids must be unique
          // within the level...
          const owner = seenActorIds.get(aid);
          if (owner) {
            errors.push(`Duplicate actor_id ${aid} on '${owner}' and '${oid}'`);
            objectsOk = false;
          } else {
            seenActorIds.set(aid, oid);
          }
          // ...and across scenes: ActorIds are global (the toolchain's
          // cross-file check), so flag ids already claimed by another
          // level on disk.
          const other = crossActorIds.find((c) => c.id === aid);
          if (other) {
            errors.push(`actor_id ${aid} on '${oid}' also used in scene '${other.level}'`);
            objectsOk = false;
          }
        }
        const facing = (props.facing as string) || 'DOWN';
        if (!['UP', 'DOWN', 'LEFT', 'RIGHT'].includes(facing)) {
          errors.push(`Object '${oid}' has invalid facing '${facing}'`);
          objectsOk = false;
        }
        ((props.flags as string[]) || []).forEach((flag) => {
          if (!['HOSTILE', 'BLOCKING', 'INTERACTABLE'].includes(flag)) {
            errors.push(`Object '${oid}' has unknown flag '${flag}'`);
            objectsOk = false;
          }
        });
        const visual = props.visual as string | undefined;
        if (visual !== undefined && (typeof visual !== 'string' || visual.length !== 1)) {
          errors.push(`Object '${oid}' visual must be a single character`);
          objectsOk = false;
        }
        if (otype === 'enemy') {
          (['hp', 'max_hp', 'ai', 'battle'] as const).forEach((key) => {
            if (!(key in props)) {
              errors.push(`Enemy object '${oid}' is missing properties.${key}`);
              objectsOk = false;
            }
          });
        }
        (['hp', 'max_hp', 'gold_reward'] as const).forEach((key) => {
          if (key in props) {
            const v = props[key] as number;
            if (!Number.isInteger(v) || v < 0) {
              errors.push(`Object '${oid}' has invalid ${key} '${v}'`);
              objectsOk = false;
            }
          }
        });
      }
    });
    if (objectsOk) passed.push('Objects valid');

    // Actor placement on walkable ground (mirror of validate.py): the
    // engine resolves hostiles/static actors only after the walkability
    // check, so an actor on solid art is inert/unreachable.  The editor
    // grid models painted cells exactly; unpainted perimeter walls are
    // the one case it cannot see (validator still catches those).
    level.objects.forEach((obj) => {
      const props = (obj.properties || {}) as Record<string, unknown>;
      if (!props.entity_id) return;
      const flags = ((props.flags as string[]) || []);
      const hostile = obj.type === 'enemy' || flags.includes('HOSTILE');
      const blocking = flags.includes('BLOCKING');
      if (!hostile && !blocking) return;
      const p = obj.position || { x: -1, y: -1 };
      const tileId = level.grid[p.y]?.[p.x];
      const tDef = ts?.tiles.find((t) => t.id === tileId);
      if (tDef && !tDef.walkable) {
        warnings.push(
          `Object '${obj.id}' (${hostile ? 'hostile' : 'blocking'}) at ` +
          `(${p.x},${p.y}) is on non-walkable art: the engine cannot engage ` +
          `or stand on it (move it to walkable ground).`);
      }
    });

    // Engine actor-slot caps (mirrors tools/level_compiler/validate.py):
    // actor_load_banked() spawns hostile rows into
    // World.actors[MAX_WORLD_ACTORS=4] and friendly rows into
    // g_static_actors (MAX_STATIC_ACTORS=10; src/world/actor.h); rows past
    // the cap are silently dropped at runtime.  Enemies count as hostile
    // even when flags are absent (compile.py defaults them HOSTILE).
    const isHostile = (o: any) =>
      o.type === 'enemy' ||
      ((((o.properties || {}).flags as string[]) || []).includes('HOSTILE'));
    const hostileRows = level.objects
      .filter((o) => (o.properties || {}).entity_id && isHostile(o))
      .map((o) => o.id);
    const staticRows = level.objects
      .filter((o) => (o.properties || {}).entity_id && !isHostile(o))
      .map((o) => o.id);
    if (hostileRows.length > 4) {
      errors.push(
        `Level has ${hostileRows.length} hostile actors but the engine spawns at most ` +
        `MAX_WORLD_ACTORS=4; extra would be silently dropped: ${JSON.stringify(hostileRows.slice(4))}`);
      objectsOk = false;
    }
    if (staticRows.length > 10) {
      errors.push(
        `Level has ${staticRows.length} friendly actors but the engine loads at most 10 ` +
        `static rows; extra would be silently dropped: ${JSON.stringify(staticRows.slice(10))}`);
      objectsOk = false;
    }
    if (!objectsOk) passed.pop();

    return { passed, errors, warnings, isValid: errors.length === 0 };
  };

  const validationResult = runValidation();

  // Generate LLM Description
  const generateLLMDescription = () => {
    if (describeFormat === 'json') {
      const jsonDesc = {
        level: level.id,
        name: level.name,
        size: `${level.width}x${level.height}`,
        tileset: level.tileset,
        music: level.music,
        player_start: [level.spawn.x, level.spawn.y],
        connections: level.exits.map(
          (e) => `${e.direction?.toLowerCase() || 'exit'} -> ${e.target_scene} (spawn ${e.target_x},${e.target_y})`
        ),
        neighbors: normalizeNeighbors(level.neighbors),
        regions: level.regions.map((r) => ({
          name: r.id,
          bounds: [r.bounds.x, r.bounds.y, r.bounds.width, r.bounds.height],
          purpose: r.gameplay?.purpose || 'exploration',
          description: r.description || '',
        })),
        objects: level.objects.map((o) => ({
          id: o.id,
          type: o.type,
          position: [o.position.x, o.position.y],
          properties: o.properties,
        })),
      };
      return JSON.stringify(jsonDesc, null, 2);
    }

    const lines: string[] = [];
    lines.push(`# ${level.name.toUpperCase()} (\`${level.id}\`)`);
    lines.push(`\n**Size:** ${level.width}×${level.height} tiles | **Tileset:** \`${level.tileset}\` | **Music:** \`${level.music}\``);
    lines.push(`**Player Start:** \`(${level.spawn.x}, ${level.spawn.y})\` facing ${level.spawn.facing || 'DOWN'}`);

    lines.push('\n## Connections');
    if (level.exits.length > 0) {
      level.exits.forEach((e) => {
        lines.push(`- ${e.direction || 'Exit'} (at ${e.x},${e.y}) → **${e.target_scene}** (spawn at ${e.target_x},${e.target_y})`);
      });
    } else {
      lines.push('- None');
    }
    lines.push('\n## Edge neighbors');
    {
      const neighbors = normalizeNeighbors(level.neighbors);
      const linked = (['north', 'south', 'east', 'west'] as const)
        .filter((d) => (neighbors[d] || '').trim());
      if (linked.length > 0) {
        linked.forEach((d) => {
          lines.push(`- ${d} edge → **${neighbors[d]}** (mirrored entry)`);
        });
      } else {
        lines.push('- None');
      }
    }

    lines.push('\n## Regions & Semantic Context');
    if (level.regions.length > 0) {
      level.regions.forEach((r) => {
        lines.push(`### ${r.id.replace(/_/g, ' ').toUpperCase()}`);
        lines.push(`- **Bounds:** [${r.bounds.x}, ${r.bounds.y}] to [${r.bounds.x + r.bounds.width}, ${r.bounds.y + r.bounds.height}]`);
        lines.push(`- **Purpose:** \`${r.gameplay?.purpose || 'exploration'}\` (Difficulty: ${r.gameplay?.difficulty || 1})`);
        if (r.description) lines.push(`- **Description:** ${r.description}`);
      });
    } else {
      lines.push('- No explicit regions defined.');
    }

    lines.push('\n## Important Objects & Entities');
    if (level.objects.length > 0) {
      level.objects.forEach((o) => {
        lines.push(`- **[${o.type.toUpperCase()}]** \`${o.id}\` at (${o.position.x}, ${o.position.y})`);
      });
    } else {
      lines.push('- None');
    }

    return lines.join('\n');
  };

  return (
    <div className="app-container">
      {/* Top Header */}
      <header className="app-header">
        <div className="header-title">
          <span className="brand-icon">🎮</span>
          <span>Game Boy RPG Level Editor</span>
        </div>
        <div className="header-levels">
          <label htmlFor="level-select-dropdown" className="header-levels-label">
            Level:
          </label>
          <FilterCombo
            items={[
              ...levelItems
                .filter((l) => l.category === 'levels')
                .map((lvl) => ({
                  value: lvl.id,
                  label: `${lvl.name} (${lvl.id}.json)`,
                  group: 'Overworld Levels',
                })),
              ...levelItems
                .filter((l) => l.category === 'screens')
                .map((lvl) => ({
                  value: lvl.id,
                  label: `${lvl.name} (screens/${lvl.id}.json)`,
                  group: 'Screens',
                })),
              ...enemyItems.map((e) => ({
                value: `enemy:${e.id}`,
                label: `${e.name} (enemy type)`,
                group: 'Enemies',
              })),
              { value: 'hero', label: 'Hero (art + stats + starter deck)', group: 'Hero' },
              { value: 'cards', label: 'Battle (HUD + layout + cards)', group: 'Battle' },
              { value: 'dialogues', label: 'Dialogues (speaker + lines)', group: 'Dialogue' },
              { value: 'shops', label: 'Shops (stock + merchant)', group: 'Shops' },
              { value: 'palettes', label: 'Palettes (preview + assign)', group: 'Palettes' },
              ...(!levelItems.some((l) => l.id === currentLevelId)
                ? [{
                    value: currentLevelId,
                    label: `${level.name || currentLevelId} (${currentLevelId}.json)`,
                    group: 'Current Level',
                  }]
                : []),
              { value: '__new__', label: '➕ + New Level...', group: 'Actions' },
            ]}
            value={heroView ? 'hero' : cardView ? 'cards' : shopView ? 'shops' : paletteView ? 'palettes' : dialogueView !== null ? 'dialogues' : enemyView ? `enemy:${enemyView}` : currentLevelId}
            onPick={(v) => handleSelectLevel(v)}
            staleLabel={(v) => `${v} (unknown — pick below)`}
            placeholder="Filter levels..."
            className="header-level-combo"
          />
        </div>
      </header>

      {/* Main App Body */}
      <div className="app-body">
        <Toolbar
          activeTool={activeTool}
          onSelectTool={setActiveTool}
          canUndo={history.length > 0}
          canRedo={redoStack.length > 0}
          onUndo={handleUndo}
          onRedo={handleRedo}
          zoom={zoom}
          onZoomChange={setZoom}
          showGrid={showGrid}
          onToggleGrid={() => setShowGrid(!showGrid)}
          showCollision={showCollision}
          onToggleCollision={() => setShowCollision(!showCollision)}
          onSave={handleSaveToServer}
          onDownload={() => downloadLevelJson(level)}
          onCompileRom={handleCompileRom}
          onRunGame={handleRunGame}
          isCompiling={isCompiling}
          isRunning={isRunning}
          onLoad={async () => {
            try {
              const res = await promptLoadLevelFile();
              pushState(res.level);
              setCurrentLevelId(res.level.id);
            } catch (err) {
              alert(`Failed to load level: ${err}`);
            }
          }}
          onValidate={() => { refreshCrossActorIds(); setShowValidateModal(true); }}
          onDescribe={() => setShowDescribeModal(true)}
          onSoundTest={() => setShowSoundTestModal(true)}
          onNew={handleCreateNewLevel}
          clonePattern={clonePattern}
          onClearClone={() => {
            setClonePattern(null);
            setNotification({ message: 'Clone buffer cleared. Drag a box to copy tiles.', type: 'info' });
          }}
          isTilesetReviewerOpen={showTilesetReviewer}
          onToggleTilesetReviewer={() => setShowTilesetReviewer((prev) => !prev)}
          isCombatArtOpen={showCombatArt}
          onToggleCombatArt={() => setShowCombatArt((prev) => !prev)}
        />

        {/* Notification Toast */}
        {notification && (
          <div
            style={{
              padding: '8px 16px',
              backgroundColor: notification.type === 'success' ? '#27ae60' : notification.type === 'error' ? '#c0392b' : '#2980b9',
              color: '#ffffff',
              fontSize: '14px',
              fontWeight: 'bold',
              display: 'flex',
              justifyContent: 'space-between',
              alignItems: 'center',
              boxShadow: '0 2px 4px rgba(0,0,0,0.3)',
              zIndex: 100
            }}
          >
            <span>{notification.message}</span>
            <button
              onClick={() => setNotification(null)}
              style={{
                background: 'transparent',
                border: 'none',
                color: '#fff',
                cursor: 'pointer',
                fontSize: '16px',
                marginLeft: '12px'
              }}
            >
              ✕
            </button>
          </div>
        )}

        <div className="main-content">
          {heroView ? (
            <HeroManager
              key="hero"
              onOpenComposer={() => setShowCombatArt(true)}
            />
          ) : enemyView ? (
            <EnemyManager
              key={enemyView}
              initialId={enemyView}
              onOpenComposer={() => setShowCombatArt(true)}
            />
          ) : cardView ? (
            <BattleManager key="cards" />
          ) : shopView ? (
            <ShopManager key="shops" />
          ) : paletteView ? (
            <PaletteManager key="palettes" />
          ) : dialogueView !== null ? (
            <DialogueManager key="dialogues" initialId={dialogueView || undefined} />
          ) : (
          <>
          {/* Left Sidebar */}
          <aside className="sidebar-left">
            <TilesetPalette
              tilesetId={level.tileset}
              selectedTileId={selectedTileId}
              // Switching tilesets re-prefixes every tile on save, so the
              // stored block form (pinned to the old tileset) is stale.
              onSelectTileset={(ts) => pushState({ ...level, tileset: ts, terrainDirty: true })}
              onSelectTile={setSelectedTileId}
            />
          </aside>

          {/* Canvas Work Area */}
          <main className="center-stage">
            <MapCanvas
              level={level}
              activeTool={activeTool}
              activeLayer={activeLayer}
              selectedTileId={selectedTileId}
              zoom={zoom}
              showGrid={showGrid}
              showCollision={showCollision}
              showTerrain={showTerrain}
              showExits={showExits}
              showObjects={showObjects}
              showRegions={showRegions}
              selectedEntityIndex={selectedEntityIndex}
              onSelectEntityIndex={setSelectedEntityIndex}
              onTilePainted={handleTilePainted}
              onRectPainted={handleRectPainted}
              onFill={handleFill}
              onTilePicked={(picked) => {
                setSelectedTileId(picked);
                setActiveTool('brush');
              }}
              onSpawnMoved={handleSpawnMoved}
              onObjectMoved={handleObjectMoved}
              onExitMoved={handleExitMoved}
              onAddObjectAt={(x, y) => {
                handleAddObject({
                  id: `obj_${Date.now().toString().slice(-4)}`,
                  type: 'npc',
                  position: { x, y },
                  properties: { display_name: 'NPC' },
                });
              }}
              onAddExitAt={(x, y) => {
                handleAddExit({
                  x,
                  y,
                  target_scene: 'field',
                  target_x: 2,
                  target_y: 7,
                  direction: 'SOUTH',
                  tile_char: '<',
                });
              }}
              clonePattern={clonePattern}
              onClonePatternCaptured={handleClonePatternCaptured}
              onStampPattern={handleStampPattern}
            />
          </main>

          {/* Right Inspector */}
          <aside className="sidebar-right">
            <Inspector
              level={level}
              activeLayer={activeLayer}
              onSelectLayer={setActiveLayer}
              showTerrain={showTerrain}
              onToggleShowTerrain={() => setShowTerrain(!showTerrain)}
              showExits={showExits}
              onToggleShowExits={() => setShowExits(!showExits)}
              showObjects={showObjects}
              onToggleShowObjects={() => setShowObjects(!showObjects)}
              showRegions={showRegions}
              onToggleShowRegions={() => setShowRegions(!showRegions)}
              selectedEntityIndex={selectedEntityIndex}
              onSelectEntityIndex={setSelectedEntityIndex}
              onUpdateLevelMeta={handleUpdateLevelMeta}
              onUpdateSpawn={(spawn) => pushState({ ...level, spawn })}
              onAddExit={handleAddExit}
              onUpdateExit={handleUpdateExit}
              onDeleteExit={handleDeleteExit}
              onUpdateNeighbors={handleUpdateNeighbors}
              onAddObject={handleAddObject}
              onUpdateObject={handleUpdateObject}
              onDeleteObject={handleDeleteObject}
              onDuplicateObject={handleDuplicateSelectedEntity}
              onAddRegion={handleAddRegion}
              onUpdateRegion={handleUpdateRegion}
              onDeleteRegion={handleDeleteRegion}
              sceneOptions={levelItems
                .filter((l) => l.category === 'levels')
                .map(({ id, name, scene_id }) => ({ id, name, scene_id: scene_id ?? null }))}
              sceneId={(levelItems.find((l) => l.id === currentLevelId)?.scene_id) ?? null}
              onDeleteLevel={handleDeleteLevel}
              onCleanRetiredOrphans={handleCleanRetiredOrphans}
            />
          </aside>
          </>
          )}
        </div>
      </div>

      {/* Validation Modal */}
      {showValidateModal && (
        <div className="modal-overlay" onClick={() => setShowValidateModal(false)}>
          <div className="modal-card" onClick={(e) => e.stopPropagation()}>
            <div className="modal-header">
              <h3>Level Validation Results</h3>
              <button className="modal-close-btn" onClick={() => setShowValidateModal(false)}>
                ✕
              </button>
            </div>
            <div className="modal-body">
              <div style={{ marginBottom: '12px', fontSize: '13px', opacity: 0.85 }}>
                Browser checks mirror the toolchain's object rules (enemy
                hp/max_hp/battle/ai, actor-id range, within-level and
                cross-scene uniqueness — cross-scene ids are read live from
                <code> levels/*.json</code>).  The toolchain still runs the full
                <code>validate.py</code> (per-tile rules) server-side at Compile ROM time.
              </div>
              <div className="validation-list">
                {validationResult.passed.map((p, i) => (
                  <div key={i} className="val-item passed">
                    ✓ {p}
                  </div>
                ))}
                {validationResult.warnings.map((w, i) => (
                  <div key={i} className="val-item warning">
                    ⚠️ {w}
                  </div>
                ))}
                {validationResult.errors.map((e, i) => (
                  <div key={i} className="val-item failed">
                    ✗ {e}
                  </div>
                ))}
              </div>
              <div style={{ marginTop: '16px', fontWeight: 'bold', color: validationResult.isValid ? '#56d364' : '#f85149' }}>
                {validationResult.isValid ? '🎉 LEVEL VALID' : '❌ LEVEL INVALID (Fix errors above)'}
              </div>
            </div>
            <div className="modal-footer">
              <button className="btn btn-primary" onClick={() => setShowValidateModal(false)}>
                Close
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Sound Test Modal */}
      {showSoundTestModal && (
        <SfxTesterModal onClose={() => setShowSoundTestModal(false)} />
      )}

      {/* LLM Description Modal */}
      {showDescribeModal && (
        <div className="modal-overlay" onClick={() => setShowDescribeModal(false)}>
          <div className="modal-card" onClick={(e) => e.stopPropagation()}>
            <div className="modal-header">
              <h3>LLM Semantic Representation</h3>
              <div style={{ display: 'flex', gap: '8px', alignItems: 'center' }}>
                <button
                  className={`btn btn-sm ${describeFormat === 'markdown' ? 'btn-primary' : ''}`}
                  onClick={() => setDescribeFormat('markdown')}
                >
                  Markdown
                </button>
                <button
                  className={`btn btn-sm ${describeFormat === 'json' ? 'btn-primary' : ''}`}
                  onClick={() => setDescribeFormat('json')}
                >
                  JSON
                </button>
                <button className="modal-close-btn" onClick={() => setShowDescribeModal(false)}>
                  ✕
                </button>
              </div>
            </div>
            <div className="modal-body">
              <pre className="code-block">{generateLLMDescription()}</pre>
            </div>
            <div className="modal-footer">
              <button
                className="btn"
                onClick={() => {
                  navigator.clipboard.writeText(generateLLMDescription());
                  alert('Copied to clipboard!');
                }}
              >
                📋 Copy
              </button>
              <button className="btn btn-primary" onClick={() => setShowDescribeModal(false)}>
                Close
              </button>
            </div>
          </div>
        </div>
      )}

      {/* Tileset & Asset Property Reviewer Modal */}
      {showTilesetReviewer && (
        <TilesetReviewer
          initialTilesetId={level.tileset}
          onClose={() => setShowTilesetReviewer(false)}
          onTilesetUpdated={(updatedTs) => {
            // Trigger state change so canvas immediately applies new walkability/properties
            pushState({ ...level });
            setNotification({
              message: `Tileset "${updatedTs.label}" updated! Canvas & collision updated.`,
              type: 'success',
            });
          }}
        />
      )}
      {showCombatArt && (
        <CombatArtStudio onClose={() => setShowCombatArt(false)} />
      )}
    </div>
  );
};
