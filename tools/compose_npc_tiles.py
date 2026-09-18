"""Compose assets/npc_tiles.png (village NPC map-art mini sheet).

The shared actors tileset (assets/sprites.png) now owns the NPC
art; the village sheet's NPC cells were blanked when the art moved
(assets/tilesets.md "Actors tileset").  The ROM's village VRAM block is
compiled from village-tile.png, so ui_load_tileset_banked() overlays
these tiles into the blanked slots after the sheet copy (see
src/game/tiles_content.c, g_actor_npc_tiles).

Layout order = g_actor_npc_tiles order = the npc_slots[] table in
tiles_content.c.  Deterministic: rerunning reproduces the sheet
byte-identically.

Exactness: curated art is copied byte-identically (RGBA transparency is
flattened onto TOWN/ground tan -- entry 0 of the town overlay ramps).
NO quantization, NO color snapping: any pixel outside the overlay
ramp fails loudly later at sidecar validation
(tools/emit_sheet_sidecars.py), naming the cell and the offending
color. Repaint the pixel; do not adjust the tool.
"""
from pathlib import Path
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
PUB = REPO_ROOT / 'tools' / 'level_editor' / 'public' / 'tiles' / 'actors'

# TOWN/ground: entry 0 of the village overlay ramps (town1/town2).
BG = (182, 162, 126)

# Order matters: mirrors tiles_content.c g_actor_npc_tiles / npc_slots.
LAYOUT = [
    'actors_guard',
    'actors_wizard',
    'actors_merchant',
    'actors_mayor',
    'actors_dog_frame_1',
    'actors_dog_frame_2',
]


def main():
    sheet = Image.new('RGB', (len(LAYOUT) * 8, 8), BG)
    for x, name in enumerate(LAYOUT):
        im = Image.open(PUB / ('%s.png' % name)).convert('RGBA')
        if im.size != (8, 8):
            raise ValueError(
                'npc sheet source %s.png is %dx%d, want 8x8 -- '
                'resampling would invent colors' % (name, im.size[0], im.size[1]))
        im = Image.alpha_composite(Image.new('RGBA', im.size, BG + (255,)), im)
        sheet.paste(im.convert('RGB'), (x * 8, 0))
    sheet.save(REPO_ROOT / 'assets' / 'npc_tiles.png')
    print('wrote assets/npc_tiles.png')


if __name__ == '__main__':
    main()
