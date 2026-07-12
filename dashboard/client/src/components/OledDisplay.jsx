import { useEffect, useRef, useState } from 'react';
import { Monitor, ZoomIn, ZoomOut, RotateCcw } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function OledDisplay() {
  const { displayFrame } = useDashboard();
  const canvasRef = useRef(null);
  const [zoom, setZoom] = useState(250); // デフォルト拡大率: 250%

  const handleZoomChange = (e) => {
    setZoom(Number(e.target.value));
  };

  const resetZoom = () => {
    setZoom(250);
  };

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = 128;
    const height = 64;

    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;

    const imgData = ctx.createImageData(width, height);

    // デフォルトの背景色 (消灯時の暗緑色)
    for (let i = 0; i < imgData.data.length; i += 4) {
      imgData.data[i] = 12;     // R
      imgData.data[i + 1] = 24; // G
      imgData.data[i + 2] = 12; // B
      imgData.data[i + 3] = 255;// A
    }

    if (displayFrame) {
      try {
        const binaryString = atob(displayFrame);
        const buffer = new Uint8Array(binaryString.length);
        for (let i = 0; i < binaryString.length; i++) {
          buffer[i] = binaryString.charCodeAt(i);
        }

        for (let page = 0; page < 8; page++) {
          for (let col = 0; col < 128; col++) {
            const byteIdx = page * 128 + col;
            if (byteIdx >= buffer.length) break;

            const byte = buffer[byteIdx];
            for (let bit = 0; bit < 8; bit++) {
              const pixelOn = (byte & (1 << bit)) !== 0;
              const x = col;
              const y = page * 8 + bit;
              const pixelIdx = (y * width + x) * 4;

              if (pixelOn) {
                // 点灯時のピクセルカラー (明るい黄緑色)
                imgData.data[pixelIdx] = 0;       // R
                imgData.data[pixelIdx + 1] = 255; // G
                imgData.data[pixelIdx + 2] = 80;  // B
                imgData.data[pixelIdx + 3] = 255; // A
              }
            }
          }
        }
      } catch (e) {
        console.error("[OledDisplay] Error decoding frame:", e);
      }
    }

    ctx.putImageData(imgData, 0, 0);
  }, [displayFrame]);

  // CSS ネジ穴・金属パッドのスタイル定義
  const screwPadStyle = (top = 'auto', left = 'auto', right = 'auto', bottom = 'auto') => ({
    position: 'absolute',
    top,
    left,
    right,
    bottom,
    width: '26px',
    height: '26px',
    borderRadius: '50%',
    background: 'radial-gradient(circle, #d0d0d0 40%, #888888 85%)',
    border: '1.2px solid #555',
    boxShadow: '0 1.5px 3px rgba(0,0,0,0.5), inset 0 1px 1px rgba(255,255,255,0.6)',
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'center'
  });

  const screwHoleStyle = {
    width: '15px',
    height: '15px',
    borderRadius: '50%',
    background: '#0d1117', // ダッシュボードのベース背景色に合わせる
    boxShadow: 'inset 0 3px 6px rgba(0,0,0,0.9), 0 1px 1px rgba(255,255,255,0.2)'
  };

  const baseWidth = 280;
  const baseHeight = 280;
  const scaledWidth = baseWidth * (zoom / 100);
  const scaledHeight = baseHeight * (zoom / 100);

  return (
    <div className="oled-pane" style={{ 
      height: '100%', 
      display: 'flex', 
      flexDirection: 'column', 
      overflow: 'hidden',
      position: 'relative',
      background: '#0d1117',
      color: '#c9d1d9'
    }}>
      <div className="panel-header" style={{
        display: 'flex',
        alignItems: 'center',
        gap: '6px',
        padding: '0.5rem 1rem',
        borderBottom: '1px solid #30363d',
        background: '#161b22',
        fontWeight: 600,
        fontSize: '0.85rem'
      }}>
        <Monitor size={16} /> Virtual OLED Display (128x64)
      </div>

      {/* ズームコントロールパネル (フローティング) */}
      <div style={{
        position: 'absolute',
        top: '2.5rem',
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
          max="600" 
          step="50"
          value={zoom} 
          onChange={handleZoomChange}
          style={{
            width: '80px',
            accentColor: '#58a6ff',
            cursor: 'pointer'
          }}
        />
        <ZoomIn size={14} style={{ color: '#8b949e' }} />
        <span style={{ fontSize: '0.75rem', fontWeight: 600, minWidth: '40px', textAlign: 'right' }}>
          {zoom}%
        </span>
        <button 
          onClick={resetZoom}
          title="Reset Zoom"
          style={{
            background: 'none',
            border: 'none',
            color: '#8b949e',
            cursor: 'pointer',
            display: 'flex',
            alignItems: 'center',
            padding: '2px',
            borderRadius: '4px',
            transition: 'background 0.2s',
          }}
          onMouseEnter={(e) => e.currentTarget.style.background = '#21262d'}
          onMouseLeave={(e) => e.currentTarget.style.background = 'none'}
        >
          <RotateCcw size={14} />
        </button>
      </div>

      <div className="oled-viewport" style={{ 
        flex: 1, 
        display: 'flex', 
        justifyContent: 'center', 
        alignItems: 'center', 
        backgroundColor: '#050705', // 有機ELが映えるように少し深めの黒
        padding: '1.5rem',
        overflow: 'auto', // 拡大時にスクロール可能にする
        boxShadow: 'inset 0 4px 20px rgba(0,0,0,0.7)'
      }}>
        {/* スケーリング用のダミー枠 */}
        <div style={{
          width: `${scaledWidth}px`,
          height: `${scaledHeight}px`,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          transition: 'width 0.1s ease-out, height 0.1s ease-out'
        }}>
          {/* OLED 物理モジュール自作ボード（CSS設計） */}
          <div style={{
            position: 'relative',
            width: `${baseWidth}px`,
            height: `${baseHeight}px`,
            transform: `scale(${zoom / 100})`,
            transformOrigin: 'center center',
            transition: 'transform 0.1s ease-out',
            userSelect: 'none',
            // PCBボード風グラデーション
            background: 'linear-gradient(135deg, #104eae 0%, #082860 100%)',
            border: '1.5px solid #183e78',
            borderRadius: '12px',
            boxShadow: '0 10px 30px rgba(0,0,0,0.6), inset 0 1.5px 2px rgba(255,255,255,0.25)'
          }}>
            {/* 四隅の取り付けネジ穴 */}
            <div style={screwPadStyle('8px', '8px')}><div style={screwHoleStyle} /></div>
            <div style={screwPadStyle('8px', 'auto', '8px')}><div style={screwHoleStyle} /></div>
            <div style={screwPadStyle('auto', '8px', 'auto', '8px')}><div style={screwHoleStyle} /></div>
            <div style={screwPadStyle('auto', 'auto', '8px', '8px')}><div style={screwHoleStyle} /></div>

            {/* 上部I2C接続ピンホール (4ピン端子) */}
            <div style={{
              position: 'absolute',
              top: '6px',
              left: '50%',
              transform: 'translateX(-50%)',
              display: 'flex',
              gap: '12px',
              alignItems: 'center'
            }}>
              {[0, 1, 2, 3].map(i => (
                <div key={i} style={{
                  width: '9px',
                  height: '9px',
                  borderRadius: '50%',
                  background: 'radial-gradient(circle, #ffd040 30%, #a27500 80%)',
                  border: '1px solid #1a1a1a',
                  boxShadow: 'inset 0 1px 1px rgba(255,255,255,0.6)'
                }} />
              ))}
            </div>

            {/* シルク印刷ピン名文字 */}
            <div style={{
              position: 'absolute',
              top: '18px',
              left: '50%',
              transform: 'translateX(-50%)',
              color: '#ffffff',
              fontFamily: 'monospace, "Courier New", sans-serif',
              fontSize: '7.5px',
              fontWeight: 'bold',
              letterSpacing: '1px',
              opacity: 0.9,
              whiteSpace: 'nowrap',
              textShadow: '0 1px 0 rgba(0,0,0,0.5)'
            }}>
              GND &nbsp;VCC &nbsp;SCL &nbsp;SDA
            </div>

            {/* ディスプレイのガラス窓枠 */}
            <div style={{
              position: 'absolute',
              top: '36px',
              left: '12px',
              width: '252px',
              height: '144px',
              background: '#14171a',
              border: '2px solid #07080a',
              borderRadius: '4px',
              boxShadow: '0 6px 15px rgba(0,0,0,0.8), inset 0 2px 4px rgba(255,255,255,0.1)',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              overflow: 'hidden'
            }}>
              {/* 実表示領域 (Canvas) を 2:1 アスペクト比で完璧にフィット */}
              <div style={{
                width: '92%',
                height: '76%',
                backgroundColor: '#040604',
                border: '1.2px solid #111',
                borderRadius: '2px',
                boxShadow: 'inset 0 0 15px rgba(0,0,0,0.95)',
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                overflow: 'hidden'
              }}>
                <canvas 
                  ref={canvasRef} 
                  style={{ 
                    display: 'block',
                    width: '100%', 
                    height: '100%',
                    imageRendering: 'pixelated', 
                    filter: 'drop-shadow(0 0 2px rgba(0, 255, 80, 0.3))'
                  }} 
                />
              </div>
            </div>

            {/* 下部FPCコネクタ接続部（OLEDモジュールのリアリティ再現） */}
            <div style={{
              position: 'absolute',
              bottom: '16px',
              left: '50%',
              transform: 'translateX(-50%)',
              width: '90px',
              height: '46px',
              display: 'flex',
              flexDirection: 'column',
              alignItems: 'center',
              pointerEvents: 'none'
            }}>
              {/* 黒いフラットケーブル押さえパーツ */}
              <div style={{
                width: '80px',
                height: '14px',
                background: '#1b1c1e',
                border: '1px solid #0c0c0d',
                borderRadius: '2px 2px 0 0',
                boxShadow: '0 2px 4px rgba(0,0,0,0.4), inset 0 1px 1px rgba(255,255,255,0.1)'
              }} />
              {/* 黄色いフラットケーブル（FPC） */}
              <div style={{
                width: '70px',
                height: '16px',
                background: 'linear-gradient(to bottom, #d2991a 0%, #ad760e 100%)',
                borderLeft: '1px solid #855803',
                borderRight: '1px solid #855803',
                boxShadow: 'inset 0 1px 2px rgba(255,255,255,0.2)'
              }} />
              {/* 基板接続コネクタ */}
              <div style={{
                width: '84px',
                height: '16px',
                background: '#111213',
                border: '1px solid #060606',
                borderRadius: '0 0 2px 2px',
                boxShadow: '0 -1px 3px rgba(0,0,0,0.4)'
              }} />
            </div>

          </div>
        </div>
      </div>
    </div>
  );
}

export default OledDisplay;
