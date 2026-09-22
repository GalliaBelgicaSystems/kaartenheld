/** Palette preview/assignment disk I/O via the vite dev API. */

export interface Ramp {
  index: number;
  name: string;
  colors: string[];
  /** Hardware-reserved slot (world tilesets: slot 4 = UI_COLOR_PAPER,
   *  reprogrammed by dialogue boxes). Assignment is refused. */
  reserved?: boolean;
  reservedReason?: string;
}

export interface PaletteTile {
  id: string;
  label: string;
  image_url: string | null;
  palette: number;
}

export interface PaletteEnemy {
  id: string;
  label: string;
  /** First overworld cell preview (cell PNG, or the actors portrait for
   *  npc_* types); null when no preview file exists. */
  image_url: string | null;
  cells: string[];
  /** Declared ramp name (null when stored as a bare OBJ slot int). */
  ramp: string | null;
  palette: number;
}

export interface PaletteData {
  tileset: string;
  bg: Ramp[];
  obj: Ramp[];
  tiles: PaletteTile[];
  enemies: PaletteEnemy[];
  hero: { palette: number; ramp: string | null; image_url: string | null };
}

export async function fetchPalettes(tileset: string): Promise<PaletteData> {
  // no-store: palette saves rewrite the manifests; a heuristically cached
  // copy would show pre-save ramps after returning to the level view.
  const res = await fetch(`/api/palettes?tileset=${encodeURIComponent(tileset)}`, { cache: 'no-store' });
  if (!res.ok) throw new Error(`palettes returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'palettes failed');
  return body;
}

export async function assignPalette(
  kind: 'tile' | 'enemy' | 'hero',
  id: string,
  palette: number,
  tileset?: string,
): Promise<void> {
  const res = await fetch('/api/assign-palette', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ kind, id, palette, tileset }),
  });
  if (!res.ok) throw new Error(`assign returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'assign failed');
}

export const TILESETS = ['forest', 'castle', 'desolate_landscape', 'village', 'sprites'];

export interface PaletteFreshness {
  fresh: boolean;
  state: 'fresh' | 'stale' | 'missing';
  missing?: string[];
}

/** Are the manifests the preview renders from newer than every source
 *  that feeds palette_compiler.py? Hand edits outside the editor show up
 *  here; our own saves refresh server-side. */
export async function fetchPaletteFreshness(): Promise<PaletteFreshness> {
  const res = await fetch('/api/palette-freshness', { cache: 'no-store' });
  if (!res.ok) throw new Error(`freshness returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'freshness failed');
  return { fresh: !!body.fresh, state: body.state || 'fresh', missing: body.missing };
}

export interface PaletteColorEdit {
  section: string;
  name: string;
  hex: string;
}

export interface PaletteRepoint {
  ramp: string;
  position: number;
  ref: string;
}

/** Author a ramp: rewrite color definitions and/or ramp-row refs in
 *  assets/palette.txt (line-preserving), then refresh the full manifest
 *  (export, shades, mismatch report). Writes happen only on explicit
 *  client Save, never per color-picker drag tick. */
export async function savePalette(
  colorEdits: PaletteColorEdit[],
  rampRepoints: PaletteRepoint[],
): Promise<{ log: string }> {
  const res = await fetch('/api/save-palette', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ colorEdits, rampRepoints }),
  });
  if (!res.ok) throw new Error(`save-palette returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'save-palette failed');
  return { log: body.log || '' };
}
