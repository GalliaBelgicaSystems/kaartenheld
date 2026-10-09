import { EditorLevel, editorToLevelData } from '../model/Level';

export function serializeLevelJson(level: EditorLevel): string {
  const data = editorToLevelData(level);
  return JSON.stringify(data, null, 2);
}

export async function saveLevelToServer(
  level: EditorLevel,
  previousId?: string | null
): Promise<{ success: boolean; path?: string; scene_id?: number; error?: string;
             tunnels_synced?: number; tunnels_removed?: string[]; tunnel_error?: string | null }> {
  try {
    const data = editorToLevelData(level);
    const category = level.isScreen ? 'screens' : 'levels';
    const res = await fetch('/api/save-level', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        id: level.id,
        category,
        data,
        // The id the level was loaded under: lets the server distinguish
        // edit from rename (rename preserves the numeric scene id and
        // rewires exits; see vite.config.ts saveRealLevel).
        previousId: previousId ?? null,
      })
    });
    if (!res.ok) {
      throw new Error(`Server returned status ${res.status}`);
    }
    return await res.json();
  } catch (err: any) {
    return { success: false, error: err.message };
  }
}

/** Delete a real level: retires its scene id (never reused), clears exits
 *  that targeted it, removes the file.  Returns how many exits were
 *  cleared.  Engine-wired scenes are refused by the server. */
export async function deleteLevel(id: string): Promise<{ success: boolean; cleared?: number; error?: string }> {
  try {
    const res = await fetch('/api/delete-level', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id }),
    });
    const body = await res.json();
    if (!res.ok || !body.success) throw new Error(body.error || `status ${res.status}`);
    return body;
  } catch (err: any) {
    return { success: false, error: err.message };
  }
}

/** Remove every levels/<id>.json whose id is retired (tombstoned).  A
 *  leftover retired file hard-fails the next compile and keeps dead actor
 *  ids reserved.  Returns the removed level ids. */
export async function cleanRetiredOrphans(): Promise<{ success: boolean; removed?: string[]; error?: string }> {
  try {
    const res = await fetch('/api/clean-retired-orphans', { method: 'POST' });
    const body = await res.json();
    if (!res.ok || !body.success) throw new Error(body.error || `status ${res.status}`);
    return body;
  } catch (err: any) {
    return { success: false, error: err.message };
  }
}

export async function fetchUsedActorIds(exclude?: string): Promise<Array<{ id: number; level: string }>> {
  const q = exclude ? `?exclude=${encodeURIComponent(exclude)}` : '';
  const res = await fetch(`/api/actor-ids${q}`);
  const data = await res.json();
  if (!res.ok || !data.success) {
    throw new Error(data.error || `actor-id fetch failed with status ${res.status}`);
  }
  return data.used || [];
}

export async function compileRom(): Promise<{ success: boolean; log?: string; romPath?: string[]; error?: string }> {
  try {
    const res = await fetch('/api/compile-rom', { method: 'POST' });
    const data = await res.json();
    if (!res.ok || !data.success) {
      return {
        success: false,
        error: data.error || `Compile failed with status ${res.status}`,
        log: data.log
      };
    }
    return data;
  } catch (err: any) {
    return { success: false, error: err.message };
  }
}

export async function runGame(): Promise<{ success: boolean; emulator?: string; message?: string; stale?: string[]; error?: string }> {
  try {
    const res = await fetch('/api/run-game', { method: 'POST' });
    const data = await res.json();
    if (!res.ok || !data.success) {
      throw new Error(data.error || `Run failed with status ${res.status}`);
    }
    return data;
  } catch (err: any) {
    return { success: false, error: err.message };
  }
}

export function downloadLevelJson(level: EditorLevel): void {
  const jsonStr = serializeLevelJson(level);
  const blob = new Blob([jsonStr], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `${level.id || 'level'}.json`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}
