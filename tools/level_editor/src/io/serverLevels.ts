import { EditorLevel, levelDataToEditor } from '../model/Level';
import { BUILTIN_TILESETS, parseTilesetJson } from '../model/Tileset';

export interface ServerLevelItem {
  id: string;
  name: string;
  category: 'levels' | 'screens';
  /** Assigned scene id, or null when the level has never been saved
   * through the editor (unregistered — saving assigns one). */
  scene_id?: number | null;
}

export interface ServerTilesetItem {
  id: string;
  label: string;
}

/** Disk-backed level/tileset reads via the vite dev API (no editor rebuild
 *  after editing JSON by hand).  All functions throw on transport failure so
 *  callers can fall back to the bundled static imports (built bundles served
 *  without the dev API). */
export async function fetchLevelList(): Promise<ServerLevelItem[]> {
  const res = await fetch('/api/levels');
  if (!res.ok) throw new Error(`level list returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'level list failed');
  const levels = (body.levels || []).map((l: any) => ({ ...l, category: 'levels' as const }));
  const screens = (body.screens || []).map((l: any) => ({ ...l, category: 'screens' as const }));
  return [...levels, ...screens];
}

export async function fetchLevelData(category: string, id: string): Promise<any> {
  const res = await fetch(`/api/level?category=${encodeURIComponent(category)}&id=${encodeURIComponent(id)}`);
  if (!res.ok) throw new Error(`level ${id} returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || `level ${id} failed`);
  return body.data;
}

export async function fetchLevelAsEditor(category: string, id: string): Promise<EditorLevel> {
  return levelDataToEditor(await fetchLevelData(category, id));
}

export async function fetchTilesetList(): Promise<ServerTilesetItem[]> {
  const res = await fetch('/api/tilesets');
  if (!res.ok) throw new Error(`tileset list returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'tileset list failed');
  return body.tilesets || [];
}

/** Refresh the in-memory tileset registry from disk (drops nothing: unknown
 *  ids are added, known ids are replaced).  Returns the ids that changed. */
export async function refreshTilesetsFromServer(): Promise<string[]> {
  const list = await fetchTilesetList();
  const changed: string[] = [];
  for (const item of list) {
    const res = await fetch(`/api/tileset?id=${encodeURIComponent(item.id)}`);
    if (!res.ok) continue;
    const body = await res.json();
    if (!body.success || !body.data) continue;
    BUILTIN_TILESETS[item.id] = parseTilesetJson(body.data);
    changed.push(item.id);
  }
  return changed;
}
