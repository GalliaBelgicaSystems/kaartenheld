import React from 'react';

export type ToolType = 'brush' | 'eraser' | 'rect' | 'fill' | 'eyedropper' | 'select' | 'clone';

interface ToolbarProps {
  activeTool: ToolType;
  onSelectTool: (tool: ToolType) => void;
  clonePattern?: string[][] | null;
  onClearClone?: () => void;
  canUndo: boolean;
  canRedo: boolean;
  onUndo: () => void;
  onRedo: () => void;
  zoom: number;
  onZoomChange: (newZoom: number) => void;
  showGrid: boolean;
  onToggleGrid: () => void;
  showCollision: boolean;
  onToggleCollision: () => void;
  fidelity?: 'raw' | 'rom';
  onFidelityChange?: (f: 'raw' | 'rom') => void;
  onSave: () => void;
  onDownload: () => void;
  onCompileRom: () => void;
  onRunGame: () => void;
  isCompiling: boolean;
  isRunning: boolean;
  onLoad: () => void;
  onValidate: () => void;
  onDescribe: () => void;
  onSoundTest: () => void;
  onNew: () => void;
  isTilesetReviewerOpen?: boolean;
  onToggleTilesetReviewer?: () => void;
  isCombatArtOpen?: boolean;
  onToggleCombatArt?: () => void;
}

export const Toolbar: React.FC<ToolbarProps> = ({
  activeTool,
  onSelectTool,
  canUndo,
  canRedo,
  onUndo,
  onRedo,
  zoom,
  onZoomChange,
  showGrid,
  onToggleGrid,
  showCollision,
  onToggleCollision,
  fidelity = 'raw',
  onFidelityChange,
  onSave,
  onDownload,
  onCompileRom,
  onRunGame,
  isCompiling,
  isRunning,
  onLoad,
  onValidate,
  onDescribe,
  onSoundTest,
  onNew,
  clonePattern,
  onClearClone,
  isTilesetReviewerOpen,
  onToggleTilesetReviewer,
  isCombatArtOpen,
  onToggleCombatArt,
}) => {
  return (
    <div className="toolbar">
      <div className="tool-group">
        <button className="btn btn-primary" onClick={onNew} title="New Level">
          ➕ New
        </button>
        <button className="btn" onClick={onLoad} title="Load JSON File">
          📂 Load
        </button>
        <button className="btn btn-success" onClick={onSave} title="Save to disk (levels/<id>.json)">
          💾 Save
        </button>
        <button
          className="btn"
          style={{ background: '#d35400', color: '#fff', fontWeight: 'bold' }}
          onClick={onCompileRom}
          disabled={isCompiling}
          title="Save Level & Build Game Boy ROMs (make debug + release, parallel)"
        >
          {isCompiling ? '⏳ Compiling...' : '🔨 Compile ROM'}
        </button>
        <button
          className="btn"
          style={{ background: '#27ae60', color: '#fff', fontWeight: 'bold' }}
          onClick={onRunGame}
          disabled={isRunning || isCompiling}
          title="Launch release Game Boy ROM in emulator (rebuilds if stale)"
        >
          {isRunning ? '⏳ Launching...' : '▶️ Run Game (PyBoy)'}
        </button>
        <button className="btn" onClick={onValidate} title="Validate Level">
          ✓ Validate
        </button>
        <button className="btn btn-accent" onClick={onDescribe} title="Generate LLM Semantic Description">
          🤖 LLM View
        </button>
        <button className="btn" onClick={onSoundTest} title="Preview sound effects">
          🔊 SFX
        </button>
        <button className="btn" onClick={onDownload} title="Export / Download JSON file">
          ⬇️ Export
        </button>
        {onToggleTilesetReviewer && (
          <button
            className={`btn ${isTilesetReviewerOpen ? 'active' : ''}`}
            style={{
              background: isTilesetReviewerOpen ? '#9b59b6' : '#8e44ad',
              color: '#ffffff',
              fontWeight: 'bold',
              border: isTilesetReviewerOpen ? '1px solid #00f0ff' : '1px solid rgba(255,255,255,0.15)',
              boxShadow: isTilesetReviewerOpen ? '0 0 10px rgba(0, 240, 255, 0.4)' : 'none',
            }}
            onClick={onToggleTilesetReviewer}
            title={isTilesetReviewerOpen ? 'Quit / Close Tileset Reviewer' : 'Review and edit tile asset properties or import a new tileset PNG'}
          >
            🎨 {isTilesetReviewerOpen ? '✕ Close Tiles' : 'Import/Review Tiles'}
          </button>
        )}
        {onToggleCombatArt && (
          <button
            className={`btn ${isCombatArtOpen ? 'active' : ''}`}
            style={{
              background: isCombatArtOpen ? '#9b59b6' : '#8e44ad',
              color: '#ffffff',
              fontWeight: 'bold',
              border: isCombatArtOpen ? '1px solid #00f0ff' : '1px solid rgba(255,255,255,0.15)',
              boxShadow: isCombatArtOpen ? '0 0 10px rgba(0, 240, 255, 0.4)' : 'none',
            }}
            onClick={onToggleCombatArt}
            title={isCombatArtOpen ? 'Close Combat Art Studio' : 'Compose enemy combat-art meta-tiles and assign combat sprites'}
          >
            ⚔️ {isCombatArtOpen ? '✕ Close Art' : 'Combat Art'}
          </button>
        )}
      </div>

      <div className="divider" />

      <div className="tool-group">
        <button
          className={`btn-tool ${activeTool === 'brush' ? 'active' : ''}`}
          onClick={() => onSelectTool('brush')}
          title="Brush / Paint (B)"
        >
          ✏️ Brush <span className="shortcut">B</span>
        </button>
        <button
          className={`btn-tool ${activeTool === 'eraser' ? 'active' : ''}`}
          onClick={() => onSelectTool('eraser')}
          title="Eraser (E)"
        >
          🧹 Erase <span className="shortcut">E</span>
        </button>
        <button
          className={`btn-tool ${activeTool === 'rect' ? 'active' : ''}`}
          onClick={() => onSelectTool('rect')}
          title="Rectangle (R)"
        >
          ⬜ Rect <span className="shortcut">R</span>
        </button>
        <button
          className={`btn-tool ${activeTool === 'fill' ? 'active' : ''}`}
          onClick={() => onSelectTool('fill')}
          title="Flood Fill (G)"
        >
          🪣 Fill <span className="shortcut">G</span>
        </button>
        <button
          className={`btn-tool ${activeTool === 'eyedropper' ? 'active' : ''}`}
          onClick={() => onSelectTool('eyedropper')}
          title="Eyedropper / Pick (I)"
        >
          💉 Pick <span className="shortcut">I</span>
        </button>
        <button
          className={`btn-tool ${activeTool === 'select' ? 'active' : ''}`}
          onClick={() => onSelectTool('select')}
          title="Select / Move Entity (V)"
        >
          👆 Select <span className="shortcut">V</span>
        </button>
        <button
          className={`btn-tool ${activeTool === 'clone' ? 'active' : ''}`}
          onClick={() => onSelectTool('clone')}
          title="Clone Stamp: Drag or Shift+Drag to capture, click to stamp (C)"
        >
          📋 Clone <span className="shortcut">C</span>
        </button>
        {activeTool === 'clone' && (
          <div className="clone-badge" title="Clone Stamp: Drag or Shift+Drag to capture, click to stamp">
            {clonePattern && clonePattern.length > 0 ? (
              <>
                <span className="clone-badge-dims">{clonePattern[0].length}×{clonePattern.length}</span>
                <button
                  className="btn-tiny"
                  onClick={(e) => { e.stopPropagation(); onClearClone?.(); }}
                  title="Clear clone buffer to capture a new area"
                >
                  ✕
                </button>
              </>
            ) : (
              <span className="clone-badge-hint">Drag box</span>
            )}
          </div>
        )}
      </div>

      <div className="divider" />

      <div className="tool-group">
        <button className="btn-icon" onClick={onUndo} disabled={!canUndo} title="Undo (Ctrl+Z)">
          ↩️
        </button>
        <button className="btn-icon" onClick={onRedo} disabled={!canRedo} title="Redo (Ctrl+Shift+Z)">
          ↪️
        </button>
      </div>

      <div className="divider" />

      <div className="tool-group">
        <button
          className={`btn-toggle ${showGrid ? 'active' : ''}`}
          onClick={onToggleGrid}
          title="Toggle Grid Overlay"
        >
          Grid {showGrid ? '✓' : '✗'}
        </button>
        <button
          className={`btn-toggle ${showCollision ? 'active' : ''}`}
          onClick={onToggleCollision}
          title="Toggle Collision Walkability Overlay"
        >
          Collision {showCollision ? '✓' : '✗'}
        </button>
        {onFidelityChange && (
          <button
            className={`btn-toggle ${fidelity === 'rom' ? 'active' : ''}`}
            onClick={() => onFidelityChange(fidelity === 'rom' ? 'raw' : 'rom')}
            title="Preview fidelity: Raw artist PNGs vs pipeline-exact ROM recolor (exact match, else nearest shade in the tile's ramp). ROM catches ramp mistakes before Compile ROM."
          >
            {fidelity === 'rom' ? '🎨 ROM-exact' : '🎨 Raw art'}
          </button>
        )}
      </div>

      <div className="divider" />

      <div className="tool-group zoom-group">
        <button
          className="btn-icon"
          onClick={() => {
            const ZOOM_LEVELS = [0.25, 0.5, 1, 2, 3, 4, 5];
            const prev = [...ZOOM_LEVELS].reverse().find((z) => z < zoom - 0.001);
            if (prev !== undefined) onZoomChange(prev);
          }}
          disabled={zoom <= 0.25}
          title="Zoom Out (Down to 25%)"
        >
          ➖
        </button>
        <span
          className="zoom-text"
          onClick={() => onZoomChange(1)}
          title="Click to reset zoom to 100%"
          style={{ cursor: 'pointer' }}
        >
          {Math.round(zoom * 100)}%
        </span>
        <button
          className="btn-icon"
          onClick={() => {
            const ZOOM_LEVELS = [0.25, 0.5, 1, 2, 3, 4, 5];
            const next = ZOOM_LEVELS.find((z) => z > zoom + 0.001);
            if (next !== undefined) onZoomChange(next);
          }}
          disabled={zoom >= 5}
          title="Zoom In (Up to 500%)"
        >
          ➕
        </button>
      </div>
    </div>
  );
};
