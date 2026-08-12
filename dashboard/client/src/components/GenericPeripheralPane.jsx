import React, { useState } from 'react';
import { ZoomIn, ZoomOut, RotateCcw } from 'lucide-react';
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
  const strokeColor = '#ffb3c1';
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
      {/* Seg G - Middle Hexagon (Clean Geometry) */}
      <polygon points="4,25 8,22.5 22,22.5 26,25 22,27.5 8,27.5" fill={segG ? onColor : offColor} stroke={segG ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segG ? glowFilter : 'none' }} />
      {/* Seg DP - Decimal Point */}
      <circle cx="32" cy="46" r="2.8" fill={segDP ? onColor : offColor} stroke={segDP ? strokeColor : 'none'} strokeWidth="0.4" style={{ filter: segDP ? glowFilter : 'none' }} />
    </g>
  );
}

function SevenSegColon({ x, y, on, color = '#ff0033' }) {
  const onColor = color === 'red' ? '#ff0033' : color;
  const strokeColor = '#ffb3c1';
  const offColor = 'rgba(75, 12, 18, 0.35)';
  const glowFilter = `drop-shadow(0px 0px 4px #ffffff) drop-shadow(0px 0px 10px ${onColor}) drop-shadow(0px 0px 22px ${onColor})`;
  return (
    <g transform={`translate(${x}, ${y}) scale(2.65)`}>
      <circle cx="5" cy="15" r="3.2" fill={on ? onColor : offColor} stroke={on ? strokeColor : 'none'} strokeWidth="0.4" style={{ filter: on ? glowFilter : 'none' }} />
      <circle cx="5" cy="35" r="3.2" fill={on ? onColor : offColor} stroke={on ? strokeColor : 'none'} strokeWidth="0.4" style={{ filter: on ? glowFilter : 'none' }} />
    </g>
  );
}

function MatrixCanvas({ ctrl, frameData, slaveData, fullHeight = false, fitInside = false }) {
  const canvasRef = React.useRef(null);
  
  const parseGrid = (val) => {
    if (Array.isArray(val) && val.length >= 2) return [Number(val[0]), Number(val[1])];
    if (typeof val === 'string') {
      const parts = val.trim().split(/\s+/).map(Number);
      if (parts.length >= 2 && !isNaN(parts[0]) && !isNaN(parts[1])) return parts;
    }
    return null;
  };

  const parsedSlaveGrid = parseGrid(slaveData?.grid_size) || parseGrid(slaveData?.extra_props?.grid_size);
  const parsedCtrlGrid = parseGrid(ctrl?.grid_size);

  const cols = parsedSlaveGrid ? parsedSlaveGrid[0] : (parsedCtrlGrid ? parsedCtrlGrid[0] : (ctrl?.width || 64));
  const rows = parsedSlaveGrid ? parsedSlaveGrid[1] : (parsedCtrlGrid ? parsedCtrlGrid[1] : (ctrl?.height || 64));

  React.useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    ctx.fillStyle = '#03060c';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    if (!frameData) return;

    try {
      const binaryString = atob(frameData);
      const buffer = new Uint8Array(binaryString.length);
      for (let i = 0; i < binaryString.length; i++) buffer[i] = binaryString.charCodeAt(i);

      const cellW = canvas.width / cols;
      const cellH = canvas.height / rows;
      const radius = Math.min(cellW, cellH) * 0.42;

      for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
          const idx = (r * cols + c) * 3;
          if (idx + 2 < buffer.length) {
            const red = buffer[idx];
            const green = buffer[idx + 1];
            const blue = buffer[idx + 2];

            const cx = c * cellW + cellW / 2;
            const cy = r * cellH + cellH / 2;

            if (red > 0 || green > 0 || blue > 0) {
              ctx.fillStyle = `rgb(${red},${green},${blue})`;
              ctx.shadowColor = `rgb(${red},${green},${blue})`;
              ctx.shadowBlur = Math.min(cellW, cellH) * 0.7;
            } else {
              ctx.fillStyle = 'rgba(15, 20, 30, 0.4)';
              ctx.shadowBlur = 0;
            }
            ctx.beginPath();
            ctx.arc(cx, cy, radius, 0, Math.PI * 2);
            ctx.fill();
          }
        }
      }
    } catch (e) {}
  }, [frameData, cols, rows]);

  if (fitInside) {
    return (
      <canvas
        ref={canvasRef}
        width={cols * 8}
        height={rows * 8}
        style={{
          width: '100%',
          height: '100%',
          aspectRatio: `${cols} / ${rows}`,
          objectFit: 'contain',
          borderRadius: '3px',
          backgroundColor: '#020408',
          boxSizing: 'border-box',
          display: 'block'
        }}
      />
    );
  }

  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      justifyContent: 'center',
      height: fullHeight ? '100%' : 'auto',
      flex: fullHeight ? 1 : 'none',
      backgroundColor: '#090d16',
      padding: '12px',
      borderRadius: '6px',
      border: '1px solid #1e293b',
      boxSizing: 'border-box',
      overflow: 'hidden'
    }}>
      <div style={{ width: '100%', fontSize: '11px', color: '#94a3b8', marginBottom: '8px', display: 'flex', justifyContent: 'space-between' }}>
        <span>{ctrl.name || 'HUB75 RGB LED Matrix'}</span>
        <span style={{ fontFamily: 'monospace', color: '#38bdf8' }}>{cols}x{rows} RGB24</span>
      </div>
      <canvas
        ref={canvasRef}
        width={cols * 8}
        height={rows * 8}
        style={{
          maxHeight: fullHeight ? 'calc(100% - 30px)' : '480px',
          maxWidth: '100%',
          width: fullHeight ? 'auto' : '100%',
          height: fullHeight ? '100%' : 'auto',
          aspectRatio: `${cols} / ${rows}`,
          objectFit: 'contain',
          borderRadius: '4px',
          backgroundColor: '#020408',
          border: '1px solid #1e293b'
        }}
      />
    </div>
  );
}

function OledCanvas({ frameData, fullHeight = false, fitInside = false }) {
  const canvasRef = React.useRef(null);

  React.useEffect(() => {
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

    if (frameData) {
      try {
        const binaryString = atob(frameData);
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
      } catch (e) {}
    }

    ctx.putImageData(imgData, 0, 0);
  }, [frameData]);

  if (fitInside) {
    return (
      <canvas
        ref={canvasRef}
        width={128}
        height={64}
        style={{
          width: '100%',
          height: '100%',
          aspectRatio: '128 / 64',
          objectFit: 'fill',
          borderRadius: '2px',
          backgroundColor: '#040604',
          boxSizing: 'border-box',
          display: 'block',
          imageRendering: 'pixelated'
        }}
      />
    );
  }

  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      justifyContent: 'center',
      height: fullHeight ? '100%' : 'auto',
      flex: fullHeight ? 1 : 'none',
      backgroundColor: '#090d16',
      padding: '12px',
      borderRadius: '6px',
      border: '1px solid #1e293b',
      boxSizing: 'border-box',
      overflow: 'hidden'
    }}>
      <div style={{ width: '100%', fontSize: '11px', color: '#94a3b8', marginBottom: '8px', display: 'flex', justifyContent: 'space-between' }}>
        <span>SSD1306 OLED Display</span>
        <span style={{ fontFamily: 'monospace', color: '#38bdf8' }}>128x64 Mono</span>
      </div>
      <canvas
        ref={canvasRef}
        width={128}
        height={64}
        style={{
          maxHeight: fullHeight ? 'calc(100% - 30px)' : '320px',
          maxWidth: '100%',
          width: fullHeight ? 'auto' : '100%',
          height: fullHeight ? '100%' : 'auto',
          aspectRatio: '128 / 64',
          objectFit: 'contain',
          borderRadius: '4px',
          backgroundColor: '#040604',
          border: '1px solid #1e293b',
          imageRendering: 'pixelated'
        }}
      />
    </div>
  );
}

function GenericPeripheralPane(props) {
  const { pluginId, params, manifest: propManifest } = props;
  const { manifest: globalManifest, peripheralFrames, display7SegFrame, displayFrame } = useDashboard();

  const topDevices = globalManifest?.devices?.filter(d => d.ui_widget || d.compatible) || [];
  const i2cSlaves = globalManifest?.devices?.flatMap(d => d.i2c_slaves || []) || [];
  const spiSlaves = globalManifest?.devices?.flatMap(d => d.spi_slaves || []) || [];
  const allSlaves = [...topDevices, ...i2cSlaves, ...spiSlaves];

  const pId = params?.pluginId || pluginId;
  const pName = params?.manifest?.name;

  const matchedSlave = allSlaves.find(s => 
    (pName && s.name === pName) ||
    (pId && s.compatible?.includes(pId)) ||
    (pId && typeof pId === 'string' && s.compatible && pId.includes(s.compatible)) ||
    (s.name && pId && pId.includes(s.name))
  ) || allSlaves.find(s => s.ui_widget?.controls?.length > 0) || allSlaves[0];

  const slaveData = matchedSlave ? {
    ...matchedSlave,
    ...propManifest,
    ...params?.manifest,
    ui_widget: {
      ...matchedSlave.ui_widget,
      ...propManifest?.ui_widget,
      ...params?.manifest?.ui_widget,
      controls: (params?.manifest?.ui_widget?.controls?.length > 0 ? params.manifest.ui_widget.controls : null)
             || (propManifest?.ui_widget?.controls?.length > 0 ? propManifest.ui_widget.controls : null)
             || matchedSlave.ui_widget?.controls || [],
      board_svg_content: params?.manifest?.ui_widget?.board_svg_content 
                      || propManifest?.ui_widget?.board_svg_content 
                      || matchedSlave.ui_widget?.board_svg_content
    }
  } : (params?.manifest || propManifest || {});
  const title = slaveData?.ui_widget?.title || slaveData?.name || pId || "Generic Peripheral";
  const controls = slaveData?.ui_widget?.controls || [];

  const hasMatrix = controls.some(c => c.type === 'matrix_canvas' || c.type === 'matrix');
  const matrixCtrl = controls.find(c => c.type === 'matrix_canvas' || c.type === 'matrix');
  const hasOled = controls.some(c => c.type === 'canvas_stream' || c.type === 'oled') || Boolean(slaveData?.compatible?.includes('ssd1306'));
  const oledCtrl = controls.find(c => c.type === 'canvas_stream' || c.type === 'oled');

  const shmNameFromSlave = slaveData?.shm_name || slaveData?.extra_props?.shm_name || slaveData?.shm_path?.replace('/dev/shm/', '');
  const targetShmFile = shmNameFromSlave || matrixCtrl?.shm_file || 'fbb_hub75_0';
  const shmFrame = peripheralFrames[targetShmFile] || peripheralFrames['/dev/shm/' + targetShmFile] || peripheralFrames[matrixCtrl?.shm_file] || peripheralFrames['fbb_hub75_0'];
  const oledFrame = displayFrame || peripheralFrames['fbb_oled_0'] || peripheralFrames['/dev/shm/fbb_oled_0'] || peripheralFrames['fbb_i2c_oled'] || shmFrame;

  const [viewMode, setViewMode] = useState((hasMatrix || hasOled) ? 'display' : 'board');
  const [zoom, setZoom] = useState(100);
  const [controlValues, setControlValues] = useState({});

  const handleSliderChange = (name, value) => {
    const numVal = parseFloat(value);
    setControlValues((prev) => ({ ...prev, [name]: numVal }));
    if (socket && socket.emit) {
      socket.emit("peripheral:action", {
        pluginId: pId,
        action: "update_control",
        control: name,
        value: numVal,
      });

      // Handle ADC Voltage / Channel slider mapping to /dev/shm/spi_adc
      if (name === 'channel0' || name.startsWith('channel') || pId?.includes('mcp3208') || pId?.includes('adc')) {
        const chIdx = parseInt(name.replace(/\D/g, ''), 10) || 0;
        let rawAdc = numVal;
        if (numVal <= 3.3 && numVal >= 0) {
          rawAdc = Math.round((numVal / 3.3) * 4095);
        }
        rawAdc = Math.max(0, Math.min(4095, Math.round(rawAdc)));
        socket.emit('spi-adc-inject', { channel: chIdx, value: rawAdc });
      }
    }
  };

  return (
    <div style={{
      padding: '12px',
      color: '#e2e8f0',
      backgroundColor: '#1e293b',
      borderRadius: '6px',
      height: '100%',
      display: 'flex',
      flexDirection: 'column',
      boxSizing: 'border-box',
      overflow: 'hidden',
      fontFamily: 'Inter, system-ui, sans-serif'
    }}>
      <div style={{
        fontSize: '14px',
        fontWeight: '600',
        marginBottom: '10px',
        borderBottom: '1px solid #334155',
        paddingBottom: '6px',
        color: '#38bdf8',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        flex: '0 0 auto'
      }}>
        <span>{title}</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
          {/* Zoom Slider Control */}
          <div style={{
            display: 'flex',
            alignItems: 'center',
            gap: '6px',
            background: '#0f172a',
            padding: '2px 8px',
            borderRadius: '4px',
            border: '1px solid #334155'
          }}>
            <ZoomOut size={12} style={{ color: '#94a3b8' }} />
            <input
              type="range"
              min="50"
              max="400"
              step="25"
              value={zoom}
              onChange={(e) => setZoom(Number(e.target.value))}
              style={{ width: '60px', accentColor: '#38bdf8', cursor: 'pointer' }}
            />
            <ZoomIn size={12} style={{ color: '#94a3b8' }} />
            <span style={{ fontSize: '11px', fontFamily: 'monospace', minWidth: '32px', textAlign: 'right' }}>
              {zoom}%
            </span>
            <button
              onClick={() => setZoom(100)}
              title="Reset Zoom"
              style={{ background: 'none', border: 'none', color: '#94a3b8', cursor: 'pointer', padding: '1px', display: 'flex' }}
            >
              <RotateCcw size={12} />
            </button>
          </div>

          {slaveData?.ui_widget?.board_svg_content && (hasMatrix || hasOled) && (
            <div style={{ display: 'flex', background: '#0f172a', padding: '2px', borderRadius: '4px', border: '1px solid #334155' }}>
              <button
                onClick={() => setViewMode('display')}
                style={{
                  padding: '2px 8px',
                  fontSize: '11px',
                  border: 'none',
                  borderRadius: '3px',
                  cursor: 'pointer',
                  backgroundColor: viewMode === 'display' ? '#0284c7' : 'transparent',
                  color: viewMode === 'display' ? '#ffffff' : '#94a3b8',
                  fontWeight: viewMode === 'display' ? 'bold' : 'normal'
                }}
              >
                🖥️ Screen
              </button>
              <button
                onClick={() => setViewMode('board')}
                style={{
                  padding: '2px 8px',
                  fontSize: '11px',
                  border: 'none',
                  borderRadius: '3px',
                  cursor: 'pointer',
                  backgroundColor: viewMode === 'board' ? '#0284c7' : 'transparent',
                  color: viewMode === 'board' ? '#ffffff' : '#94a3b8',
                  fontWeight: viewMode === 'board' ? 'bold' : 'normal'
                }}
              >
                🔌 PCB Board
              </button>
            </div>
          )}
          <span style={{ fontSize: '11px', color: '#94a3b8', fontWeight: 'normal' }}>PPA 4.0 Plugin</span>
        </div>
      </div>

      {/* Outer Scaled Viewport Container */}
      <div style={{
        flex: 1,
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        justifyContent: 'center',
        overflow: 'auto',
        transform: `scale(${zoom / 100})`,
        transformOrigin: 'top center',
        transition: 'transform 0.1s ease-out'
      }}>
        {/* SVG Board Background Overlay (PCB View mode) */}
        {viewMode === 'board' && slaveData?.ui_widget?.board_svg_content && (() => {
          const rawSvg = slaveData.ui_widget.board_svg_content;
          const vbMatch = rawSvg.match(/viewBox=["']([^"']+)["']/i);
          const viewBoxStr = vbMatch ? vbMatch[1] : '0 0 480 360';
          const vbParts = viewBoxStr.split(/\s+/).map(Number);
          const aspect = (vbParts.length === 4 && vbParts[2] && vbParts[3]) ? `${vbParts[2]} / ${vbParts[3]}` : '480 / 360';

          return (
            <div 
              style={{ 
                position: 'relative',
                marginBottom: '12px', 
                borderRadius: '6px', 
                overflow: 'hidden', 
                display: 'flex', 
                justifyContent: 'center',
                backgroundColor: '#090d16',
                padding: '0px',
                border: '1px solid #1e293b',
                width: '100%',
                maxWidth: '520px',
                aspectRatio: aspect
              }}
            >
              <svg viewBox={viewBoxStr} width="100%" height="100%" style={{ display: 'block' }}>
                <g dangerouslySetInnerHTML={{ __html: extractSvgInnerHtml(rawSvg) }} />

              {/* Render 7-Segment LED if declared in controls */}
              {controls.filter(c => c.type === '7seg_display').map((ctrl, cIdx) => {
                const targetShmFile = ctrl.shm_file || slaveData?.shm_path?.replace('/dev/shm/', '') || 'fbb_display_7seg_0';
                const shmFrame = peripheralFrames[targetShmFile] || peripheralFrames['/dev/shm/' + targetShmFile] || display7SegFrame;
                let seg7Bytes = [0, 0, 0, 0];
                let seg7Colon = false;
                if (shmFrame) {
                  try {
                    const binaryString = atob(shmFrame);
                    const buffer = new Uint8Array(binaryString.length);
                    for (let i = 0; i < binaryString.length; i++) buffer[i] = binaryString.charCodeAt(i);
                    if (buffer.length >= 10) {
                      seg7Bytes = [buffer[0], buffer[2], buffer[6], buffer[8]];
                      seg7Colon = (buffer[4] & 0x02) !== 0;
                    }
                  } catch (e) {}
                }

                const colorMap = { red: '#ff0033', green: '#20ff40', blue: '#20a0ff', yellow: '#ffb000', white: '#f0f0f0' };
                const ledColor = colorMap[ctrl.color] || ctrl.color || '#ff0033';

                return (
                  <g key={cIdx} transform="translate(55, 52)">
                    <SevenSegDigit x={0} y={0} byte={seg7Bytes[0]} color={ledColor} />
                    <SevenSegDigit x={90} y={0} byte={seg7Bytes[1]} color={ledColor} />
                    {ctrl.has_colon && <SevenSegColon x={183} y={0} on={seg7Colon} color={ledColor} />}
                    <SevenSegDigit x={215} y={0} byte={seg7Bytes[2]} color={ledColor} />
                    <SevenSegDigit x={305} y={0} byte={seg7Bytes[3]} color={ledColor} />
                  </g>
                );
              })}
            </svg>

            {/* Embedded MatrixCanvas overlay inside PCB Board Screen window (Decoupled overlay_offset per device with PCB base window fallback) */}
            {hasMatrix && matrixCtrl && (() => {
              const defaultOffset = { left: '24.1667%', top: '5.5556%', width: '66.6667%', height: '88.8889%' };
              const offset = matrixCtrl.overlay_offset || slaveData?.overlay_offset || slaveData?.ui_widget?.overlay_offset || defaultOffset;
              return (
                <div style={{
                  position: 'absolute',
                  left: offset.left || '24.1667%',
                  top: offset.top || '5.5556%',
                  width: offset.width || '66.6667%',
                  height: offset.height || '88.8889%',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  overflow: 'hidden',
                  borderRadius: '4px',
                  pointerEvents: 'none',
                  boxSizing: 'border-box'
                }}>
                  <MatrixCanvas ctrl={matrixCtrl} frameData={shmFrame} slaveData={slaveData} fitInside />
                </div>
              );
            })()}

            {/* Embedded OledCanvas overlay inside PCB Board Screen window */}
            {hasOled && (() => {
              const defaultOledOffset = { left: '8.214%', top: '16.786%', width: '83.571%', height: '43.571%' };
              const offset = oledCtrl?.overlay_offset || slaveData?.overlay_offset || slaveData?.ui_widget?.overlay_offset || defaultOledOffset;
              return (
                <div style={{
                  position: 'absolute',
                  left: offset.left || '8.214%',
                  top: offset.top || '16.786%',
                  width: offset.width || '83.571%',
                  height: offset.height || '43.571%',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  overflow: 'hidden',
                  borderRadius: '3px',
                  pointerEvents: 'none',
                  boxSizing: 'border-box'
                }}>
                  <OledCanvas frameData={oledFrame} fitInside />
                </div>
              );
            })()}
          </div>
        );
      })()}

        {controls.length === 0 ? (
          <div style={{ color: '#94a3b8', fontSize: '12px', fontStyle: 'italic' }}>
            No custom UI controls specified in fbb-plugin.json
          </div>
        ) : (
          <div style={{ flex: 1, width: '100%', display: 'flex', flexDirection: 'column', gap: '12px', overflow: 'hidden' }}>
            {controls.map((ctrl, idx) => {
              if (ctrl.type === 'slider') {
                const val = controlValues[ctrl.name] !== undefined ? controlValues[ctrl.name] : (ctrl.default || ctrl.min || 0);
                return (
                  <div key={idx} style={{ backgroundColor: '#0f172a', padding: '8px', borderRadius: '4px' }}>
                    <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '12px', marginBottom: '4px' }}>
                      <span>{ctrl.label || ctrl.name}</span>
                      <span style={{ fontFamily: 'monospace', color: '#38bdf8' }}>{val}</span>
                    </div>
                    <input
                      type="range"
                      min={ctrl.min || 0}
                      max={ctrl.max || 100}
                      step={ctrl.step || 0.01}
                      value={val}
                      onChange={(e) => handleSliderChange(ctrl.name, e.target.value)}
                      style={{ width: '100%', cursor: 'pointer' }}
                    />
                  </div>
                );
              }

              if ((ctrl.type === 'matrix_canvas' || ctrl.type === 'matrix') && viewMode === 'display') {
                return (
                  <MatrixCanvas key={idx} ctrl={ctrl} frameData={shmFrame} slaveData={slaveData} fullHeight={true} />
                );
              }

              if ((ctrl.type === 'canvas_stream' || ctrl.type === 'oled') && viewMode === 'display') {
                return (
                  <OledCanvas key={idx} frameData={oledFrame} fullHeight={true} />
                );
              }

              if (ctrl.type === '7seg_display') {
                return (
                  <div key={idx} style={{ backgroundColor: '#0f172a', padding: '8px', borderRadius: '4px' }}>
                    <div style={{ fontSize: '12px', marginBottom: '4px', color: '#94a3b8', display: 'flex', justifyContent: 'space-between' }}>
                      <span>7-Segment LED Control</span>
                      <span style={{ color: '#38bdf8', textTransform: 'capitalize' }}>Color: {ctrl.color || 'red'}</span>
                    </div>
                  </div>
                );
              }

              return null;
            })}
          </div>
        )}
      </div>
    </div>
  );
}

export default GenericPeripheralPane;
