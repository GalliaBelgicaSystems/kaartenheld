/** Whole-edge neighbor pairing check via the vite dev API.
 *
 *  The engine drops the player one cell inside the target's opposite edge,
 *  so a crossing must always land on walkable ground or the player is stuck
 *  in a wall.  The pairing logic lives once in tools/level_compiler/
 *  collision.py; this call runs it through validate.py --neighbor-check for
 *  the editor's current (possibly unsaved) level.
 */

import { EditorLevel, editorToLevelData } from '../model/Level';

export interface NeighborLinkStatus {
  direction: string | null;
  target: string | null;
  ok: boolean;
  errors: string[];
  warnings: string[];
}

export async function fetchNeighborCheck(level: EditorLevel): Promise<NeighborLinkStatus[]> {
  const res = await fetch('/api/neighbor-check', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ from_id: level.id, data: editorToLevelData(level) }),
  });
  if (!res.ok) throw new Error(`neighbor check returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'neighbor check failed');
  return body.links || [];
}
