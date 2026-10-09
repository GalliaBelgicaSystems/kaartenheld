import React from 'react';

export type EditLayer = 'terrain' | 'collision' | 'exits' | 'objects' | 'regions' | 'spawn';

interface LayerPanelProps {
  activeLayer: EditLayer;
  onSelectLayer: (layer: EditLayer) => void;
  showTerrain: boolean;
  onToggleShowTerrain: () => void;
  showExits: boolean;
  onToggleShowExits: () => void;
  showObjects: boolean;
  onToggleShowObjects: () => void;
  showRegions: boolean;
  onToggleShowRegions: () => void;
  exitCount: number;
  objectCount: number;
  regionCount: number;
  embedded?: boolean;
}

export const LayerPanel: React.FC<LayerPanelProps> = ({
  activeLayer,
  onSelectLayer,
  showTerrain,
  onToggleShowTerrain,
  showExits,
  onToggleShowExits,
  showObjects,
  onToggleShowObjects,
  showRegions,
  onToggleShowRegions,
  exitCount,
  objectCount,
  regionCount,
  embedded = false,
}) => {
  const list = (
    <div className="layer-list">
      <div className={`layer-item ${activeLayer === 'terrain' ? 'active' : ''}`}>
        <button className="layer-select-btn" onClick={() => onSelectLayer('terrain')}>
          <span className="layer-icon">🗺️</span>
          <span className="layer-title">Terrain Layer</span>
        </button>
        <button
          className={`layer-vis-btn ${showTerrain ? 'visible' : 'hidden'}`}
          onClick={onToggleShowTerrain}
          title="Toggle Terrain Visibility"
        >
          {showTerrain ? '👁️' : '🙈'}
        </button>
      </div>

      <div className={`layer-item ${activeLayer === 'spawn' ? 'active' : ''}`}>
        <button className="layer-select-btn" onClick={() => onSelectLayer('spawn')}>
          <span className="layer-icon">🚩</span>
          <span className="layer-title">Player Spawn</span>
        </button>
      </div>

      <div className={`layer-item ${activeLayer === 'exits' ? 'active' : ''}`}>
        <button className="layer-select-btn" onClick={() => onSelectLayer('exits')}>
          <span className="layer-icon">🚪</span>
          <span className="layer-title">Exits / Warps</span>
          <span className="badge">{exitCount}</span>
        </button>
        <button
          className={`layer-vis-btn ${showExits ? 'visible' : 'hidden'}`}
          onClick={onToggleShowExits}
          title="Toggle Exits Visibility"
        >
          {showExits ? '👁️' : '🙈'}
        </button>
      </div>

      <div className={`layer-item ${activeLayer === 'objects' ? 'active' : ''}`}>
        <button className="layer-select-btn" onClick={() => onSelectLayer('objects')}>
          <span className="layer-icon">👾</span>
          <span className="layer-title">Objects & NPCs</span>
          <span className="badge">{objectCount}</span>
        </button>
        <button
          className={`layer-vis-btn ${showObjects ? 'visible' : 'hidden'}`}
          onClick={onToggleShowObjects}
          title="Toggle Objects Visibility"
        >
          {showObjects ? '👁️' : '🙈'}
        </button>
      </div>

      <div className={`layer-item ${activeLayer === 'regions' ? 'active' : ''}`}>
        <button className="layer-select-btn" onClick={() => onSelectLayer('regions')}>
          <span className="layer-icon">🏷️</span>
          <span className="layer-title">Regions (LLM)</span>
          <span className="badge">{regionCount}</span>
        </button>
        <button
          className={`layer-vis-btn ${showRegions ? 'visible' : 'hidden'}`}
          onClick={onToggleShowRegions}
          title="Toggle Regions Visibility"
        >
          {showRegions ? '👁️' : '🙈'}
        </button>
      </div>
    </div>
  );

  if (embedded) {
    return (
      <div className="layer-panel-embedded">
        <h4 style={{ marginBottom: '10px' }}>Layers & Edit Modes</h4>
        <p className="hint-text" style={{ marginBottom: '12px' }}>
          Select an active editing layer below. Toggle 👁️ to show or hide the layer on the canvas.
        </p>
        {list}
      </div>
    );
  }

  return (
    <div className="panel layer-panel">
      <div className="panel-header">
        <h3>Layers & Modes</h3>
      </div>
      <div className="panel-body">
        {list}
      </div>
    </div>
  );
};

