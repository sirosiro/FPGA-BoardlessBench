import { useEffect, useRef, useState } from 'react';
import { Monitor, ZoomIn, ZoomOut, RotateCcw, Layers } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function extractSvgInnerHtml(svgStr) {
  if (!svgStr) return '';
  const match = svgStr.match(/<svg[^>]*>([\s\S]*)<\/svg>/i);
  return match ? match[1] : svgStr;
}

function SevenSegDigit({ x, y, byte, color = '#ff0033' }) {
  const segA = (byte & 0x01) !== 0;
  const segB = (byte & 0x02) !== 0;
  const segC = (byte & 0x04) !== 0;
  const segD = (byte & 0x08) !== 0;
  const segE = (byte & 0x10) !== 0;
  const segF = (byte & 0x20) !== 0;
  const segG = (byte & 0x40) !== 0;
  const segDP = (byte & 0x80) !== 0;

  const onColor = color === 'red' ? '#ff0033' : color;
  const strokeColor = color === 'red' ? '#ffb3c1' : (color === '#20ff40' ? '#b3ffc1' : '#ffffff');
  const offColor = 'rgba(75, 12, 18, 0.35)';
  const glowFilter = `drop-shadow(0px 0px 4px #ffffff) drop-shadow(0px 0px 10px ${onColor}) drop-shadow(0px 0px 22px ${onColor})`;

  return (
    <g transform={`translate(${x}, ${y}) scale(2.65) skewX(-6)`}>
      {/* Seg A - Top */}
      <polygon points="4,2 26,2 21,7 9,7" fill={segA ? onColor : offColor} stroke={segA ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segA ? glowFilter : 'none' }} />
      {/* Seg B - Top Right */}
      <polygon points="27,3 27,23.5 22,21 22,8" fill={segB ? onColor : offColor} stroke={segB ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segB ? glowFilter : 'none' }} />
      {/* Seg C - Bottom Right */}
      <polygon points="27,26.5 27,47 22,42 22,29" fill={segC ? onColor : offColor} stroke={segC ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segC ? glowFilter : 'none' }} />
      {/* Seg D - Bottom */}
      <polygon points="4,48 26,48 21,43 9,43" fill={segD ? onColor : offColor} stroke={segD ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segD ? glowFilter : 'none' }} />
      {/* Seg E - Bottom Left */}
      <polygon points="3,26.5 3,47 8,42 8,29" fill={segE ? onColor : offColor} stroke={segE ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segE ? glowFilter : 'none' }} />
      {/* Seg F - Top Left */}
      <polygon points="3,3 3,23.5 8,21 8,8" fill={segF ? onColor : offColor} stroke={segF ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segF ? glowFilter : 'none' }} />
      {/* Seg G - Middle Hexagon */}
      <polygon points="4,25 8,22.5 22,22.5 26,25 22,27.5 8,27.5" fill={segG ? onColor : offColor} stroke={segG ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segG ? glowFilter : 'none' }} />
      {/* Seg DP - Decimal Point */}
      <circle cx="32" cy="46" r="2.8" fill={segDP ? onColor : offColor} stroke={segDP ? strokeColor : 'none'} strokeWidth="0.4" style={{ filter: segDP ? glowFilter : 'none' }} />
    </g>
  );
}

function SevenSegColon({ x, y, on, color = '#ff0033' }) {
  const onColor = color === 'red' ? '#ff0033' : color;
  const strokeColor = color === 'red' ? '#ffb3c1' : (color === '#20ff40' ? '#b3ffc1' : '#ffffff');
  const offColor = 'rgba(75, 12, 18, 0.35)';
  const glowFilter = `drop-shadow(0px 0px 4px #ffffff) drop-shadow(0px 0px 10px ${onColor}) drop-shadow(0px 0px 22px ${onColor})`;
  return (
    <g transform={`translate(${x}, ${y}) scale(2.65) skewX(-6)`}>
      <circle cx="5" cy="15" r="3.2" fill={on ? onColor : offColor} stroke={on ? strokeColor : 'none'} strokeWidth="0.4" style={{ filter: on ? glowFilter : 'none' }} />
      <circle cx="5" cy="35" r="3.2" fill={on ? onColor : offColor} stroke={on ? strokeColor : 'none'} strokeWidth="0.4" style={{ filter: on ? glowFilter : 'none' }} />
    </g>
  );
}

function OledDisplay(props) {
  const { displayFrame, display7SegFrame, peripheralFrames, manifest } = useDashboard();
  const canvasRef = useRef(null);
  const [zoom, setZoom] = useState(250);

  const handleZoomChange = (e) => setZoom(Number(e.target.value));
  const resetZoom = () => setZoom(250);

  const i2cSlaves = manifest?.devices?.flatMap(d => d.i2c_slaves || []) || [];
  const oledSlave = i2cSlaves.find(s => s.compatible === 'solomon,ssd1306');
  const seg7Slave = i2cSlaves.find(s => s.compatible?.includes('ht16k33'));

  const params = props?.params || {};
  const panelId = String(props?.api?.id || props?.id || '');
  const panelTitle = String(props?.api?.title || props?.title || params?.title || '');
  
  const isSeg7Pane = props?.type === 'seg7' ||
                     params?.type === 'seg7' || 
                     panelId.includes('seg7') ||
                     Boolean(params?.manifest?.compatible?.includes('ht16k33')) ||
                     Boolean(params?.pluginId?.includes('ht16k33')) ||
                     panelTitle.includes('7-Segment') ||
                     panelTitle.includes('ht16k33') ||
                     panelTitle.includes('7Seg');

  const activeType = isSeg7Pane ? 'seg7' : 'oled';
  const currentSlave = isSeg7Pane ? (seg7Slave || params?.manifest) : (oledSlave || params?.manifest);
  const paneTitle = currentSlave?.ui_widget?.title || 
                    (activeType === 'seg7' ? 'Adafruit 4-Digit 7-Segment LED (Red)' : 'SSD1306 OLED Display (128x64)');

  const hasActivePeripheral = Boolean(currentSlave || (isSeg7Pane ? seg7Slave : oledSlave));

  const colorMap = { red: '#ff0033', green: '#20ff40', blue: '#20a0ff', yellow: '#ffb000', white: '#f0f0f0' };
  const ctrlColor = currentSlave?.ui_widget?.controls?.[0]?.color;
  const ledColor = colorMap[ctrlColor] || ctrlColor || (currentSlave?.compatible?.includes('green') ? '#20ff40' : '#ff0033');

  useEffect(() => {
    if (!hasActivePeripheral || activeType !== 'oled') return;

    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = 128;
    const height = 64;
    if (canvas.width !== width) canvas.width = width;
    if (canvas.height !== height) canvas.height = height;

    const imgData = ctx.createImageData(width, height);
    for (let i = 0; i < imgData.data.length; i += 4) {
      imgData.data[i] = 12;
      imgData.data[i + 1] = 24;
      imgData.data[i + 2] = 12;
      imgData.data[i + 3] = 255;
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
              if ((byte & (1 << bit)) !== 0) {
                const x = col;
                const y = page * 8 + bit;
                const pixelIdx = (y * width + x) * 4;
                imgData.data[pixelIdx] = 0;
                imgData.data[pixelIdx + 1] = 255;
                imgData.data[pixelIdx + 2] = 80;
                imgData.data[pixelIdx + 3] = 255;
              }
            }
          }
        }
      } catch (e) {
        console.error("[OledDisplay] Error decoding frame:", e);
      }
    }
    ctx.putImageData(imgData, 0, 0);
  }, [displayFrame, hasActivePeripheral, activeType]);

  let seg7Bytes = [0, 0, 0, 0];
  let seg7Colon = false;
  const rawSeg7Frame = display7SegFrame || peripheralFrames?.['fbb_display_7seg_0'];
  if (rawSeg7Frame) {
    try {
      const binaryString = atob(rawSeg7Frame);
      const buffer = new Uint8Array(binaryString.length);
      for (let i = 0; i < binaryString.length; i++) {
        buffer[i] = binaryString.charCodeAt(i);
      }
      if (buffer.length >= 10) {
        seg7Bytes = [buffer[0], buffer[2], buffer[6], buffer[8]];
        seg7Colon = (buffer[4] & 0x02) !== 0;
      }
    } catch (e) {
      console.error("[7SegDisplay] Error decoding frame:", e);
    }
  }

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
        justify: 'space-between',
        padding: '0.5rem 1rem',
        borderBottom: '1px solid #30363d',
        background: '#161b22',
        fontWeight: 600,
        fontSize: '0.85rem'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <Monitor size={16} style={{ color: '#58a6ff' }} />
          <span>{paneTitle}</span>
        </div>
      </div>

      {hasActivePeripheral ? (
        <>
          <div style={{
            position: 'absolute',
            top: '2.8rem',
            right: '1.2rem',
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
              style={{ width: '80px', accentColor: '#58a6ff', cursor: 'pointer' }}
            />
            <ZoomIn size={14} style={{ color: '#8b949e' }} />
            <span style={{ fontSize: '0.75rem', fontWeight: 600, minWidth: '40px', textAlign: 'right' }}>
              {zoom}%
            </span>
            <button onClick={resetZoom} title="Reset Zoom" style={{ background: 'none', border: 'none', color: '#8b949e', cursor: 'pointer', padding: '2px' }}>
              <RotateCcw size={14} />
            </button>
          </div>

          <div className="oled-viewport" style={{ 
            flex: 1, 
            display: 'flex', 
            justifyContent: 'center', 
            alignItems: 'center', 
            backgroundColor: '#050705',
            padding: '1.5rem',
            overflow: 'auto',
            boxShadow: 'inset 0 4px 20px rgba(0,0,0,0.7)'
          }}>
            <div style={{
              width: `${scaledWidth}px`,
              height: `${scaledHeight}px`,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              transition: 'width 0.1s ease-out, height 0.1s ease-out'
            }}>
              <div style={{
                position: 'relative',
                width: `${baseWidth}px`,
                height: `${baseHeight}px`,
                transform: `scale(${zoom / 100})`,
                transformOrigin: 'center center',
                transition: 'transform 0.1s ease-out',
                userSelect: 'none'
              }}>
                {/* OLED Display Active View */}
                {activeType === 'oled' && (
                  <>
                    {oledSlave?.ui_widget?.board_svg_content && (
                      <div 
                        style={{ position: 'absolute', top: 0, left: 0, width: '100%', height: '100%', zIndex: 0 }}
                        dangerouslySetInnerHTML={{ __html: oledSlave.ui_widget.board_svg_content }} 
                      />
                    )}
                    <div style={{
                      position: 'absolute',
                      top: '36px',
                      left: '12px',
                      width: '252px',
                      height: '144px',
                      background: '#14171a',
                      border: '2px solid #07080a',
                      borderRadius: '4px',
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'center',
                      overflow: 'hidden',
                      zIndex: 1
                    }}>
                      <div style={{
                        width: '92%',
                        height: '76%',
                        backgroundColor: '#040604',
                        border: '1.2px solid #111',
                        borderRadius: '2px',
                        display: 'flex',
                        alignItems: 'center',
                        justifyContent: 'center'
                      }}>
                        <canvas 
                          ref={canvasRef} 
                          style={{ display: 'block', width: '100%', height: '100%', imageRendering: 'pixelated' }} 
                        />
                      </div>
                    </div>
                  </>
                )}

                {/* 7-Segment LED Active View (Full-Cutout Scale Vector SVG ViewBox) */}
                {activeType === 'seg7' && (
                  <div style={{ position: 'absolute', top: 0, left: 0, width: '100%', height: '100%', zIndex: 1 }}>
                    <svg viewBox="0 0 500 270" width="100%" height="100%">
                      <g dangerouslySetInnerHTML={{ __html: extractSvgInnerHtml(seg7Slave?.ui_widget?.board_svg_content) }} />
                      <g transform="translate(55, 52)">
                        <SevenSegDigit x={0} y={0} byte={seg7Bytes[0]} color={ledColor} />
                        <SevenSegDigit x={90} y={0} byte={seg7Bytes[1]} color={ledColor} />
                        <SevenSegColon x={183} y={0} on={seg7Colon} color={ledColor} />
                        <SevenSegDigit x={215} y={0} byte={seg7Bytes[2]} color={ledColor} />
                        <SevenSegDigit x={305} y={0} byte={seg7Bytes[3]} color={ledColor} />
                      </g>
                    </svg>
                  </div>
                )}
              </div>
            </div>
          </div>
        </>
      ) : (
        <div style={{ 
          flex: 1, 
          display: 'flex', 
          flexDirection: 'column',
          justifyContent: 'center', 
          alignItems: 'center', 
          backgroundColor: '#050705',
          padding: '2rem'
        }}>
          <div style={{
            display: 'flex',
            flexDirection: 'column',
            alignItems: 'center',
            gap: '16px',
            color: '#8b949e',
            background: '#161b22',
            border: '1px solid #30363d',
            borderRadius: '12px',
            padding: '2.5rem 3rem',
            textAlign: 'center',
            maxWidth: '460px'
          }}>
            <div style={{
              width: '64px',
              height: '64px',
              borderRadius: '50%',
              background: 'rgba(88, 166, 255, 0.1)',
              border: '1px solid rgba(88, 166, 255, 0.2)',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center'
            }}>
              <Layers size={32} style={{ color: '#58a6ff' }} />
            </div>
            <div>
              <h3 style={{ margin: '0 0 6px 0', color: '#f0f6fc', fontSize: '1.1rem', fontWeight: 600 }}>
                NO ACTIVE PERIPHERAL CONNECTED
              </h3>
              <p style={{ margin: 0, fontSize: '0.8rem', color: '#8b949e', lineHeight: 1.5 }}>
                Waiting for DTS PPA peripheral device specification<br />
                (e.g., SSD1306 OLED, 7-Segment LED, SPI Sensors).
              </p>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export default OledDisplay;
