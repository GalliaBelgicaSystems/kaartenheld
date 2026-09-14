import React, { useRef, useEffect, useState, useCallback, useMemo } from 'react';
import { EditorLevel, LevelExit, LevelRegion } from './model/Level';
import { LevelObject, OBJECT_TEMPLATES } from './model/Objects';
import { BUILTIN_TILESETS, TileDefinition, TilesetDefinition } from './model/Tileset';
import { SHEET_TILE_NAMES, COMBAT_TILE_URL, fetchCombatArtList, fetchCombatArtSet, fetchEnemyTypeList, fetchEnemyType } from './io/combatArt';
import { CardSkin, CARD_COLOR_HEX, fetchCardSkin } from './io/cardSkin';
import { BattleHud, fetchBattleHud } from './io/battleHud';
import { ToolType } from './Toolbar';
import { EditLayer } from './LayerPanel';

interface MapCanvasProps {
  level: EditorLevel;
  activeTool: ToolType;
  activeLayer: EditLayer;
  selectedTileId: string;
  zoom: number;
  showGrid: boolean;
  showCollision: boolean;
  showTerrain: boolean;
  showExits: boolean;
  showObjects: boolean;
  showRegions: boolean;
  selectedEntityIndex: number | null;
  onSelectEntityIndex: (index: number | null) => void;
  onTilePainted: (x: number, y: number, tileId: string) => void;
  onRectPainted: (x: number, y: number, w: number, h: number, tileId: string) => void;
  onFill: (startX: number, startY: number, newTileId: string) => void;
  onTilePicked: (tileId: string) => void;
  onSpawnMoved: (x: number, y: number) => void;
  onObjectMoved: (index: number, x: number, y: number) => void;
  onExitMoved: (index: number, x: number, y: number) => void;
  onAddObjectAt: (x: number, y: number) => void;
  onAddExitAt: (x: number, y: number) => void;
  clonePattern?: string[][] | null;
  onClonePatternCaptured?: (pattern: string[][]) => void;
  onStampPattern?: (startX: number, startY: number, pattern: string[][]) => void;
}

const TILE_SIZE = 24; // Base pixel size per tile

/* Draw ROM-accurate title text with the intrepid font tiles.  The ROM maps
 * char `ch` to tile `ch - ' '` (ui_font_tile_base = 0), so the 96 tile PNGs
 * (16 cols x 6 rows) index directly by charCode - 32.  Out-of-range cells
 * are clipped to the 20-column grid. */
function drawTitleText(
  ctx: CanvasRenderingContext2D,
  fontImgs: Map<number, HTMLImageElement>,
  x: number,
  y: number,
  text: string,
  tileSize: number
) {
  if (fontImgs.size === 0) return;
  for (let i = 0; i < text.length; i++) {
    const col = x + i;
    if (col >= 20) break;
    if (col < 0) continue;
    const code = text.charCodeAt(i);
    if (code < 32 || code > 127) continue;
    const img = fontImgs.get(code - 32);
    if (img) ctx.drawImage(img, col * tileSize, y * tileSize, tileSize, tileSize);
  }
}


export const MapCanvas: React.FC<MapCanvasProps> = ({
  level,
  activeTool,
  activeLayer,
  selectedTileId,
  zoom,
  showGrid,
  showCollision,
  showTerrain,
  showExits,
  showObjects,
  showRegions,
  selectedEntityIndex,
  onSelectEntityIndex,
  onTilePainted,
  onRectPainted,
  onFill,
  onTilePicked,
  onSpawnMoved,
  onObjectMoved,
  onExitMoved,
  onAddObjectAt,
  onAddExitAt,
  clonePattern,
  onClonePatternCaptured,
  onStampPattern,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const [isMouseDown, setIsMouseDown] = useState(false);
  const [dragStartTile, setDragStartTile] = useState<{ x: number; y: number } | null>(null);
  const [hoverTile, setHoverTile] = useState<{ x: number; y: number } | null>(null);
  const [draggingEntity, setDraggingEntity] = useState<{ type: 'object' | 'exit' | 'spawn'; index: number } | null>(null);
  const [isCapturingClone, setIsCapturingClone] = useState(false);

  const tileset: TilesetDefinition = BUILTIN_TILESETS[level.tileset] || BUILTIN_TILESETS.forest;
  if (!BUILTIN_TILESETS[level.tileset]) {
    console.warn(`[MapCanvas] unknown tileset '${level.tileset}', falling back to forest`);
  }
  const defaultFloorTile = tileset.tiles.find((t) => t.walkable)?.id || tileset.tiles[0]?.id || 'floor';
  const tileMap = new Map<string, TileDefinition>();
  tileset.tiles.forEach((t) => tileMap.set(t.id, t));

  const tileSize = TILE_SIZE * zoom;
  const canvasWidth = level.width * tileSize;
  const canvasHeight = level.height * tileSize;

  // Real-time animation clock for animated actors / objects (4 fps)
  const [animTick, setAnimTick] = useState<number>(0);
  useEffect(() => {
    const timer = setInterval(() => {
      setAnimTick((t) => (t + 1) % 120);
    }, 250);
    return () => clearInterval(timer);
  }, []);

  // Pre-load tile images for all tiles across all tilesets.
  // Re-runs when the registry gains ids at runtime (refreshTilesetsFromServer
  // injects disk tilesets after mount); already-loaded images are kept so the
  // canvas never flashes back to fallback colors.
  const [tileImages, setTileImages] = useState<Map<string, HTMLImageElement>>(new Map());
  /* Combat-art meta-tiles (screens/combat_art/*.json) + enemy->set lookup
   * for the battle preview.  Loaded once; empty maps = offline fallback
   * (the preview keeps its drawn ellipse placeholders). */
  const [combatSets, setCombatSets] = useState<Map<string, { w: number; h: number; f0: Array<string | null>; f1: Array<string | null> }>>(new Map());
  const [enemyArtByName, setEnemyArtByName] = useState<Map<string, string>>(new Map());
  const [combatImgs, setCombatImgs] = useState<Map<string, HTMLImageElement>>(new Map());
  const [titleFontImgs, setTitleFontImgs] = useState<Map<number, HTMLImageElement>>(new Map());
  const [titleLogoImg, setTitleLogoImg] = useState<HTMLImageElement | null>(null);
  /* Shared overworld enemy sprites (UPPER name -> cells): type-owned art
   * wins over per-instance sprite names, mirroring the ROM. */
  const [enemyOwByName, setEnemyOwByName] = useState<Map<string, string[]>>(new Map());
  const [enemyOwDims, setEnemyOwDims] = useState<Map<string, { w: number; h: number }>>(new Map());
  const [owImgs, setOwImgs] = useState<Map<string, HTMLImageElement>>(new Map());
  useEffect(() => {
    fetchCombatArtList().then(async (items) => {
      const m = new Map<string, { w: number; h: number; f0: Array<string | null>; f1: Array<string | null> }>();
      for (const it of items) {
        try {
          const s = await fetchCombatArtSet(it.id);
          m.set(it.id, { w: s.width, h: s.height, f0: s.frame0, f1: s.frame1 || s.frame0 });
        } catch { /* keep other sets */ }
      }
      setCombatSets(m);
    }).catch(() => undefined);
    fetchEnemyTypeList().then(async (types) => {
      const e = new Map<string, string>();
      const o = new Map<string, string[]>();
      const owDims = new Map<string, { w: number; h: number }>();
      const needed = new Set<string>();
      for (const t of types) {
        if (t.art) {
          e.set(t.label.toUpperCase(), t.art);
          e.set(t.id.toUpperCase(), t.art);
        }
        try {
          const full = await fetchEnemyType(t.id);
          const cells = (full.overworld && full.overworld.cells) || [];
          if (cells.length > 0) {
            o.set(t.id.toUpperCase(), cells);
            o.set(t.label.toUpperCase(), cells);
            owDims.set(t.id.toUpperCase(), { w: full.overworld.width || 1, h: full.overworld.height || 1 });
            owDims.set(t.label.toUpperCase(), { w: full.overworld.width || 1, h: full.overworld.height || 1 });
            cells.forEach((c: string) => needed.add(c));
          }
        } catch { /* keep others */ }
      }
      setEnemyArtByName(e);
      setEnemyOwByName(o);
      setEnemyOwDims(owDims);
      const imgs = new Map<string, HTMLImageElement>();
      let done = 0;
      const names = [...needed];
      if (names.length === 0) return;
      names.forEach((name) => {
        const img = new Image();
        img.src = `/tiles/enemies/${name}.png`;
        const fin = () => { done++; imgs.set(name, img); if (done === names.length) setOwImgs(new Map(imgs)); };
        img.onload = fin;
        img.onerror = fin;
      });
    }).catch(() => undefined);
    const imgs = new Map<string, HTMLImageElement>();
    let done = 0;
    SHEET_TILE_NAMES.forEach((name) => {
      const img = new Image();
      img.src = COMBAT_TILE_URL(name);
      const fin = () => { done++; imgs.set(name, img); if (done === SHEET_TILE_NAMES.length) setCombatImgs(new Map(imgs)); };
      img.onload = fin;
      img.onerror = fin;
    });
  }, []);
  // Title-screen authentic preview assets: the intrepid font tiles (96,
  // 16 cols x 6 rows; char tile index = charCode - 32) and the bitmap title
  // logo (public copy of assets/title-red.png, published by `make gfx`).
  useEffect(() => {
    const imgs = new Map<number, HTMLImageElement>();
    let done = 0;
    for (let i = 0; i < 96; i++) {
      const img = new Image();
      img.src = `/tiles/intrepid/tile_${Math.floor(i / 16)}_${i % 16}.png`;
      const fin = () => { done++; imgs.set(i, img); if (done === 96) setTitleFontImgs(new Map(imgs)); };
      img.onload = fin;
      img.onerror = fin;
    }
  }, []);
  const titleLogoUrl = level.titleLayout?.logoImage?.url || '/tiles/title/logo.png';
  useEffect(() => {
    const img = new Image();
    img.src = titleLogoUrl;
    img.onload = () => setTitleLogoImg(img);
    img.onerror = () => setTitleLogoImg(null);
  }, [titleLogoUrl]);
  const tilesetIdsKey = Object.keys(BUILTIN_TILESETS).sort().join(',');
  useEffect(() => {
    setTileImages((prev) => {
      const imgMap = new Map<string, HTMLImageElement>(prev);
      const tilesWithScope: { scopedId: string; url: string }[] = [];
      Object.entries(BUILTIN_TILESETS).forEach(([tsId, ts]) => {
        ts.tiles.forEach((t) => {
          const scopedId = `${tsId}.${t.id}`;
          if (!imgMap.has(scopedId)) {
            tilesWithScope.push({ scopedId, url: t.image_url });
          }
        });
      });
      if (tilesWithScope.length === 0) return prev;

      let loadedCount = 0;
      tilesWithScope.forEach((item) => {
        const img = new Image();
        img.src = item.url;
        img.onload = () => {
          loadedCount++;
          imgMap.set(item.scopedId, img);
          if (loadedCount === tilesWithScope.length) {
            setTileImages(new Map(imgMap));
          }
        };
        img.onerror = () => {
          loadedCount++;
          if (loadedCount === tilesWithScope.length) {
            setTileImages(new Map(imgMap));
          }
        };
        imgMap.set(item.scopedId, img);
      });
      return new Map(imgMap);
    });
  }, [tilesetIdsKey]);

  // Live battle-card + HUD skins (screens/cards_skin.json and
  // screens/battle_hud.json via the dev API).  The battle-screen
  // preview renders hand cards and HUD icons from these -- the same
  // data battle_compile.py emits into the ROM -- instead of hardcoding
  // icon keys (which drifted: fire/ice riders that the ROM no longer
  // draws, a phantom deck_cards tile).
  const [cardSkin, setCardSkin] = useState<CardSkin | null>(null);
  const [hudSkin, setHudSkin] = useState<BattleHud | null>(null);
  useEffect(() => {
    fetchCardSkin().then(setCardSkin).catch(() => undefined);
    fetchBattleHud().then(setHudSkin).catch(() => undefined);
  }, []);

  // Convert mouse pixel coordinates to tile coordinates
  const getTileCoords = (e: React.MouseEvent<HTMLCanvasElement>): { x: number; y: number } | null => {
    const canvas = canvasRef.current;
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    const px = e.clientX - rect.left;
    const py = e.clientY - rect.top;

    const tx = Math.floor(px / tileSize);
    const ty = Math.floor(py / tileSize);

    if (tx < 0 || tx >= level.width || ty < 0 || ty >= level.height) {
      return null;
    }
    return { x: tx, y: ty };
  };

  // Render function
  const render = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    // Clear canvas
    ctx.fillStyle = '#0f141c';
    ctx.fillRect(0, 0, canvasWidth, canvasHeight);

    // ── TITLE SCREEN AUTHENTIC RENDERER ──
    // Mirrors the ROM 1:1: white GB background, the bitmap logo
    // (assets/title-red.png, drawn at its tile position), and all text
    // using the intrepid font tiles at the exact ROM rows/columns.  The
    // "Objects" toggle picks the ROM's two title states: menu (caret on
    // NEW GAME, post-START) when on, PRESS START prompt when off.
    if (level.isScreen && (level.mapId === 'SCREEN_TITLE' || level.id === 'title')) {
      const titleLayout = level.titleLayout || {};
      const logo = titleLayout.logo || { x: 0, y: 1, lines: [] };
      const logoImage = titleLayout.logoImage;
      const subtitle = titleLayout.subtitle;
      const graphic = titleLayout.graphic || { enabled: false, x: 2, y: 7, width: 16, height: 5, lines: [] };
      const prompt = titleLayout.prompt || { text: 'PRESS START', x: 4, y: 14, align: 'center' };
      const credits = titleLayout.credits || { enabled: false, text: 'GALLIA BELGICA', x: 6, y: 17, align: 'right' };
      const menu = titleLayout.menu || { x: 3, caret_x: 3, first_row: 10, row_step: 2, options: [] };

      // 1. GB white background (the ROM title is font + logo over white).
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, canvasWidth, canvasHeight);

      // 2. Cyan grid lines if enabled
      if (showGrid && tileSize >= 6) {
        ctx.strokeStyle = 'rgba(74, 185, 209, 0.25)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (let x = 0; x <= level.width; x++) {
          ctx.moveTo(x * tileSize, 0);
          ctx.lineTo(x * tileSize, canvasHeight);
        }
        for (let y = 0; y <= level.height; y++) {
          ctx.moveTo(0, y * tileSize);
          ctx.lineTo(canvasWidth, y * tileSize);
        }
        ctx.stroke();
      }

      // 3. Bitmap logo (nearest-neighbour, exact tile footprint).
      const li = titleLogoImg;
      if (logoImage && logoImage.enabled !== false && li) {
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(
          li,
          logoImage.x * tileSize,
          logoImage.y * tileSize,
          logoImage.width * tileSize,
          logoImage.height * tileSize
        );
      } else if (logo.lines && logo.lines.length > 0) {
        // Fallback: the ASCII logo identity (image disabled / not loaded).
        logo.lines.forEach((line: string, idx: number) => {
          drawTitleText(ctx, titleFontImgs, logo.x, logo.y + idx, line, tileSize);
        });
      }

      // 4. Subtitle (e.g. BATTLE DEMO), centered by align like the compiler.
      if (subtitle && subtitle.text) {
        const len = subtitle.text.length;
        const sx =
          subtitle.align === 'right'
            ? 20 - len
            : subtitle.align === 'left'
            ? 0
            : Math.max(0, Math.floor((20 - len) / 2));
        drawTitleText(ctx, titleFontImgs, sx, subtitle.y, subtitle.text, tileSize);
      }

      // 5. Big Title Graphic / ASCII artwork (if enabled), drawn with the
      //    same font tiles the ROM uses.
      if (graphic.enabled && graphic.lines && graphic.lines.length > 0) {
        graphic.lines.forEach((line: string, idx: number) => {
          drawTitleText(ctx, titleFontImgs, graphic.x, graphic.y + idx, line, tileSize);
        });
      }

      // 6. Menu (post-START state) or the PRESS START prompt, never both.
      //    The menu options are editor objects, so the existing "Objects"
      //    toggle switches the preview between the ROM's two title states.
      if (showObjects && menu.options && menu.options.length > 0) {
        menu.options.forEach((opt: string, idx: number) => {
          const selected = idx === 0; // ROM boot cursor sits on NEW GAME
          const text = (selected ? '>' : ' ') + opt.slice(1);
          drawTitleText(ctx, titleFontImgs, menu.x, menu.first_row + idx * menu.row_step, text, tileSize);
        });
      } else if (prompt.text) {
        const len = prompt.text.length;
        const px =
          prompt.align === 'right'
            ? 20 - len
            : prompt.align === 'left'
            ? prompt.x ?? 0
            : Math.max(0, Math.floor((20 - len) / 2));
        drawTitleText(ctx, titleFontImgs, px, prompt.y ?? 14, prompt.text, tileSize);
      }

      // 7. Bottom-row credits, drawn with the font tiles.
      if (credits.enabled && credits.text) {
        const len = credits.text.length;
        const cx =
          credits.align === 'center'
            ? Math.max(0, Math.floor((20 - len) / 2))
            : credits.align === 'left'
            ? credits.x ?? 0
            : 20 - len;
        drawTitleText(ctx, titleFontImgs, cx, credits.y ?? 17, credits.text, tileSize);
      }

      return;
    }

    // ── BATTLE SCREEN AUTHENTIC RENDERER ──
    // Geometry is DATA-DRIVEN from the screen's hud_layout -- the same
    // rows the ROM reads (AGENTS.md 52.11.2: banner 0, HP 2, art 3-4,
    // caret 5/6, hero 7, deck/AP 8, msgs 9, combo 10, cards 11-14,
    // markers 15, desc 16, timer 17).  Enemy columns come from the
    // screen's enemy objects (boss screens: one centered enemy; the
    // legacy 9x9 boss meta-tile preview is gone -- bosses render their
    // combat-art set or the shared placeholder, exactly like the ROM).
    if (level.isScreen && (level.mapId === 'SCREEN_BATTLE' || level.id.includes('battle'))) {
      const L = level.battleHudLayout;
      const bannerRow = L?.turn_banner_row ?? 0;
      const enemyHpRow = L?.enemy_hp_row ?? 2;
      const enemySpriteRow = L?.enemy_sprite_row ?? 3;
      const enemyCursorRow = L?.enemy_cursor_row ?? 5;
      const heroLabelRow = L?.hero_label_row ?? 7;
      const heroLabelCol = L?.hero_label_col ?? 0;
      const heroHpRow = L?.hero_hp_row ?? 7;
      const heroHpCol = L?.hero_hp_col ?? 13;
      const deckRow = L?.deck_row ?? 8;
      const deckCol = L?.deck_col ?? 1;
      const apRow = L?.ap_row ?? 8;
      const apCol = L?.ap_col ?? 13;
      const comboRow = L?.combo_row ?? 10;
      const cardsRow = L?.cards_row ?? 14;
      const cardCursorRow = L?.card_cursor_row ?? 15;
      const cardDescRow = L?.card_desc_row ?? 16;
      const timerRow = L?.timer_row ?? 17;

      // 1. Crisp white background
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(0, 0, canvasWidth, canvasHeight);

      // 2. Mockup Cyan Grid lines
      if (showGrid && tileSize >= 6) {
        ctx.strokeStyle = 'rgba(74, 185, 209, 0.45)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        for (let x = 0; x <= level.width; x++) {
          ctx.moveTo(x * tileSize, 0);
          ctx.lineTo(x * tileSize, canvasHeight);
        }
        for (let y = 0; y <= level.height; y++) {
          ctx.moveTo(0, y * tileSize);
          ctx.lineTo(canvasWidth, y * tileSize);
        }
        ctx.stroke();
      }

      const fontScale = Math.max(10, Math.floor(tileSize * 0.72));
      ctx.font = `bold ${fontScale}px monospace`;
      ctx.fillStyle = '#593c28';

      // 3. Row 0: Dynamic target banner (ROM: "TARGET <name>" on select)
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(
        'TARGET SLIME',
        canvasWidth / 2,
        bannerRow * tileSize + tileSize * 0.5
      );

      // 4. Enemy roster at the screen's configured positions (boss
      // screens: single centered enemy).
      {
        const enemyObjs = (level.objects || []).filter((o) => o.type === 'enemy');
        const enemyCols = enemyObjs.length > 0
          ? enemyObjs.map((o) => o.position.x)
          : [0, 7, 14];
        const enemyHps = ['10/10', '10/10', '02/10'];

        enemyCols.forEach((colX, idx) => {
          const cx = (colX + 1.5) * tileSize;
          // Enemy HP Text above sprite
          ctx.fillStyle = '#593c28';
          ctx.font = `bold ${Math.max(9, Math.floor(tileSize * 0.65))}px monospace`;
          ctx.textAlign = 'center';
          ctx.fillText(enemyHps[idx], cx, (enemyHpRow + 0.5) * tileSize);

          // Combat-art meta-tile (screens/combat_art/*.json): resolve this
          // slot's set through the enemy object's battle name, exactly how
          // the ROM resolves art per enemy type.  Unmatched/offline slots
          // keep the drawn placeholder below.
          const wobble = animTick % 2 === idx % 2 ? 1 : 0;
          const slotObjs = (level.objects || []).filter((o) => o.type === 'enemy');
          const slotObj = slotObjs[idx];
          const slotKey = ((slotObj && (slotObj.battle_name || (slotObj.properties && slotObj.properties.display_name))) || '').toUpperCase();
          const slotSetId = enemyArtByName.get(slotKey);
          const slotSet = slotSetId ? combatSets.get(slotSetId) : undefined;
          const slotCells = slotSet ? (wobble ? slotSet.f1 : slotSet.f0) : [];
          const canDrawArt = !!slotSet && slotCells.length === slotSet.w * slotSet.h &&
            slotCells.every((c) => c === null || combatImgs.get(c));

          if (canDrawArt && slotSet) {
            const startX = colX * tileSize;
            const startY = enemySpriteRow * tileSize;
            for (let ay = 0; ay < slotSet.h; ay++) {
              for (let ax = 0; ax < slotSet.w; ax++) {
                const cell = slotCells[ay * slotSet.w + ax];
                if (cell === null) continue;
                const img = combatImgs.get(cell);
                if (img) ctx.drawImage(img, startX + ax * tileSize, startY + ay * tileSize, tileSize, tileSize);
              }
            }
          } else {
            const sx = (colX + 0.3) * tileSize;
            const sy = (enemySpriteRow + 0.1) * tileSize;
            const sw = 2.4 * tileSize;
            const sh = 1.8 * tileSize;
            ctx.fillStyle = '#72b847';
            ctx.beginPath();
            ctx.ellipse(cx, sy + sh * 0.55 + wobble, sw * 0.48, sh * 0.44, 0, 0, Math.PI * 2);
            ctx.fill();
            ctx.strokeStyle = '#4e852d';
            ctx.lineWidth = 1.5;
            ctx.stroke();

            ctx.fillStyle = '#ffffff';
            ctx.fillRect(cx - sw * 0.22, sy + sh * 0.35 + wobble, Math.max(3, tileSize * 0.22), Math.max(3, tileSize * 0.22));
            ctx.fillRect(cx + sw * 0.12, sy + sh * 0.35 + wobble, Math.max(3, tileSize * 0.22), Math.max(3, tileSize * 0.22));

            ctx.fillStyle = '#000000';
            ctx.fillRect(cx - sw * 0.18, sy + sh * 0.38 + wobble, Math.max(2, tileSize * 0.12), Math.max(2, tileSize * 0.12));
            ctx.fillRect(cx + sw * 0.16, sy + sh * 0.38 + wobble, Math.max(2, tileSize * 0.12), Math.max(2, tileSize * 0.12));
          }
        });

        // Target arrow under enemy 0 (the default target), centered on
        // the art's middle column (art_x = pos+1, caret = art_x+1).
        const arrowImg = tileImages.get('combat.combat_arrow_pointing_up');
        if (arrowImg) {
          ctx.drawImage(arrowImg, (enemyCols[0] + 2) * tileSize, enemyCursorRow * tileSize, tileSize, tileSize);
        } else {
          ctx.fillStyle = '#593c28';
          ctx.font = `bold ${Math.max(12, Math.floor(tileSize * 0.9))}px sans-serif`;
          ctx.textAlign = 'center';
          ctx.fillText('⬆', (enemyCols[0] + 2.5) * tileSize, (enemyCursorRow + 0.5) * tileSize);
        }
      }

      // 5. Dual-Column Hero & Status Panel (Rows 6 & 7)
      ctx.font = `bold ${fontScale}px monospace`;
      ctx.textAlign = 'left';
      ctx.fillStyle = '#593c28';

      // Row 6: Left "HERO" + Icon | Right "[♥] : 10/10"
      // HUD icon tiles come from the live battle_hud.json skin (the
      // same names battle_compile.py resolves into g_hud_skin), not
      // hardcoded keys.
      const heroSprite = tileImages.get('combat.hero');
      const heartImg = tileImages.get('combat.' + (hudSkin?.hp.icon || 'combat_hp_icon'));
      const batteryImg = tileImages.get('combat.' + (hudSkin?.ap.icon || 'combat_ap_icon'));
      const deckImg = tileImages.get('combat.' + (hudSkin?.deck.icon || 'combat_deck_icon'));

      ctx.fillText('HERO', heroLabelCol * tileSize, (heroLabelRow + 0.5) * tileSize);
      if (heroSprite) {
        ctx.drawImage(heroSprite, (heroLabelCol + 2.6) * tileSize, (heroLabelRow - 0.2) * tileSize, tileSize, tileSize);
      } else {
        ctx.fillText('🧙', (heroLabelCol + 2.6) * tileSize, (heroLabelRow + 0.5) * tileSize);
      }

      if (heartImg) {
        ctx.drawImage(heartImg, heroHpCol * tileSize, (heroHpRow - 0.2) * tileSize, tileSize, tileSize);
        ctx.fillStyle = '#593c28';
        ctx.fillText(': 10/10', (heroHpCol + 1.2) * tileSize, (heroHpRow + 0.5) * tileSize);
      } else {
        ctx.fillStyle = '#c0392b';
        ctx.fillText('♥', heroHpCol * tileSize, (heroHpRow + 0.5) * tileSize);
        ctx.fillStyle = '#593c28';
        ctx.fillText(' : 10/10', (heroHpCol + 0.8) * tileSize, (heroHpRow + 0.5) * tileSize);
      }

      // Row 7: Left "DECK:  7 [🎴]" | Right "[🔋] :  6 :  6"
      ctx.fillText('DECK:  7', deckCol * tileSize, (deckRow + 0.5) * tileSize);
      if (deckImg) {
        ctx.drawImage(deckImg, (deckCol + 4.2) * tileSize, (deckRow - 0.2) * tileSize, tileSize, tileSize);
      } else {
        ctx.fillText('🎴', (deckCol + 4.2) * tileSize, (deckRow + 0.5) * tileSize);
      }

      if (batteryImg) {
        ctx.drawImage(batteryImg, apCol * tileSize, (apRow - 0.2) * tileSize, tileSize, tileSize);
        ctx.fillStyle = '#593c28';
        ctx.fillText(':  6 :  6', (apCol + 1.2) * tileSize, (apRow + 0.5) * tileSize);
      } else {
        ctx.fillText('🔋 :  6 :  6', apCol * tileSize, (apRow + 0.5) * tileSize);
      }

      // 6. Row 9: Combo Header "COMBO"
      ctx.fillText('COMBO', 1 * tileSize, (comboRow + 0.5) * tileSize);

      // 7. Rows 10–13: 5 Framed Hand Cards -- rendered from the LIVE skin
      // (screens/cards_skin.json), the same data battle_compile.py emits
      // into the ROM.  Weapon icon per card type; box tint = type color,
      // element color when the demo card carries a status (the ROM shows
      // element status purely as the box tint -- battle_card_box_color();
      // the old floating fire/ice rider icons were removed).
      const skinFallback: CardSkin = {
        id: 'card_skin', label: '', box: { w: 3, h: 4 },
        types: {
          sword: { icon: 'combat_sword_icon', color: 'iron' },
          shield: { icon: 'combat_shield_icon', color: 'wood' },
          bow: { icon: 'combat_bow_icon', color: 'gold' },
          heal: { icon: 'combat_ring_icon', color: 'field' },
          dagger: { icon: 'combat_dagger_icon', color: 'poison' },
        },
        elements: {
          fire: { icon: 'combat_fire_status', color: 'fire' },
          ice: { icon: 'combat_ice_status', color: 'iron' },
          poison: { icon: 'combat_poison_status', color: 'poison' },
        },
      };
      const skin = cardSkin || skinFallback;
      const demoHand: Array<{ type: keyof CardSkin['types']; value: number; elem: keyof CardSkin['elements'] | null }> = [
        { type: 'sword', value: 3, elem: null },
        { type: 'bow', value: 2, elem: 'poison' },
        { type: 'shield', value: 2, elem: null },
        { type: 'heal', value: 2, elem: null },
        { type: 'dagger', value: 4, elem: null },
      ];
      const typeFallbacks: Record<string, string> = {
        sword: '🗡️', shield: '🛡️', bow: '🏹', heal: '💚', dagger: '🔪',
      };
      // Hand stride is 4 in the ROM (box 3 wide + 1 gap).
      const cardCols = [1, 5, 9, 13, 17];

      cardCols.forEach((cx, idx) => {
        const card = demoHand[idx];
        const typeSkin = skin.types[card.type];
        const tintHex = CARD_COLOR_HEX[
          (card.elem ? skin.elements[card.elem].color : typeSkin.color)
        ] || '#deb580';
        const cardX = cx * tileSize;
        /* cards_row is the BOTTOM row in the ROM (box spans
         * cards_row-3..cards_row); draw upward from it. */
        const cardY = (cardsRow - 3) * tileSize;
        const cardW = 2.8 * tileSize;
        const cardH = 3.8 * tileSize;

        // Outer border
        ctx.fillStyle = '#b78e58';
        ctx.fillRect(cardX, cardY, cardW, cardH);

        // Inner parchment face
        ctx.fillStyle = '#deb580';
        ctx.fillRect(cardX + 2, cardY + 2, cardW - 4, cardH - 4);

        // Box tint: element status overrides the type's material color,
        // exactly like battle_card_box_color() in the ROM.
        ctx.globalAlpha = 0.35;
        ctx.fillStyle = tintHex;
        ctx.fillRect(cardX + 2, cardY + 2, cardW - 4, cardH - 4);
        ctx.globalAlpha = 1.0;

        // Inset border line
        ctx.strokeStyle = '#cd9e64';
        ctx.lineWidth = 1;
        ctx.strokeRect(cardX + 4, cardY + 4, cardW - 8, cardH - 8);

        // Weapon icon in center (skin-driven combat tileset tile)
        const weaponImg = tileImages.get('combat.' + typeSkin.icon);
        if (weaponImg) {
          ctx.drawImage(weaponImg, cardX + (cardW - tileSize * 1.2) / 2, cardY + tileSize * 0.5, tileSize * 1.2, tileSize * 1.2);
        } else {
          ctx.fillStyle = '#593c28';
          ctx.font = `${Math.max(12, Math.floor(tileSize * 0.85))}px sans-serif`;
          ctx.textAlign = 'center';
          ctx.fillText(typeFallbacks[card.type] || '❔', cardX + cardW / 2, cardY + cardH * 0.42);
        }

        // Power digit underneath (the ROM renders it as a font glyph --
        // the semantic buffer asserts '0'+value on that row).
        ctx.fillStyle = '#593c28';
        ctx.font = `bold ${Math.max(11, Math.floor(tileSize * 0.8))}px monospace`;
        ctx.textAlign = 'center';
        ctx.fillText(String(card.value), cardX + cardW / 2, cardY + cardH * 0.78);
      });

      // 8. Row 14: Card Cursor
      const cardArrow = tileImages.get('combat.combat_arrow_pointing_up');
      if (cardArrow) {
        ctx.drawImage(cardArrow, (cardCols[0] + 0.9) * tileSize, cardCursorRow * tileSize, tileSize, tileSize);
      } else {
        ctx.fillStyle = '#593c28';
        ctx.font = `bold ${Math.max(12, Math.floor(tileSize * 0.9))}px sans-serif`;
        ctx.textAlign = 'center';
        ctx.fillText('⬆', (cardCols[0] + 1.4) * tileSize, (cardCursorRow + 0.5) * tileSize);
      }

      // 9. Row 15: Card Description "Sword: physical"
      ctx.fillStyle = '#593c28';
      ctx.font = `bold ${fontScale}px monospace`;
      ctx.textAlign = 'left';
      ctx.fillText('Sword: physical', 1 * tileSize, (cardDescRow + 0.5) * tileSize);

      // 10. Timer Bar (single row, at the layout's timer_row)
      const timerWidthCols = 20;
      const barX = 0 * tileSize;
      const barY = timerRow * tileSize;
      const barW = timerWidthCols * tileSize;
      const barH = tileSize;

      ctx.fillStyle = '#d59f63';
      ctx.fillRect(barX, barY, barW, barH);
      ctx.strokeStyle = '#b78e58';
      ctx.lineWidth = 1.5;
      ctx.strokeRect(barX, barY, barW, barH);

      // Hover cursor
      if (hoverTile) {
        ctx.strokeStyle = 'rgba(231, 76, 60, 0.8)';
        ctx.lineWidth = 2;
        ctx.strokeRect(hoverTile.x * tileSize, hoverTile.y * tileSize, tileSize, tileSize);
      }

      return;
    }

    // 1. Render Terrain Tiles
    if (showTerrain) {
      for (let y = 0; y < level.height; y++) {
        for (let x = 0; x < level.width; x++) {
          const tileId = level.grid[y]?.[x] || defaultFloorTile;
          const tDef = tileMap.get(tileId) || tileMap.get(defaultFloorTile);

          const px = x * tileSize;
          const py = y * tileSize;

          // Draw tile image if loaded, fallback to colored rect
          const img = tileImages.get(`${level.tileset}.${tileId}`);
          if (img && img.complete && img.naturalWidth > 0) {
            ctx.imageSmoothingEnabled = false;
            ctx.drawImage(img, px, py, tileSize, tileSize);
          } else {
            const color = tDef ? tDef.color : '#88c070';
            ctx.fillStyle = color;
            ctx.fillRect(px, py, tileSize, tileSize);
          }

          // Perimeter wall highlight
          if (x === 0 || x === level.width - 1 || y === 0 || y === level.height - 1) {
            ctx.fillStyle = 'rgba(0, 0, 0, 0.25)';
            ctx.fillRect(px, py, tileSize, tileSize);
          }
        }
      }
    }

    // 2. Render Rectangle Drag Preview
    if (isMouseDown && activeTool === 'rect' && dragStartTile && hoverTile) {
      const rx = Math.min(dragStartTile.x, hoverTile.x);
      const ry = Math.min(dragStartTile.y, hoverTile.y);
      const rw = Math.abs(dragStartTile.x - hoverTile.x) + 1;
      const rh = Math.abs(dragStartTile.y - hoverTile.y) + 1;

      const tDef = tileMap.get(selectedTileId);
      const img = tileImages.get(`${level.tileset}.${selectedTileId}`);
      ctx.globalAlpha = 0.6;
      if (img && img.complete && img.naturalWidth > 0) {
        ctx.imageSmoothingEnabled = false;
        for (let ty = ry; ty < ry + rh; ty++) {
          for (let tx = rx; tx < rx + rw; tx++) {
            ctx.drawImage(img, tx * tileSize, ty * tileSize, tileSize, tileSize);
          }
        }
      } else {
        ctx.fillStyle = tDef?.color || '#ffffff';
        ctx.fillRect(rx * tileSize, ry * tileSize, rw * tileSize, rh * tileSize);
      }
      ctx.strokeStyle = '#ffffff';
      ctx.lineWidth = 2;
      ctx.strokeRect(rx * tileSize, ry * tileSize, rw * tileSize, rh * tileSize);
      ctx.globalAlpha = 1.0;
    }

    // 2b. Render Clone Region Capture Preview
    if (isMouseDown && activeTool === 'clone' && isCapturingClone && dragStartTile && hoverTile) {
      const rx = Math.min(dragStartTile.x, hoverTile.x);
      const ry = Math.min(dragStartTile.y, hoverTile.y);
      const rw = Math.abs(dragStartTile.x - hoverTile.x) + 1;
      const rh = Math.abs(dragStartTile.y - hoverTile.y) + 1;

      ctx.save();
      ctx.fillStyle = 'rgba(0, 240, 255, 0.18)';
      ctx.fillRect(rx * tileSize, ry * tileSize, rw * tileSize, rh * tileSize);
      ctx.strokeStyle = '#00f0ff';
      ctx.lineWidth = 2;
      ctx.setLineDash([4, 4]);
      ctx.strokeRect(rx * tileSize, ry * tileSize, rw * tileSize, rh * tileSize);

      ctx.setLineDash([]);
      ctx.fillStyle = 'rgba(10, 15, 25, 0.85)';
      const badgeY = Math.max(0, ry * tileSize - 20);
      ctx.fillRect(rx * tileSize, badgeY, 60, 18);
      ctx.strokeStyle = '#00f0ff';
      ctx.strokeRect(rx * tileSize, badgeY, 60, 18);
      ctx.fillStyle = '#00f0ff';
      ctx.font = 'bold 11px monospace';
      ctx.fillText(`${rw}×${rh}`, rx * tileSize + 6, badgeY + 13);
      ctx.restore();
    }

    // 2c. Render Clone Stamp Ghost Preview
    if (activeTool === 'clone' && !isCapturingClone && clonePattern && clonePattern.length > 0 && hoverTile && activeLayer === 'terrain') {
      const pw = clonePattern[0]?.length || 0;
      const ph = clonePattern.length;
      const sx = hoverTile.x;
      const sy = hoverTile.y;

      ctx.save();
      ctx.globalAlpha = 0.65;
      ctx.imageSmoothingEnabled = false;

      for (let py = 0; py < ph; py++) {
        for (let px = 0; px < pw; px++) {
          const tId = clonePattern[py][px];
          const tx = sx + px;
          const ty = sy + py;
          if (tx >= level.width || ty >= level.height) continue;

          const img = tileImages.get(`${level.tileset}.${tId}`);
          const tDef = tileMap.get(tId);
          if (img && img.complete && img.naturalWidth > 0) {
            ctx.drawImage(img, tx * tileSize, ty * tileSize, tileSize, tileSize);
          } else {
            ctx.fillStyle = tDef?.color || '#ffffff';
            ctx.fillRect(tx * tileSize, ty * tileSize, tileSize, tileSize);
          }
        }
      }

      ctx.globalAlpha = 0.95;
      ctx.strokeStyle = '#00f0ff';
      ctx.lineWidth = 2;
      const bw = Math.min(pw, level.width - sx) * tileSize;
      const bh = Math.min(ph, level.height - sy) * tileSize;
      ctx.strokeRect(sx * tileSize, sy * tileSize, bw, bh);

      ctx.fillStyle = 'rgba(10, 15, 25, 0.85)';
      const badgeY = Math.max(0, sy * tileSize - 20);
      ctx.fillRect(sx * tileSize, badgeY, 75, 18);
      ctx.strokeStyle = '#00f0ff';
      ctx.strokeRect(sx * tileSize, badgeY, 75, 18);
      ctx.fillStyle = '#00f0ff';
      ctx.font = 'bold 11px monospace';
      ctx.fillText(`📋 ${pw}×${ph}`, sx * tileSize + 6, badgeY + 13);
      ctx.restore();
    }

    // 3. Render Collision Walkability Overlay
    if (showCollision) {
      for (let y = 0; y < level.height; y++) {
        for (let x = 0; x < level.width; x++) {
          const tileId = level.grid[y]?.[x] || defaultFloorTile;
          const tDef = tileMap.get(tileId);
          const isPerimeter = x === 0 || x === level.width - 1 || y === 0 || y === level.height - 1;
          const isBlocked = isPerimeter || (tDef ? !tDef.walkable : false);

          if (isBlocked) {
            const px = x * tileSize;
            const py = y * tileSize;

            ctx.fillStyle = 'rgba(231, 76, 60, 0.4)';
            ctx.fillRect(px, py, tileSize, tileSize);

            // Red diagonal hatch
            ctx.strokeStyle = 'rgba(231, 76, 60, 0.7)';
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            ctx.moveTo(px, py);
            ctx.lineTo(px + tileSize, py + tileSize);
            ctx.stroke();
          }
        }
      }
    }

    // 4. Render Grid Lines
    if (showGrid && tileSize >= 8) {
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.12)';
      ctx.lineWidth = 1;

      ctx.beginPath();
      for (let x = 0; x <= level.width; x++) {
        ctx.moveTo(x * tileSize, 0);
        ctx.lineTo(x * tileSize, canvasHeight);
      }
      for (let y = 0; y <= level.height; y++) {
        ctx.moveTo(0, y * tileSize);
        ctx.lineTo(canvasWidth, y * tileSize);
      }
      ctx.stroke();
    }

    // 5. Render Regions
    if (showRegions && level.regions) {
      level.regions.forEach((reg, idx) => {
        const isSelected = activeLayer === 'regions' && selectedEntityIndex === idx;
        const b = reg.bounds;
        const rx = b.x * tileSize;
        const ry = b.y * tileSize;
        const rw = b.width * tileSize;
        const rh = b.height * tileSize;

        ctx.fillStyle = isSelected ? 'rgba(52, 152, 219, 0.25)' : 'rgba(52, 152, 219, 0.12)';
        ctx.fillRect(rx, ry, rw, rh);

        ctx.strokeStyle = isSelected ? '#3498db' : 'rgba(52, 152, 219, 0.6)';
        ctx.lineWidth = isSelected ? 2.5 : 1.5;
        ctx.setLineDash([4, 4]);
        ctx.strokeRect(rx, ry, rw, rh);
        ctx.setLineDash([]);

        // Region label
        if (tileSize >= 16) {
          ctx.fillStyle = '#3498db';
          ctx.font = `bold ${Math.max(10, Math.floor(tileSize * 0.4))}px Inter, sans-serif`;
          ctx.textAlign = 'left';
          ctx.textBaseline = 'top';
          ctx.fillText(`🏷️ ${reg.id}`, rx + 4, ry + 4);
        }
      });
    }

    // 6b. Linked-edge indicators (neighbors): a green strip along each
    // linked border with the target scene name. Exits stay orange squares.
    if (showExits && !level.isScreen) {
      const neighbors = level.neighbors || {};
      const linked: Array<{ direction: string; target: string }> = [];
      for (const direction of ['north', 'south', 'east', 'west']) {
        const target = (neighbors as Record<string, string | undefined>)[direction];
        if (target && target.trim()) linked.push({ direction, target: target.trim() });
      }
      for (const { direction, target } of linked) {
        let x = 0, y = 0, wdt = 0, hgt = 0;
        if (direction === 'north') { x = 0; y = 0; wdt = level.width; hgt = 1; }
        else if (direction === 'south') { x = 0; y = level.height - 1; wdt = level.width; hgt = 1; }
        else if (direction === 'west') { x = 0; y = 0; wdt = 1; hgt = level.height; }
        else { x = level.width - 1; y = 0; wdt = 1; hgt = level.height; }
        ctx.fillStyle = 'rgba(46, 204, 113, 0.30)';
        ctx.fillRect(x * tileSize, y * tileSize, wdt * tileSize, hgt * tileSize);
        if (tileSize >= 16) {
          ctx.fillStyle = '#27ae60';
          ctx.font = `bold ${Math.max(10, Math.floor(tileSize * 0.5))}px Inter, sans-serif`;
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          const cx = (x + wdt / 2) * tileSize;
          const cy = (y + hgt / 2) * tileSize;
          const short = target.length > 12 ? target.slice(0, 11) + '…' : target;
          ctx.fillText(`⇄ ${short}`, cx, cy);
        }
      }
    }

    // 6. Render Exits
    if (showExits && level.exits) {
      level.exits.forEach((ex, idx) => {
        const isSelected = activeLayer === 'exits' && selectedEntityIndex === idx;
        const px = ex.x * tileSize;
        const py = ex.y * tileSize;

        const m = tileSize >= 16 ? 2 : (tileSize >= 8 ? 1 : 0);
        const s = Math.max(2, tileSize - m * 2);

        ctx.fillStyle = isSelected ? '#f39c12' : '#e67e22';
        ctx.fillRect(px + m, py + m, s, s);

        ctx.strokeStyle = isSelected ? '#ffffff' : '#d35400';
        ctx.lineWidth = isSelected ? 2 : 1;
        ctx.strokeRect(px + m, py + m, s, s);

        // Exit target label / arrow
        if (tileSize >= 16) {
          ctx.fillStyle = '#ffffff';
          ctx.font = `bold ${Math.floor(tileSize * 0.55)}px sans-serif`;
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          const glyph = ex.direction === 'NORTH' ? '⬆️' : ex.direction === 'SOUTH' ? '⬇️' : ex.direction === 'WEST' ? '⬅️' : '➡️';
          ctx.fillText(glyph, px + tileSize / 2, py + tileSize / 2);
        }
      });
    }

    // 7. Render Objects / NPCs / Enemies
    if (showObjects && level.objects) {
      level.objects.forEach((obj, idx) => {
        const isSelected = activeLayer === 'objects' && selectedEntityIndex === idx;
        const px = obj.position.x * tileSize;
        const py = obj.position.y * tileSize;
        const wTiles = obj.sprite_width || 1;
        const hTiles = obj.sprite_height || 1;
        const objW = wTiles * tileSize;
        const objH = hTiles * tileSize;

        const tmpl = OBJECT_TEMPLATES.find((t) => t.type === obj.type);
        const color = tmpl ? tmpl.color : '#9b59b6';

        if (wTiles > 1 || hTiles > 1) {
          // ── Multi-tile object preview (sprite_width/height, 1-4) ──
          ctx.fillStyle = 'rgba(142, 68, 173, 0.25)';
          ctx.fillRect(px, py, objW, objH);

          {
            let spriteId: string | null = null;
            if (obj.animation_frames && obj.animation_frames.length > 0) {
              const frameKey = obj.animation_frames[animTick % obj.animation_frames.length];
              spriteId = frameKey;
            } else if (obj.overworld_sprite) {
              spriteId = obj.overworld_sprite;
            }

            const spriteImg = spriteId ? tileImages.get(spriteId) : null;
            if (spriteImg && spriteImg.complete && spriteImg.naturalWidth > 0) {
              ctx.drawImage(spriteImg, px + 4, py + 4, objW - 8, objH - 8);
            }
          }

          // Boss Frame
          ctx.strokeStyle = isSelected ? '#f59e0b' : '#a855f7';
          ctx.lineWidth = isSelected ? 3 : 2;
          ctx.strokeRect(px, py, objW, objH);

          // Header badge
          ctx.fillStyle = 'rgba(15, 23, 42, 0.85)';
          ctx.fillRect(px, py, Math.min(objW, 140), Math.min(objH, 20));
          ctx.fillStyle = '#f59e0b';
          ctx.font = `bold ${Math.max(9, Math.floor(tileSize * 0.45))}px monospace`;
          ctx.textAlign = 'left';
          ctx.textBaseline = 'middle';
          ctx.fillText(`👑 ${wTiles}×${hTiles} ${obj.properties?.display_name || obj.id}`, px + 4, py + 10);
        } else {
          // Standard 1x1 object
          ctx.fillStyle = color;
          ctx.beginPath();
          ctx.arc(px + tileSize / 2, py + tileSize / 2, Math.max(1.5, tileSize * 0.38), 0, Math.PI * 2);
          ctx.fill();

          ctx.strokeStyle = isSelected ? '#ffffff' : 'rgba(0,0,0,0.5)';
          ctx.lineWidth = isSelected ? 2 : 1;
          ctx.stroke();

          // Sprite image (animated or static) or fallback icon
          if (tileSize >= 16) {
            // Type-owned overworld art wins (Enemies view): shared sprite
            // everywhere this enemy appears, like the ROM.
            let owDrawn = false;
            if (obj.type === 'enemy') {
              const props = (obj as any).properties || {};
              const explicit = props.enemy_type ? String(props.enemy_type).toUpperCase() : '';
              const ent = String(props.entity_id || '');
              const conv = ent.startsWith('ENTITY_ID_') ? ent.slice('ENTITY_ID_'.length).toUpperCase() : '';
              const key = (explicit && enemyOwByName.get(explicit)) ? explicit : (conv && enemyOwByName.get(conv)) ? conv : '';
              const cells = key ? enemyOwByName.get(key) : null;
              const dims = key ? enemyOwDims.get(key) : undefined;
              if (cells && cells.length > 0) {
                const gw = dims?.w || 1;
                const gh = dims?.h || 1;
                const per = gw * gh;
                // Multi-tile sprites occupy gw x gh map tiles (like the
                // ROM's w*h OAM grid); single-tile sprites animate frames.
                const frames = Math.max(1, Math.floor(cells.length / Math.max(1, per)));
                const frameIdx = frames > 1 ? animTick % frames : 0;
                const base = frameIdx * per;
                const cw = (gw > 1 || gh > 1) ? tileSize : objW / Math.max(1, gw);
                const chh = (gw > 1 || gh > 1) ? tileSize : objH / Math.max(1, gh);
                for (let gy = 0; gy < gh; gy++) {
                  for (let gx = 0; gx < gw; gx++) {
                    const cell = cells[base + gy * gw + gx];
                    const img = cell ? owImgs.get(cell) : undefined;
                    if (img && img.complete && img.naturalWidth > 0) {
                      ctx.imageSmoothingEnabled = false;
                      ctx.drawImage(img, px + gx * cw, py + gy * chh, cw, chh);
                    }
                  }
                }
                owDrawn = true;
              }
            }
            let spriteId: string | null = null;
            if (!owDrawn && obj.animation_frames && obj.animation_frames.length > 0) {
              const frameKey = obj.animation_frames[animTick % obj.animation_frames.length];
              spriteId = frameKey;
            } else if (!owDrawn && obj.overworld_sprite) {
              spriteId = obj.overworld_sprite;
            }

            const spriteImg = spriteId ? tileImages.get(spriteId) : null;
            if (spriteImg && spriteImg.complete && spriteImg.naturalWidth > 0) {
              ctx.imageSmoothingEnabled = false;
              ctx.drawImage(spriteImg, px + 2, py + 2, tileSize - 4, tileSize - 4);
            } else {
              ctx.font = `${Math.floor(tileSize * 0.45)}px sans-serif`;
              ctx.textAlign = 'center';
              ctx.textBaseline = 'middle';
              ctx.fillText(tmpl?.icon || '👾', px + tileSize / 2, py + tileSize / 2);
            }
          }
        }
      });
    }

    // 8. Render Player Spawn
    if (level.spawn) {
      const sp = level.spawn;
      const px = sp.x * tileSize;
      const py = sp.y * tileSize;
      const isSelected = activeLayer === 'spawn';

      ctx.fillStyle = '#2ecc71';
      ctx.beginPath();
      ctx.arc(px + tileSize / 2, py + tileSize / 2, Math.max(2, tileSize * 0.42), 0, Math.PI * 2);
      ctx.fill();

      ctx.strokeStyle = isSelected ? '#ffffff' : '#27ae60';
      ctx.lineWidth = isSelected ? 2 : 1;
      ctx.stroke();

      if (tileSize >= 16) {
        let heroSpriteId: string | null = null;
        if (sp.animation_frames && sp.animation_frames.length > 0) {
          const frameKey = sp.animation_frames[animTick % sp.animation_frames.length];
          heroSpriteId = frameKey;
        }
        const heroImg = heroSpriteId ? tileImages.get(heroSpriteId) : null;
        if (heroImg && heroImg.complete && heroImg.naturalWidth > 0) {
          ctx.imageSmoothingEnabled = false;
          ctx.drawImage(heroImg, px + 2, py + 2, tileSize - 4, tileSize - 4);
        } else {
          ctx.font = `bold ${Math.floor(tileSize * 0.45)}px sans-serif`;
          ctx.fillStyle = '#ffffff';
          ctx.textAlign = 'center';
          ctx.textBaseline = 'middle';
          ctx.fillText('🧙', px + tileSize / 2, py + tileSize / 2);
        }
      }
    }

    // 9. Render Hover Cursor
    if (hoverTile) {
      const hpx = hoverTile.x * tileSize;
      const hpy = hoverTile.y * tileSize;

      ctx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
      ctx.lineWidth = 2;
      ctx.strokeRect(hpx, hpy, tileSize, tileSize);
    }
  }, [
    level,
    activeTool,
    activeLayer,
    selectedTileId,
    zoom,
    showGrid,
    showCollision,
    showTerrain,
    showExits,
    showObjects,
    showRegions,
    selectedEntityIndex,
    isMouseDown,
    dragStartTile,
    hoverTile,
    canvasWidth,
    canvasHeight,
    tileSize,
    tileMap,
    tileImages,
    animTick,
    clonePattern,
    isCapturingClone,
    cardSkin,
    hudSkin,
    titleFontImgs,
    titleLogoImg,
  ]);

  useEffect(() => {
    render();
  }, [render]);

  // Mouse Handlers
  const handleMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const coords = getTileCoords(e);
    if (!coords) return;

    setIsMouseDown(true);
    setDragStartTile(coords);

    // Spawn mode
    if (activeLayer === 'spawn') {
      onSpawnMoved(coords.x, coords.y);
      return;
    }

    // Select / Move Mode
    if (activeTool === 'select' || activeLayer === 'objects' || activeLayer === 'exits') {
      // Check if clicked an object (multi-tile bounds for 1-4 sized objects)
      const objIndex = level.objects.findIndex((o) => {
        const ow = o.sprite_width || 1;
        const oh = o.sprite_height || 1;
        return (
          coords.x >= o.position.x &&
          coords.x < o.position.x + ow &&
          coords.y >= o.position.y &&
          coords.y < o.position.y + oh
        );
      });
      if (objIndex !== -1) {
        onSelectEntityIndex(objIndex);
        setDraggingEntity({ type: 'object', index: objIndex });
        return;
      }

      // Check if clicked an exit
      const exitIndex = level.exits.findIndex((ex) => ex.x === coords.x && ex.y === coords.y);
      if (exitIndex !== -1) {
        onSelectEntityIndex(exitIndex);
        setDraggingEntity({ type: 'exit', index: exitIndex });
        return;
      }

      // Check if clicked player spawn
      if (level.spawn.x === coords.x && level.spawn.y === coords.y) {
        setDraggingEntity({ type: 'spawn', index: 0 });
        return;
      }

      // Check if clicked a region
      const regIndex = level.regions.findIndex(
        (r) =>
          coords.x >= r.bounds.x &&
          coords.x < r.bounds.x + r.bounds.width &&
          coords.y >= r.bounds.y &&
          coords.y < r.bounds.y + r.bounds.height
      );
      if (regIndex !== -1) {
        onSelectEntityIndex(regIndex);
        return;
      }

      onSelectEntityIndex(null);
    }

    // Drawing Tools
    if (activeLayer === 'terrain') {
      if (activeTool === 'brush') {
        onTilePainted(coords.x, coords.y, selectedTileId);
      } else if (activeTool === 'eraser') {
        onTilePainted(coords.x, coords.y, defaultFloorTile);
      } else if (activeTool === 'fill') {
        onFill(coords.x, coords.y, selectedTileId);
      } else if (activeTool === 'eyedropper') {
        const picked = level.grid[coords.y]?.[coords.x] || defaultFloorTile;
        onTilePicked(picked);
      } else if (activeTool === 'clone') {
        const isCaptureAction = e.shiftKey || e.altKey || e.button === 2 || !clonePattern || clonePattern.length === 0;
        if (isCaptureAction) {
          setIsCapturingClone(true);
        } else if (clonePattern && onStampPattern) {
          onStampPattern(coords.x, coords.y, clonePattern);
        }
      }
    }
  };

  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const coords = getTileCoords(e);
    setHoverTile(coords);

    if (!isMouseDown || !coords) return;

    // Entity dragging
    if (draggingEntity) {
      if (draggingEntity.type === 'object') {
        onObjectMoved(draggingEntity.index, coords.x, coords.y);
      } else if (draggingEntity.type === 'exit') {
        onExitMoved(draggingEntity.index, coords.x, coords.y);
      } else if (draggingEntity.type === 'spawn') {
        onSpawnMoved(coords.x, coords.y);
      }
      return;
    }

    // Continuous brush painting
    if (activeLayer === 'terrain') {
      if (activeTool === 'brush') {
        onTilePainted(coords.x, coords.y, selectedTileId);
      } else if (activeTool === 'eraser') {
        onTilePainted(coords.x, coords.y, defaultFloorTile);
      } else if (activeTool === 'clone' && !isCapturingClone && clonePattern && onStampPattern) {
        onStampPattern(coords.x, coords.y, clonePattern);
      }
    }
  };

  const handleMouseUp = () => {
    if (isMouseDown && activeTool === 'rect' && dragStartTile && hoverTile && activeLayer === 'terrain') {
      const rx = Math.min(dragStartTile.x, hoverTile.x);
      const ry = Math.min(dragStartTile.y, hoverTile.y);
      const rw = Math.abs(dragStartTile.x - hoverTile.x) + 1;
      const rh = Math.abs(dragStartTile.y - hoverTile.y) + 1;
      onRectPainted(rx, ry, rw, rh, selectedTileId);
    }

    if (isMouseDown && activeTool === 'clone' && isCapturingClone && dragStartTile && hoverTile && activeLayer === 'terrain') {
      const rx = Math.min(dragStartTile.x, hoverTile.x);
      const ry = Math.min(dragStartTile.y, hoverTile.y);
      const rw = Math.abs(dragStartTile.x - hoverTile.x) + 1;
      const rh = Math.abs(dragStartTile.y - hoverTile.y) + 1;

      const pattern: string[][] = [];
      for (let y = ry; y < ry + rh; y++) {
        const row: string[] = [];
        for (let x = rx; x < rx + rw; x++) {
          row.push(level.grid[y]?.[x] || defaultFloorTile);
        }
        pattern.push(row);
      }
      if (onClonePatternCaptured) {
        onClonePatternCaptured(pattern);
      }
    }

    setIsMouseDown(false);
    setIsCapturingClone(false);
    setDragStartTile(null);
    setDraggingEntity(null);
  };

  return (
    <div className="map-canvas-container">
      <div className="canvas-wrapper">
        <canvas
          ref={canvasRef}
          width={canvasWidth}
          height={canvasHeight}
          className={`map-canvas tool-${activeTool}`}
          onMouseDown={handleMouseDown}
          onMouseMove={handleMouseMove}
          onMouseUp={handleMouseUp}
          onContextMenu={(e) => {
            if (activeTool === 'clone') e.preventDefault();
          }}
          onMouseLeave={() => {
            setIsMouseDown(false);
            setIsCapturingClone(false);
            setHoverTile(null);
            setDraggingEntity(null);
          }}
        />
      </div>
      <div className="canvas-footer-status">
        <span>
          Cursor: {hoverTile ? `(${hoverTile.x}, ${hoverTile.y})` : '—'}
        </span>
        <span>
          Tile: {hoverTile ? level.grid[hoverTile.y]?.[hoverTile.x] || defaultFloorTile : '—'}
        </span>
        <span>
          Dimensions: {level.width} × {level.height}
        </span>
        <span>
          Zoom: {Math.round(zoom * 100)}%
        </span>
        {activeTool === 'clone' && (
          <span style={{ color: '#00f0ff', fontWeight: 500 }}>
            {clonePattern && clonePattern.length > 0
              ? `📋 Stamp ${clonePattern[0].length}×${clonePattern.length}: Click to stamp • Shift+Drag or Right-Click to copy new`
              : '📋 Drag a box over map tiles to copy'}
          </span>
        )}
      </div>
    </div>
  );
};
