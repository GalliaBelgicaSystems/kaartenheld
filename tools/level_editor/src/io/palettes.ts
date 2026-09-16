/** Palette preview/assignment disk I/O via the vite dev API. */

export interface Ramp {
  index: number;
  name: string;
  colors: string[];
}

export interface PaletteTile {
  id: string;
  label: string;
  image_url: string | null;
  /** Ramp name (SLOTS tables in assets/palette.txt), never an index. */
  palette: string;
}

export interface PaletteEnemy {
  id: string;
  label: string;
  image_url: string;
  /** OBJ ramp name. */
  palette: string;
}

export interface PaletteData {
  tileset: string;
  bg: Ramp[];
  obj: Ramp[];
  tiles: PaletteTile[];
  enemies: PaletteEnemy[];
  hero: { palette: string };
}

export async function fetchPalettes(tileset: string): Promise<PaletteData> {
  const res = await fetch(`/api/palettes?tileset=${encodeURIComponent(tileset)}`);
  if (!res.ok) throw new Error(`palettes returned ${res.status}`);
  const body = await res.json();
  if (!body.success) throw new Error(body.error || 'palettes failed');
  return body;
}

export async function assignPalette(
  kind: 'tile' | 'enemy' | 'hero',
  id: string,
  palette: string,
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

export const TILESETS = ['forest', 'castle', 'desolate_landscape', 'village'];
