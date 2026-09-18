"""Compose assets/npc_tiles.png (village NPC map-art mini sheet).

The shared actors tileset (assets/actor-sprites.png) now owns the NPC
art; the village sheet's NPC cells were blanked when the art moved
(assets/tilesets.md "Actors tileset").  The ROM's village VRAM block is
compiled from village-tile.png, so ui_load_tileset_banked() overlays
these tiles into the blanked slots after the sheet copy (see
src/game/tiles_content.c, g_actor_npc_tiles).

Layout order = g_actor_npc_tiles order = the npc_slots[] table in
tiles_content.c.  Deterministic: rerunning reproduces the sheet
byte-identically.  The chroma-key yellow (241,235,3) -- the actor
sheet's transparent-background convention (actor-tileset-description
CSV) -- is the composite background; the Makefile png2gb rule anchors
it to shade 0, matching how the old village sheet encoded art colors.
"""
from PIL import Image

PUB = 'tools/level_editor/public/tiles/actors'
BG = (241, 235, 3)

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
    # RGB sheet, byte-exact curated colors; transparency -> chroma-key
    # yellow (the actor sheet's transparent-background convention). No
    # quantization: off-ramp pixels are reported, not hidden.
    sheet = Image.new('RGB', (len(LAYOUT) * 8, 8), BG)
    for x, name in enumerate(LAYOUT):
        im = Image.open('%s/%s.png' % (PUB, name)).convert('RGBA')
        im = Image.alpha_composite(Image.new('RGBA', im.size, BG + (255,)), im)
        sheet.paste(im.convert('RGB'), (x * 8, 0))
    sheet.save('assets/npc_tiles.png')
    print('wrote assets/npc_tiles.png')


if __name__ == '__main__':
    main()
