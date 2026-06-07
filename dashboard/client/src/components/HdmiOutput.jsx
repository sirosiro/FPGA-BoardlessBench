import React, { useState } from 'react';
import { useDashboard } from './DashboardContext';
import { Tv, AlertCircle, ZoomIn, ZoomOut, RotateCcw } from 'lucide-react';

const HdmiOutput = () => {
  const { hdmiFrame } = useDashboard();
  const [zoom, setZoom] = useState(400); // ズーム倍率 (%) のデフォルト（Fit解除時は400%などが見やすい）
  const [isFit, setIsFit] = useState(true); // 初期状態はウィンドウに収めるFitモード

  const handleZoomChange = (e) => {
    setZoom(Number(e.target.value));
    setIsFit(false); // スライダーを動かしたらFitモードを解除する
  };

  const enableFit = () => {
    setIsFit(true);
  };

  return (
    <div className="hdmi-panel" style={{
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      width: '100%',
      background: '#0d1117',
      color: '#c9d1d9',
      padding: '1rem',
      boxSizing: 'border-box',
      overflow: 'hidden',
      position: 'relative'
    }}>
      <style>{`
        @keyframes pulse {
          0% { opacity: 0.5; transform: scale(0.98); }
          50% { opacity: 0.9; transform: scale(1.02); }
          100% { opacity: 0.5; transform: scale(0.98); }
        }
      `}</style>
      
      {/* ズームコントロールパネル (フローティング) */}
      {hdmiFrame && (
        <div style={{
          position: 'absolute',
          top: '1.5rem',
          right: '1.5rem',
          zIndex: 10,
          background: 'rgba(22, 27, 34, 0.85)',
          backdropFilter: 'blur(8px)',
          border: '1px solid #30363d',
          borderRadius: '8px',
          padding: '6px 12px',
          display: 'flex',
          alignItems: 'center',
          gap: '8px',
          boxShadow: '0 4px 12px rgba(0,0,0,0.5)'
        }}>
          <ZoomOut size={14} style={{ color: '#8b949e' }} />
          <input 
            type="range" 
            min="100" 
            max="1600" 
            step="50"
            value={isFit ? 100 : zoom} 
            onChange={handleZoomChange}
            style={{
              width: '80px',
              accentColor: '#58a6ff',
              cursor: 'pointer'
            }}
          />
          <ZoomIn size={14} style={{ color: '#8b949e' }} />
          <span style={{ fontSize: '0.75rem', fontWeight: 600, minWidth: '45px', textAlign: 'right' }}>
            {isFit ? 'Fit' : `${zoom}%`}
          </span>
          <button 
            onClick={enableFit}
            title="Fit to Window"
            style={{
              background: isFit ? '#21262d' : 'none',
              border: 'none',
              color: isFit ? '#58a6ff' : '#8b949e',
              cursor: 'pointer',
              display: 'flex',
              alignItems: 'center',
              padding: '2px',
              borderRadius: '4px',
              transition: 'background 0.2s',
            }}
            onMouseEnter={(e) => { if(!isFit) e.target.style.background = '#21262d'; }}
            onMouseLeave={(e) => { if(!isFit) e.target.style.background = 'none'; }}
          >
            <RotateCcw size={14} />
          </button>
        </div>
      )}

      <div className="hdmi-content" style={{
        flex: 1,
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        background: '#161b22',
        borderRadius: '8px',
        border: '1px solid #30363d',
        position: 'relative',
        boxShadow: 'inset 0 4px 20px rgba(0,0,0,0.5)',
        overflow: 'auto' // 拡大時にスクロール可能にする
      }}>
        {hdmiFrame ? (
          <div style={{
            width: '100%',
            height: '100%',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            padding: '1rem',
            boxSizing: 'border-box',
          }}>
            <img 
              src={`data:image/bmp;base64,${hdmiFrame}`} 
              alt="HDMI Output"
              style={{
                width: isFit ? '100%' : `${64 * zoom / 100}px`,
                height: isFit ? '100%' : `${64 * zoom / 100}px`,
                maxWidth: isFit ? '100%' : 'none',
                maxHeight: isFit ? '100%' : 'none',
                objectFit: 'contain',
                imageRendering: 'pixelated',
                borderRadius: '4px',
                boxShadow: '0 8px 32px rgba(0,0,0,0.8)',
                border: '1px solid #30363d',
                transition: 'width 0.1s ease-out, height 0.1s ease-out'
              }}
            />
          </div>
        ) : (
          <div className="no-signal" style={{
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'center',
            gap: '1rem',
            color: '#8b949e',
            animation: 'pulse 2s infinite ease-in-out'
          }}>
            <div style={{ position: 'relative', display: 'inline-block' }}>
              <Tv size={48} style={{ color: '#8b949e', opacity: 0.8 }} />
              <AlertCircle size={20} style={{ 
                color: '#f85149', 
                position: 'absolute', 
                bottom: -4, 
                right: -4,
                background: '#161b22',
                borderRadius: '50%'
              }} />
            </div>
            <div style={{ fontSize: '0.9rem', fontWeight: 600, letterSpacing: '0.1em' }}>NO HDMI SIGNAL</div>
            <div style={{ fontSize: '0.75rem', opacity: 0.6 }}>Waiting for video stream...</div>
          </div>
        )}
      </div>
    </div>
  );
};

export default HdmiOutput;
