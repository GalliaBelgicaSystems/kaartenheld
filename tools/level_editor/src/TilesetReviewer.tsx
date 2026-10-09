import React, { useState, useEffect, useRef } from 'react';
import { BUILTIN_TILESETS, TileDefinition, TilesetDefinition, saveTilesetToServer } from './model/Tileset';

interface TilesetReviewerProps {
  initialTilesetId?: string;
  onClose: () => void;
  onTilesetUpdated?: (tileset: TilesetDefinition) => void;
}

export const TilesetReviewer: React.FC<TilesetReviewerProps> = ({
  initialTilesetId = 'forest',
  onClose,
  onTilesetUpdated,
}) => {
  // Current active tileset
  const [tilesetId, setTilesetId] = useState<string>(initialTilesetId);
  const [tileset, setTileset] = useState<TilesetDefinition>(() => {
    const orig = BUILTIN_TILESETS[initialTilesetId] || BUILTIN_TILESETS.forest;
    return JSON.parse(JSON.stringify(orig));
  });

  const [selectedTileIndex, setSelectedTileIndex] = useState<number>(0);
  const [zoom, setZoom] = useState<number>(8);
  const [showPixelGrid, setShowPixelGrid] = useState<boolean>(true);
  const [filterMode, setFilterMode] = useState<'all' | 'walkable' | 'solid'>('all');
  const [searchQuery, setSearchQuery] = useState<string>('');
  const [isSaving, setIsSaving] = useState<boolean>(false);
  const [notification, setNotification] = useState<{ message: string; type: 'success' | 'error' } | null>(null);

  // Import PNG state
  const [isImporting, setIsImporting] = useState<boolean>(false);
  const [importId, setImportId] = useState<string>('custom_tileset');
  const [importLabel, setImportLabel] = useState<string>('Custom Tileset');
  const [importTileSize, setImportTileSize] = useState<number>(8);
  const [importImages, setImportImages] = useState<Record<string, string>>({});
  const fileInputRef = useRef<HTMLInputElement | null>(null);

  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  // Switch tileset
  const handleSelectTileset = (newId: string) => {
    setTilesetId(newId);
    const orig = BUILTIN_TILESETS[newId] || BUILTIN_TILESETS.forest;
    setTileset(JSON.parse(JSON.stringify(orig)));
    setSelectedTileIndex(0);
    setIsImporting(false);
  };

  // Filtered tiles
  const filteredIndices = tileset.tiles
    .map((tile, index) => ({ tile, index }))
    .filter(({ tile }) => {
      if (filterMode === 'walkable' && !tile.walkable) return false;
      if (filterMode === 'solid' && tile.walkable) return false;
      if (searchQuery.trim()) {
        const q = searchQuery.toLowerCase();
        return (
          tile.label.toLowerCase().includes(q) ||
          tile.id.toLowerCase().includes(q) ||
          tile.gb_constant.toLowerCase().includes(q)
        );
      }
      return true;
    })
    .map(({ index }) => index);

  const activeTile: TileDefinition | undefined = tileset.tiles[selectedTileIndex];

  // Auto-dismiss notification
  useEffect(() => {
    if (notification) {
      const timer = setTimeout(() => setNotification(null), 4000);
      return () => clearTimeout(timer);
    }
  }, [notification]);

  // Update active tile property
  const updateActiveTile = (updates: Partial<TileDefinition>) => {
    if (selectedTileIndex < 0 || selectedTileIndex >= tileset.tiles.length) return;
    const newTiles = [...tileset.tiles];
    newTiles[selectedTileIndex] = { ...newTiles[selectedTileIndex], ...updates };
    setTileset({ ...tileset, tiles: newTiles });
  };

  // Toggle active tile walkability
  const toggleWalkability = () => {
    if (!activeTile) return;
    updateActiveTile({
      walkable: !activeTile.walkable,
      ascii: !activeTile.walkable ? '.' : '#',
    });
  };

  // Keyboard navigation
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (
        e.target instanceof HTMLInputElement ||
        e.target instanceof HTMLTextAreaElement ||
        e.target instanceof HTMLSelectElement
      ) {
        return;
      }

      if (e.key === 'ArrowLeft') {
        e.preventDefault();
        handlePrevTile();
      } else if (e.key === 'ArrowRight') {
        e.preventDefault();
        handleNextTile();
      } else if (e.key === ' ' || e.key.toLowerCase() === 'w') {
        e.preventDefault();
        toggleWalkability();
      } else if (e.key === 'Escape') {
        e.preventDefault();
        onClose();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [selectedTileIndex, tileset, filteredIndices]);

  const handlePrevTile = () => {
    if (filteredIndices.length === 0) return;
    const currentFilteredPos = filteredIndices.indexOf(selectedTileIndex);
    if (currentFilteredPos > 0) {
      setSelectedTileIndex(filteredIndices[currentFilteredPos - 1]);
    } else {
      setSelectedTileIndex(filteredIndices[filteredIndices.length - 1]);
    }
  };

  const handleNextTile = () => {
    if (filteredIndices.length === 0) return;
    const currentFilteredPos = filteredIndices.indexOf(selectedTileIndex);
    if (currentFilteredPos !== -1 && currentFilteredPos < filteredIndices.length - 1) {
      setSelectedTileIndex(filteredIndices[currentFilteredPos + 1]);
    } else {
      setSelectedTileIndex(filteredIndices[0]);
    }
  };

  // Draw tile on zoomed canvas
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !activeTile) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const img = new Image();
    img.src = activeTile.image_url;
    img.onload = () => {
      const baseW = img.naturalWidth || 8;
      const baseH = img.naturalHeight || 8;
      canvas.width = baseW * zoom;
      canvas.height = baseH * zoom;

      ctx.imageSmoothingEnabled = false;
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.drawImage(img, 0, 0, canvas.width, canvas.height);

      // Pixel Grid Overlay
      if (showPixelGrid && zoom >= 4) {
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.18)';
        ctx.lineWidth = 1;
        for (let x = 0; x <= baseW; x++) {
          ctx.beginPath();
          ctx.moveTo(x * zoom + 0.5, 0);
          ctx.lineTo(x * zoom + 0.5, canvas.height);
          ctx.stroke();
        }
        for (let y = 0; y <= baseH; y++) {
          ctx.beginPath();
          ctx.moveTo(0, y * zoom + 0.5);
          ctx.lineTo(canvas.width, y * zoom + 0.5);
          ctx.stroke();
        }
      }
    };
    img.onerror = () => {
      canvas.width = 8 * zoom;
      canvas.height = 8 * zoom;
      ctx.fillStyle = activeTile.color || '#34495e';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
    };
  }, [activeTile, zoom, showPixelGrid]);

  // Save changes to disk
  const handleSave = async () => {
    setIsSaving(true);
    const res = await saveTilesetToServer(tileset, Object.keys(importImages).length > 0 ? importImages : undefined);
    setIsSaving(false);
    if (res.success) {
      setNotification({ message: `Successfully saved "${tileset.label}" properties!`, type: 'success' });
      onTilesetUpdated?.(tileset);
    } else {
      setNotification({ message: `Failed to save tileset: ${res.error}`, type: 'error' });
    }
  };

  // Import PNG handler
  const handleImportPngFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (event) => {
      const dataUrl = event.target?.result as string;
      const img = new Image();
      img.onload = () => {
        const sliceSize = importTileSize;
        const cols = Math.floor(img.width / sliceSize);
        const rows = Math.floor(img.height / sliceSize);

        if (cols === 0 || rows === 0) {
          alert(`Image dimensions (${img.width}x${img.height}) are too small for ${sliceSize}x${sliceSize} tiles.`);
          return;
        }

        const canvas = document.createElement('canvas');
        canvas.width = sliceSize;
        canvas.height = sliceSize;
        const ctx = canvas.getContext('2d')!;

        const newTiles: TileDefinition[] = [];
        const extractedImages: Record<string, string> = {};

        for (let r = 0; r < rows; r++) {
          for (let c = 0; c < cols; c++) {
            ctx.clearRect(0, 0, sliceSize, sliceSize);
            ctx.drawImage(img, c * sliceSize, r * sliceSize, sliceSize, sliceSize, 0, 0, sliceSize, sliceSize);
            const tileDataUrl = canvas.toDataURL('image/png');
            const tileId = `${importId}_${r}_${c}`;

            extractedImages[tileId] = tileDataUrl;
            newTiles.push({
              id: tileId,
              label: `Tile (${c}, ${r})`,
              gb_constant: `TILE_${importId.toUpperCase()}_${r}_${c}`,
              walkable: false,
              color: '#7f8c8d',
              ascii: '#',
              image_url: tileDataUrl,
              category: 'terrain',
            });
          }
        }

        setImportImages(extractedImages);
        const newTilesetDef: TilesetDefinition = {
          id: importId,
          label: importLabel,
          gb_tileset_kind: `WORLD_TILESET_${importId.toUpperCase()}`,
          tiles: newTiles,
        };
        setTileset(newTilesetDef);
        setTilesetId(importId);
        setSelectedTileIndex(0);
        setIsImporting(false);
        setNotification({
          message: `Imported ${newTiles.length} tiles from PNG! Review properties below and click Save.`,
          type: 'success',
        });
      };
      img.src = dataUrl;
    };
    reader.readAsDataURL(file);
  };

  return (
    <div className="tileset-reviewer-overlay" onClick={onClose}>
      <div
        className="tileset-reviewer-modal"
        onClick={(e) => e.stopPropagation()}
        style={{ width: '92vw', maxWidth: '1180px', height: '88vh', display: 'flex', flexDirection: 'column' }}
      >
        {/* Modal Header */}
        <div className="modal-header" style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '12px 20px', borderBottom: '1px solid var(--border-color)' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '14px' }}>
            <span style={{ fontSize: '18px', fontWeight: 'bold', color: '#00f0ff' }}>
              🎨 Tileset Asset Reviewer & Importer
            </span>
            <select
              value={tilesetId}
              onChange={(e) => handleSelectTileset(e.target.value)}
              style={{
                background: 'var(--bg-secondary)',
                color: 'var(--text-primary)',
                border: '1px solid var(--border-color)',
                padding: '6px 12px',
                borderRadius: '4px',
                fontWeight: 'bold',
                fontSize: '13px'
              }}
            >
              {Object.keys(BUILTIN_TILESETS).map((key) => (
                <option key={key} value={key}>
                  {BUILTIN_TILESETS[key].label} ({BUILTIN_TILESETS[key].tiles.length} tiles)
                </option>
              ))}
            </select>
          </div>

          <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
            <button
              className="btn"
              style={{ background: '#8e44ad', color: '#fff' }}
              onClick={() => setIsImporting(!isImporting)}
              title="Import a new tileset from PNG image"
            >
              📁 {isImporting ? 'Cancel Import' : 'Import PNG Tileset'}
            </button>
            <button
              className="btn btn-success"
              onClick={handleSave}
              disabled={isSaving}
              style={{ fontWeight: 'bold' }}
              title="Save all changes to disk"
            >
              {isSaving ? 'Saving...' : '💾 Save Tileset'}
            </button>
            <button className="btn-icon" onClick={onClose} title="Close (Escape)">
              ✕
            </button>
          </div>
        </div>

        {/* Notification Toast */}
        {notification && (
          <div
            style={{
              padding: '8px 16px',
              backgroundColor: notification.type === 'success' ? '#27ae60' : '#c0392b',
              color: '#fff',
              fontSize: '13px',
              fontWeight: 'bold',
              display: 'flex',
              justifyContent: 'space-between',
              alignItems: 'center'
            }}
          >
            <span>{notification.message}</span>
            <button onClick={() => setNotification(null)} style={{ background: 'transparent', border: 'none', color: '#fff', cursor: 'pointer' }}>✕</button>
          </div>
        )}

        {/* Import Drawer */}
        {isImporting && (
          <div style={{ background: '#1c2430', padding: '14px 20px', borderBottom: '1px solid var(--border-color)', display: 'flex', alignItems: 'center', gap: '16px', flexWrap: 'wrap' }}>
            <span style={{ fontWeight: 'bold', color: '#f39c12' }}>Import Tileset from PNG:</span>
            <label style={{ fontSize: '12px' }}>
              ID: <input type="text" value={importId} onChange={(e) => setImportId(e.target.value.toLowerCase().replace(/[^a-z0-9_]/g, '_'))} style={{ width: '120px', padding: '3px 6px', background: '#0f141c', color: '#fff', border: '1px solid #444', borderRadius: '3px' }} />
            </label>
            <label style={{ fontSize: '12px' }}>
              Label: <input type="text" value={importLabel} onChange={(e) => setImportLabel(e.target.value)} style={{ width: '140px', padding: '3px 6px', background: '#0f141c', color: '#fff', border: '1px solid #444', borderRadius: '3px' }} />
            </label>
            <label style={{ fontSize: '12px' }}>
              Tile Size:
              <select value={importTileSize} onChange={(e) => setImportTileSize(Number(e.target.value))} style={{ marginLeft: '6px', padding: '3px 6px', background: '#0f141c', color: '#fff', border: '1px solid #444', borderRadius: '3px' }}>
                <option value={8}>8×8 px (Native GB)</option>
                <option value={16}>16×16 px (RPG / GBC)</option>
                <option value={32}>32×32 px (HD)</option>
              </select>
            </label>
            <input type="file" accept="image/png" ref={fileInputRef} onChange={handleImportPngFile} style={{ display: 'none' }} />
            <button className="btn btn-primary" onClick={() => fileInputRef.current?.click()}>
              Choose PNG File...
            </button>
          </div>
        )}

        {/* Filter & Stats Toolbar */}
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '8px 20px', background: 'var(--bg-secondary)', borderBottom: '1px solid var(--border-color)', fontSize: '12px' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
            <span style={{ color: 'var(--text-muted)' }}>
              Showing {filteredIndices.length} of {tileset.tiles.length} tiles
              ({tileset.tiles.filter(t => t.walkable).length} walkable, {tileset.tiles.filter(t => !t.walkable).length} solid)
            </span>
            <div style={{ display: 'flex', gap: '4px' }}>
              <button
                className={`btn btn-sm ${filterMode === 'all' ? 'btn-primary' : ''}`}
                onClick={() => setFilterMode('all')}
                style={{ padding: '2px 8px' }}
              >
                All
              </button>
              <button
                className={`btn btn-sm ${filterMode === 'walkable' ? 'btn-primary' : ''}`}
                onClick={() => setFilterMode('walkable')}
                style={{ padding: '2px 8px' }}
              >
                🟢 Walkable ({tileset.tiles.filter(t => t.walkable).length})
              </button>
              <button
                className={`btn btn-sm ${filterMode === 'solid' ? 'btn-primary' : ''}`}
                onClick={() => setFilterMode('solid')}
                style={{ padding: '2px 8px' }}
              >
                🔴 Solid Obstacles ({tileset.tiles.filter(t => !t.walkable).length})
              </button>
            </div>
          </div>

          <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
            <input
              type="text"
              placeholder="Search tiles (name, constant)..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              style={{
                background: 'var(--bg-primary)',
                color: 'var(--text-primary)',
                border: '1px solid var(--border-color)',
                padding: '4px 8px',
                borderRadius: '4px',
                fontSize: '12px',
                width: '180px'
              }}
            />
          </div>
        </div>

        {/* Thumbnail Strip / Gallery */}
        <div
          style={{
            display: 'flex',
            gap: '8px',
            padding: '10px 20px',
            overflowX: 'auto',
            background: '#090d13',
            borderBottom: '1px solid var(--border-color)',
            minHeight: '74px',
            alignItems: 'center'
          }}
        >
          {filteredIndices.map((idx) => {
            const t = tileset.tiles[idx];
            const isSelected = idx === selectedTileIndex;
            return (
              <div
                key={t.id + '_' + idx}
                onClick={() => setSelectedTileIndex(idx)}
                style={{
                  position: 'relative',
                  width: '48px',
                  height: '48px',
                  flexShrink: 0,
                  cursor: 'pointer',
                  border: isSelected ? '2px solid #00f0ff' : '1px solid #2a3648',
                  borderRadius: '4px',
                  background: '#131b26',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  boxShadow: isSelected ? '0 0 8px rgba(0, 240, 255, 0.4)' : 'none',
                  transition: 'all 0.1s'
                }}
                title={`${t.label} (${t.walkable ? 'Walkable' : 'Solid Obstacle'})`}
              >
                <img
                  src={t.image_url}
                  alt={t.id}
                  style={{
                    width: '36px',
                    height: '36px',
                    imageRendering: 'pixelated',
                    objectFit: 'contain'
                  }}
                />
                {/* Walkable Indicator Badge */}
                <div
                  style={{
                    position: 'absolute',
                    top: '2px',
                    right: '2px',
                    width: '8px',
                    height: '8px',
                    borderRadius: '50%',
                    background: t.walkable ? '#2ecc71' : '#e74c3c',
                    boxShadow: '0 0 2px rgba(0,0,0,0.8)'
                  }}
                />
              </div>
            );
          })}
        </div>

        {/* Main Body: Left Zoom Viewport & Right Property Panel */}
        <div style={{ flex: 1, display: 'flex', overflow: 'hidden' }}>
          {/* Left Column: Big Zoomable Tile Preview */}
          <div
            style={{
              flex: '0 0 460px',
              borderRight: '1px solid var(--border-color)',
              display: 'flex',
              flexDirection: 'column',
              background: '#0e141d',
              padding: '16px 20px',
              justifyContent: 'space-between'
            }}
          >
            {/* Viewport Header & Zoom controls */}
            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' }}>
                <span style={{ fontWeight: 'bold', fontSize: '14px', color: 'var(--text-primary)' }}>
                  Tile Preview & Inspection
                </span>
                <div style={{ display: 'flex', gap: '4px', alignItems: 'center' }}>
                  {[2, 4, 8, 12, 16].map((z) => (
                    <button
                      key={z}
                      className={`btn btn-sm ${zoom === z ? 'btn-primary' : ''}`}
                      onClick={() => setZoom(z)}
                      style={{ padding: '2px 6px', fontSize: '11px', minWidth: '30px' }}
                    >
                      {z}×
                    </button>
                  ))}
                </div>
              </div>

              {/* Checkboxes */}
              <div style={{ display: 'flex', gap: '16px', marginBottom: '12px', fontSize: '12px', color: 'var(--text-muted)' }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: '6px', cursor: 'pointer' }}>
                  <input
                    type="checkbox"
                    checked={showPixelGrid}
                    onChange={(e) => setShowPixelGrid(e.target.checked)}
                  />
                  Show Pixel Grid
                </label>
              </div>
            </div>

            {/* Canvas Stage */}
            <div
              style={{
                flex: 1,
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                background: 'repeating-conic-gradient(#151d28 0% 25%, #1a2432 0% 50%) 50% / 16px 16px',
                borderRadius: '6px',
                border: '1px solid var(--border-color)',
                overflow: 'hidden',
                position: 'relative'
              }}
            >
              {activeTile ? (
                <canvas
                  ref={canvasRef}
                  style={{
                    imageRendering: 'pixelated',
                    boxShadow: '0 4px 16px rgba(0,0,0,0.6)',
                    border: '1px solid rgba(255,255,255,0.1)'
                  }}
                />
              ) : (
                <span style={{ color: 'var(--text-muted)' }}>No tile selected</span>
              )}
            </div>

            {/* Navigation & Shortcuts Footer */}
            <div style={{ marginTop: '16px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px' }}>
                <button className="btn" onClick={handlePrevTile} style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
                  ◀ Previous (←)
                </button>
                <span style={{ fontSize: '12px', color: 'var(--text-muted)', fontFamily: 'monospace' }}>
                  {filteredIndices.indexOf(selectedTileIndex) + 1} / {filteredIndices.length}
                </span>
                <button className="btn" onClick={handleNextTile} style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
                  Next (→) ▶
                </button>
              </div>

              <div style={{ textAlign: 'center', fontSize: '11px', color: 'var(--text-muted)' }}>
                💡 Tip: Press <kbd style={{ background: '#222', padding: '1px 5px', borderRadius: '3px', color: '#00f0ff' }}>Space</kbd> or <kbd style={{ background: '#222', padding: '1px 5px', borderRadius: '3px', color: '#00f0ff' }}>W</kbd> to toggle walkability instantly!
              </div>
            </div>
          </div>

          {/* Right Column: Tile Properties Editor */}
          <div
            style={{
              flex: 1,
              overflowY: 'auto',
              background: 'var(--bg-primary)',
              padding: '20px 24px',
              display: 'flex',
              flexDirection: 'column',
              gap: '16px'
            }}
          >
            {activeTile ? (
              <>
                {/* 1. HERO WALKABILITY TOGGLE CARD */}
                <div
                  onClick={toggleWalkability}
                  style={{
                    cursor: 'pointer',
                    padding: '16px 20px',
                    borderRadius: '8px',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'space-between',
                    background: activeTile.walkable
                      ? 'linear-gradient(135deg, rgba(46, 204, 113, 0.15), rgba(39, 174, 96, 0.25))'
                      : 'linear-gradient(135deg, rgba(231, 76, 60, 0.15), rgba(192, 57, 43, 0.25))',
                    border: activeTile.walkable ? '2px solid #2ecc71' : '2px solid #e74c3c',
                    boxShadow: activeTile.walkable
                      ? '0 0 12px rgba(46, 204, 113, 0.25)'
                      : '0 0 12px rgba(231, 76, 60, 0.25)',
                    transition: 'all 0.15s ease'
                  }}
                  title="Click or press Space / W to toggle walkability"
                >
                  <div>
                    <div style={{ fontSize: '16px', fontWeight: 'bold', color: activeTile.walkable ? '#2ecc71' : '#e74c3c', display: 'flex', alignItems: 'center', gap: '8px' }}>
                      {activeTile.walkable ? '🟢 PASSABLE / WALKABLE' : '⛔ SOLID OBSTACLE / BLOCKED'}
                    </div>
                    <div style={{ fontSize: '12px', color: 'var(--text-muted)', marginTop: '4px' }}>
                      {activeTile.walkable
                        ? 'Player and NPCs can walk freely through this floor tile.'
                        : 'Impassable collision barrier (wall, tree trunk, obstacle, water, prop).'}
                    </div>
                  </div>

                  <div
                    style={{
                      padding: '6px 14px',
                      background: activeTile.walkable ? '#27ae60' : '#c0392b',
                      color: '#ffffff',
                      borderRadius: '4px',
                      fontWeight: 'bold',
                      fontSize: '13px'
                    }}
                  >
                    {activeTile.walkable ? 'Passable (Floor)' : 'Solid (Collision)'}
                  </div>
                </div>

                {/* 2. IDENTITY SECTION */}
                <div style={{ background: 'var(--bg-secondary)', padding: '14px 16px', borderRadius: '6px', border: '1px solid var(--border-color)' }}>
                  <div style={{ fontSize: '13px', fontWeight: 'bold', color: 'var(--text-primary)', marginBottom: '10px' }}>
                    Tile Identity
                  </div>
                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px' }}>
                    <div>
                      <label style={{ fontSize: '11px', color: 'var(--text-muted)', display: 'block', marginBottom: '4px' }}>
                        Display Label
                      </label>
                      <input
                        type="text"
                        value={activeTile.label}
                        onChange={(e) => updateActiveTile({ label: e.target.value })}
                        style={{ width: '100%', padding: '6px 10px', background: 'var(--bg-primary)', color: '#fff', border: '1px solid var(--border-color)', borderRadius: '4px', fontSize: '13px' }}
                      />
                    </div>
                    <div>
                      <label style={{ fontSize: '11px', color: 'var(--text-muted)', display: 'block', marginBottom: '4px' }}>
                        Semantic Tile ID
                      </label>
                      <input
                        type="text"
                        value={activeTile.id}
                        onChange={(e) => updateActiveTile({ id: e.target.value })}
                        style={{ width: '100%', padding: '6px 10px', background: 'var(--bg-primary)', color: '#fff', border: '1px solid var(--border-color)', borderRadius: '4px', fontSize: '13px', fontFamily: 'monospace' }}
                      />
                    </div>
                  </div>
                </div>

                {/* 3. GAME BOY ENGINE ATTRIBUTES */}
                <div style={{ background: 'var(--bg-secondary)', padding: '14px 16px', borderRadius: '6px', border: '1px solid var(--border-color)' }}>
                  <div style={{ fontSize: '13px', fontWeight: 'bold', color: 'var(--text-primary)', marginBottom: '10px' }}>
                    Game Boy ROM Engine Constants
                  </div>

                  <div style={{ marginBottom: '10px' }}>
                    <label style={{ fontSize: '11px', color: 'var(--text-muted)', display: 'block', marginBottom: '4px' }}>
                      GB Constant (C enum)
                    </label>
                    <input
                      type="text"
                      value={activeTile.gb_constant}
                      onChange={(e) => updateActiveTile({ gb_constant: e.target.value })}
                      style={{ width: '100%', padding: '6px 10px', background: 'var(--bg-primary)', color: '#00f0ff', border: '1px solid var(--border-color)', borderRadius: '4px', fontSize: '13px', fontFamily: 'monospace' }}
                    />
                    {/* Quick Suggestions */}
                    <div style={{ display: 'flex', gap: '6px', marginTop: '6px', flexWrap: 'wrap' }}>
                      {['TILE_FLOOR', 'TILE_WALL', 'TILE_TREE', 'TILE_BUILDING', 'TILE_DOOR'].map((c) => (
                        <button
                          key={c}
                          className="btn btn-sm"
                          style={{ fontSize: '10px', padding: '2px 6px' }}
                          onClick={() => updateActiveTile({ gb_constant: c, walkable: c === 'TILE_FLOOR' })}
                        >
                          {c}
                        </button>
                      ))}
                    </div>
                  </div>

                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '12px' }}>
                    <div>
                      <label style={{ fontSize: '11px', color: 'var(--text-muted)', display: 'block', marginBottom: '4px' }}>
                        ASCII Map Character
                      </label>
                      <input
                        type="text"
                        maxLength={1}
                        value={activeTile.ascii || (activeTile.walkable ? '.' : '#')}
                        onChange={(e) => updateActiveTile({ ascii: e.target.value })}
                        style={{ width: '60px', padding: '6px 10px', textAlign: 'center', background: 'var(--bg-primary)', color: '#f1c40f', border: '1px solid var(--border-color)', borderRadius: '4px', fontSize: '14px', fontFamily: 'monospace', fontWeight: 'bold' }}
                      />
                    </div>
                    <div>
                      <label style={{ fontSize: '11px', color: 'var(--text-muted)', display: 'block', marginBottom: '4px' }}>
                        Category
                      </label>
                      <select
                        value={activeTile.category || 'terrain'}
                        onChange={(e) => updateActiveTile({ category: e.target.value as any })}
                        style={{ width: '100%', padding: '6px 10px', background: 'var(--bg-primary)', color: '#fff', border: '1px solid var(--border-color)', borderRadius: '4px', fontSize: '13px' }}
                      >
                        <option value="terrain">Terrain</option>
                        <option value="wall">Wall / Structure</option>
                        <option value="nature">Nature / Tree</option>
                        <option value="building">Building</option>
                        <option value="object">Object / Prop</option>
                        <option value="ui">UI / Screen</option>
                      </select>
                    </div>
                  </div>
                </div>

                {/* 4. VISUAL & ASSET DETAILS */}
                <div style={{ background: 'var(--bg-secondary)', padding: '14px 16px', borderRadius: '6px', border: '1px solid var(--border-color)' }}>
                  <div style={{ fontSize: '13px', fontWeight: 'bold', color: 'var(--text-primary)', marginBottom: '10px' }}>
                    Visual & Asset Details
                  </div>
                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 2fr', gap: '12px', alignItems: 'center' }}>
                    <div>
                      <label style={{ fontSize: '11px', color: 'var(--text-muted)', display: 'block', marginBottom: '4px' }}>
                        Fallback Color
                      </label>
                      <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                        <input
                          type="color"
                          value={activeTile.color || '#7f8c8d'}
                          onChange={(e) => updateActiveTile({ color: e.target.value })}
                          style={{ width: '36px', height: '36px', border: 'none', background: 'transparent', cursor: 'pointer' }}
                        />
                        <span style={{ fontSize: '12px', fontFamily: 'monospace', color: 'var(--text-primary)' }}>
                          {activeTile.color}
                        </span>
                      </div>
                    </div>
                    <div>
                      <label style={{ fontSize: '11px', color: 'var(--text-muted)', display: 'block', marginBottom: '4px' }}>
                        Image Asset Path
                      </label>
                      <input
                        type="text"
                        readOnly
                        value={activeTile.image_url}
                        style={{ width: '100%', padding: '6px 10px', background: 'var(--bg-primary)', color: 'var(--text-muted)', border: '1px solid var(--border-color)', borderRadius: '4px', fontSize: '12px', fontFamily: 'monospace' }}
                      />
                    </div>
                  </div>
                </div>
              </>
            ) : (
              <div style={{ padding: '40px', textAlign: 'center', color: 'var(--text-muted)' }}>
                Select a tile to review or edit its properties.
              </div>
            )}
          </div>
        </div>
      </div>
    </div>
  );
};
