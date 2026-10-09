/** Entity-type disk I/O via the vite dev API.
 *
 *  Entity types live in screens/enemy_types/ (battle / shared-overworld-art
 *  types) and screens/entity_types/ (plain NPC / quest / pickup types).
 *  The union of their ids is the single source of truth for the ENTITY_ID_*
 *  game range; tools/screen_compiler/entity_compile.py generates the C
 *  header from the same files. */

export interface EntityTypeListItem {
  id: string;
  label: string;
  kind: 'enemy' | 'entity';
  entity_id: string;
}

export interface EntityTypeDraft {
  id: string;
  label?: string;
  kind?: 'npc' | 'item';
  name?: string;
  visual?: string;
  sprite_kind?: 'tile' | 'ascii' | 'chest' | 'boss';
  [k: string]: unknown;
}

const DIR_FOR_KIND: Record<string, string> = {
  enemy: 'enemy_types',
  entity: 'entity_types',
};

export async function fetchEntityTypeList(): Promise<EntityTypeListItem[]> {
  const res = await fetch('/api/entity-types');
  if (!res.ok) throw new Error(`entity types returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'entity types failed');
  return body.items || [];
}

export async function fetchEntityType(id: string, dir: string): Promise<EntityTypeDraft> {
  const res = await fetch(`/api/entity-type?id=${encodeURIComponent(id)}&dir=${encodeURIComponent(dir)}`);
  if (!res.ok) throw new Error(`entity type ${id} returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || `entity type ${id} failed`);
  return body.data;
}

export async function saveEntityType(
  dir: 'enemy_types' | 'entity_types',
  data: EntityTypeDraft,
): Promise<void> {
  const res = await fetch('/api/save-entity-type', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: data.id, dir, data }),
  });
  if (!res.ok) throw new Error(`save entity type returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save entity type failed');
}

export async function deleteEntityType(id: string, dir: string): Promise<void> {
  const res = await fetch('/api/delete-entity-type', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id, dir }),
  });
  if (!res.ok) throw new Error(`delete entity type returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'delete entity type failed');
}

export function dirForKind(kind: string): string {
  return DIR_FOR_KIND[kind] || 'entity_types';
}
