import React, { useState } from 'react';
import { ZoomIn, ZoomOut, RotateCcw } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function extractSvgInnerHtml(svgStr) {
  if (!svgStr) return '';
  const match = svgStr.match(/<svg[^>]*>([\s\S]*)<\/svg>/i);
  return match ? match[1] : svgStr;
}

// PPA 5.0 Vector Segment Component
function SevenSegDigit({ x, y, byte, color = '#ff0033' }) {
  const segA = (byte & 0x01) !== 0;
  const segB = (byte & 0x02) !== 0;
  const segC = (byte & 0x04) !== 0;
  const segD = (byte & 0x08) !== 0;
  const segE = (byte & 0x10) !== 0;
  const segF = (byte & 0x20) !== 0;
  const segG = (byte & 0x40) !== 0;
  const segDP = (byte & 0x80) !== 0;

  const colorMap = { red: '#ff0033', green: '#20ff40', blue: '#20a0ff', yellow: '#ffb000', white: '#f0f0f0' };
  const onColor = colorMap[color] || color || '#ff0033';
  const strokeColor = '#ffb3c1';
  const offColor = 'rgba(75, 12, 18, 0.35)';
  const glowFilter = `drop-shadow(0px 0px 4px #ffffff) drop-shadow(0px 0px 10px ${onColor}) drop-shadow(0px 0px 22px ${onColor})`;

  return (
    <g transform={`translate(${x}, ${y}) scale(2.65) skewX(-6)`}>
      <polygon points="4,2 26,2 21,7 9,7" fill={segA ? onColor : offColor} stroke={segA ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segA ? glowFilter : 'none' }} />
      <polygon points="27,3 27,23.5 22,21 22,8" fill={segB ? onColor : offColor} stroke={segB ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segB ? glowFilter : 'none' }} />
      <polygon points="27,26.5 27,47 22,42 22,29" fill={segC ? onColor : offColor} stroke={segC ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segC ? glowFilter : 'none' }} />
      <polygon points="4,48 26,48 21,43 9,43" fill={segD ? onColor : offColor} stroke={segD ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segD ? glowFilter : 'none' }} />
      <polygon points="3,26.5 3,47 8,42 8,29" fill={segE ? onColor : offColor} stroke={segE ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segE ? glowFilter : 'none' }} />
      <polygon points="3,3 3,23.5 8,21 8,8" fill={segF ? onColor : offColor} stroke={segF ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segF ? glowFilter : 'none' }} />
      <polygon points="4,25 8,22.5 22,22.5 26,25 22,27.5 8,27.5" fill={segG ? onColor : offColor} stroke={segG ? strokeColor : 'none'} strokeWidth="0.5" style={{ filter: segG ? glowFilter : 'none' }} />
      <circle cx="32" cy="46" r="2.8" fill={segDP ? onColor : offColor} stroke={segDP ? strokeColor : 'none'} strokeWidth="0.4" style={{ filter: segDP ? glowFilter : 'none' }} />
    </g>
  );
}

function SevenSegColon({ x, y, on, color = '#ff0033' }) {
  const colorMap = { red: '#ff0033', green: '#20ff40', blue: '#20a0ff', yellow: '#ffb000', white: '#f0f0f0' };
  const onColor = colorMap[color] || color || '#ff0033';
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

// PPA 5.0 Universal Data-Driven Framebuffer Renderer Component
function FramebufferCanvas({ ctrl, frameData, slaveData, fullHeight = false, fitInside = false }) {
  const canvasRef = React.useRef(null);

  const format = ctrl?.format || 'rgb24';
  const renderMode = ctrl?.render_mode || (format === 'rgb24' ? 'led_matrix' : 'pixelated');

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

  const cols = parsedSlaveGrid ? parsedSlaveGrid[0] : (parsedCtrlGrid ? parsedCtrlGrid[0] : (ctrl?.width || (format === 'mono_page_8' ? 128 : 64)));
  const rows = parsedSlaveGrid ? parsedSlaveGrid[1] : (parsedCtrlGrid ? parsedCtrlGrid[1] : (ctrl?.height || (format === 'mono_page_8' ? 64 : 64)));

  const palette = ctrl?.palette || { bg: '#040604', fg: '#00ff50' };

  React.useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const canvasW = renderMode === 'led_matrix' ? cols * 8 : cols;
    const canvasH = renderMode === 'led_matrix' ? rows * 8 : rows;

    if (canvas.width !== canvasW) canvas.width = canvasW;
    if (canvas.height !== canvasH) canvas.height = canvasH;

    if (format === 'mono_page_8') {
      const imgData = ctx.createImageData(cols, rows);
      const hexToRgb = (hex) => {
        const c = hex.replace('#', '');
        return [parseInt(c.substring(0, 2), 16) || 4, parseInt(c.substring(2, 4), 16) || 6, parseInt(c.substring(4, 6), 16) || 4];
      };
      const bgRgb = hexToRgb(palette.bg || '#040604');
      const fgRgb = hexToRgb(palette.fg || '#00ff50');

      for (let i = 0; i < imgData.data.length; i += 4) {
        imgData.data[i] = bgRgb[0];
        imgData.data[i + 1] = bgRgb[1];
        imgData.data[i + 2] = bgRgb[2];
        imgData.data[i + 3] = 255;
      }

      if (frameData) {
        try {
          const binaryString = atob(frameData);
          const buffer = new Uint8Array(binaryString.length);
          for (let i = 0; i < binaryString.length; i++) buffer[i] = binaryString.charCodeAt(i);

          const pages = Math.min(8, Math.floor(rows / 8));
          for (let page = 0; page < pages; page++) {
            for (let col = 0; col < cols; col++) {
              const byteIdx = page * cols + col;
              if (byteIdx >= buffer.length) break;

              const byte = buffer[byteIdx];
              for (let bit = 0; bit < 8; bit++) {
                if ((byte & (1 << bit)) !== 0) {
                  const x = col;
                  const y = page * 8 + bit;
                  if (y < rows) {
                    const pixelIdx = (y * cols + x) * 4;
                    imgData.data[pixelIdx] = fgRgb[0];
                    imgData.data[pixelIdx + 1] = fgRgb[1];
                    imgData.data[pixelIdx + 2] = fgRgb[2];
                    imgData.data[pixelIdx + 3] = 255;
                  }
                }
              }
            }
          }
        } catch (e) {}
      }
      ctx.putImageData(imgData, 0, 0);
    } else if (format === 'rgb24') {
      if (renderMode === 'led_matrix') {
        ctx.fillStyle = palette.bg || '#03060c';
        ctx.fillRect(0, 0, canvas.width, canvas.height);

        if (frameData) {
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
        }
      } else {
        const imgData = ctx.createImageData(cols, rows);
        if (frameData) {
          try {
            const binaryString = atob(frameData);
            const buffer = new Uint8Array(binaryString.length);
            for (let i = 0; i < binaryString.length; i++) buffer[i] = binaryString.charCodeAt(i);

            for (let i = 0; i < cols * rows; i++) {
              const srcIdx = i * 3;
              const dstIdx = i * 4;
              if (srcIdx + 2 < buffer.length) {
                imgData.data[dstIdx] = buffer[srcIdx];
                imgData.data[dstIdx + 1] = buffer[srcIdx + 1];
                imgData.data[dstIdx + 2] = buffer[srcIdx + 2];
                imgData.data[dstIdx + 3] = 255;
              }
            }
          } catch (e) {}
        }
        ctx.putImageData(imgData, 0, 0);
      }
    }
  }, [frameData, cols, rows, format, renderMode, palette]);

  if (fitInside) {
    return (
      <canvas
        ref={canvasRef}
        width={renderMode === 'led_matrix' ? cols * 8 : cols}
        height={renderMode === 'led_matrix' ? rows * 8 : rows}
        style={{
          width: '100%',
          height: '100%',
          aspectRatio: `${cols} / ${rows}`,
          objectFit: 'fill',
          borderRadius: '2px',
          backgroundColor: palette.bg || '#040604',
          boxSizing: 'border-box',
          display: 'block',
          imageRendering: renderMode === 'pixelated' ? 'pixelated' : 'auto'
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
        <span>{ctrl?.title || ctrl?.name || 'Universal Display Framebuffer'}</span>
        <span style={{ fontFamily: 'monospace', color: '#38bdf8' }}>{cols}x{rows} {format.toUpperCase()}</span>
      </div>
      <canvas
        ref={canvasRef}
        width={renderMode === 'led_matrix' ? cols * 8 : cols}
        height={renderMode === 'led_matrix' ? rows * 8 : rows}
        style={{
          maxHeight: fullHeight ? 'calc(100% - 30px)' : '360px',
          maxWidth: '100%',
          width: fullHeight ? 'auto' : '100%',
          height: fullHeight ? '100%' : 'auto',
          aspectRatio: `${cols} / ${rows}`,
          objectFit: 'contain',
          borderRadius: '4px',
          backgroundColor: palette.bg || '#040604',
          border: '1px solid #1e293b',
          imageRendering: renderMode === 'pixelated' ? 'pixelated' : 'auto'
        }}
      />
    </div>
  );
}

// PPA 5.0 Backward Compatibility Control Normalizer
function normalizeControlSpec(ctrl, slaveData) {
  if (!ctrl) return null;
  let type = ctrl.type;
  if (type === 'canvas_stream' || type === 'oled') {
    return {
      ...ctrl,
      type: 'framebuffer',
      format: ctrl.format || 'mono_page_8',
      width: ctrl.width || 128,
      height: ctrl.height || 64,
      palette: ctrl.palette || { bg: '#040604', fg: '#00ff50' },
      overlay_offset: ctrl.overlay_offset || { left: '13.25%', top: '20.0%', width: '73.5%', height: '36.75%' }
    };
  }
  if (type === 'matrix_canvas' || type === 'matrix') {
    return {
      ...ctrl,
      type: 'framebuffer',
      format: ctrl.format || 'rgb24',
      render_mode: ctrl.render_mode || 'led_matrix',
      width: ctrl.width || 64,
      height: ctrl.height || 64,
      overlay_offset: ctrl.overlay_offset || { left: '25.833%', top: '7.778%', width: '63.333%', height: '84.444%' }
    };
  }
  if (type === '7seg_display' || type === 'seg7') {
    return {
      ...ctrl,
      type: 'segment_array',
      digit_count: ctrl.digit_count || 4,
      has_colon: ctrl.has_colon !== false,
      color: ctrl.color || 'red'
    };
  }
  return ctrl;
}

function GenericPeripheralPane(props) {
  const { pluginId, params, manifest: propManifest } = props;
  const { socket, manifest: globalManifest, peripheralFrames, display7SegFrame, displayFrame } = useDashboard();

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
  const rawControls = slaveData?.ui_widget?.controls || [];
  const controls = rawControls.map(c => normalizeControlSpec(c, slaveData));
  const displayCtrls = controls.filter(c => c.type === 'framebuffer');
  const segmentCtrls = controls.filter(c => c.type === 'segment_array');
  const hasDisplayWidget = displayCtrls.length > 0 || segmentCtrls.length > 0;

  const [viewMode, setViewMode] = useState('board');
  const [zoom, setZoom] = useState(100);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const [isDragging, setIsDragging] = useState(false);
  const [dragStart, setDragStart] = useState({ x: 0, y: 0 });
  const [controlValues, setControlValues] = useState({});

  const handleMouseDown = (e) => {
    if (e.target.tagName === 'INPUT' || e.target.tagName === 'BUTTON') return;
    setIsDragging(true);
    setDragStart({ x: e.clientX - pan.x, y: e.clientY - pan.y });
  };

  const handleMouseMove = (e) => {
    if (!isDragging) return;
    setPan({
      x: e.clientX - dragStart.x,
      y: e.clientY - dragStart.y
    });
  };

  const handleMouseUp = () => {
    setIsDragging(false);
  };

  const handleResetZoomPan = () => {
    setZoom(100);
    setPan({ x: 0, y: 0 });
  };

  const handleControlChange = (ctrl, value) => {
    const numVal = parseFloat(value);
    setControlValues((prev) => ({ ...prev, [ctrl.name]: numVal }));
    if (socket && socket.emit) {
      socket.emit("peripheral:action", {
        pluginId: pId,
        action: "update_control",
        control: ctrl.name,
        value: numVal,
        shm_file: ctrl.shm_file || slaveData?.shm_name,
        shm_offset: ctrl.shm_offset,
        format: ctrl.format || 'uint16_le',
        min: ctrl.min,
        max: ctrl.max,
        raw_min: ctrl.raw_min,
        raw_max: ctrl.raw_max
      });
    }
  };

  return (
    <div style={{
      width: '100%',
      height: '100%',
      backgroundColor: '#0b0f19',
      color: '#f8fafc',
      display: 'flex',
      flexDirection: 'column',
      fontFamily: 'Inter, sans-serif',
      boxSizing: 'border-box',
      overflow: 'hidden'
    }}>
      {/* Header Bar */}
      <div style={{
        padding: '8px 12px',
        backgroundColor: '#0f172a',
        borderBottom: '1px solid #1e293b',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        flexWrap: 'wrap',
        gap: '8px'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <span style={{ fontWeight: 'bold', fontSize: '13px', color: '#38bdf8' }}>{title}</span>
        </div>

        {/* View Mode & Zoom Controls Toolbar */}
        <div style={{ display: 'flex', alignItems: 'center', gap: '12px' }}>
          {/* Zoom Slider */}
          <div style={{ display: 'flex', alignItems: 'center', gap: '6px', background: '#0f172a', padding: '2px 8px', borderRadius: '4px', border: '1px solid #334155' }}>
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
              onClick={handleResetZoomPan}
              title="Reset Zoom & Position"
              style={{ background: 'none', border: 'none', color: '#94a3b8', cursor: 'pointer', padding: '1px', display: 'flex' }}
            >
              <RotateCcw size={12} />
            </button>
          </div>

          {slaveData?.ui_widget?.board_svg_content && hasDisplayWidget && (
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
          <span style={{ fontSize: '11px', color: '#94a3b8', fontWeight: 'normal' }}>PPA 5.0 Standard</span>
        </div>
      </div>

      {/* Main Viewport Container */}
      <div 
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseUp}
        style={{
          flex: 1,
          width: '100%',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          overflow: 'hidden',
          position: 'relative',
          cursor: isDragging ? 'grabbing' : (zoom > 100 ? 'grab' : 'default'),
          userSelect: isDragging ? 'none' : 'auto'
        }}
      >
        <div style={{
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          width: '100%',
          transform: `translate(${pan.x}px, ${pan.y}px) scale(${zoom / 100})`,
          transformOrigin: 'center center',
          transition: isDragging ? 'none' : 'transform 0.1s ease-out'
        }}>
        {/* PCB Board SVG View Mode */}
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

                {/* Render Segment Displays on SVG */}
                {segmentCtrls.map((ctrl, cIdx) => {
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

                  return (
                    <g key={cIdx} transform="translate(55, 52)">
                      <SevenSegDigit x={0} y={0} byte={seg7Bytes[0]} color={ctrl.color} />
                      <SevenSegDigit x={90} y={0} byte={seg7Bytes[1]} color={ctrl.color} />
                      {ctrl.has_colon && <SevenSegColon x={183} y={0} on={seg7Colon} color={ctrl.color} />}
                      <SevenSegDigit x={215} y={0} byte={seg7Bytes[2]} color={ctrl.color} />
                      <SevenSegDigit x={305} y={0} byte={seg7Bytes[3]} color={ctrl.color} />
                    </g>
                  );
                })}
              </svg>

              {/* Render Absolute Framebuffer Overlays on SVG */}
              {displayCtrls.map((ctrl, dIdx) => {
                const targetShmFile = ctrl.shm_file || slaveData?.shm_name || 'fbb_hub75_0';
                const frameData = peripheralFrames[targetShmFile] || peripheralFrames['/dev/shm/' + targetShmFile] || displayFrame || peripheralFrames['fbb_oled_0'];
                const defaultOffset = ctrl.format === 'mono_page_8' 
                  ? { left: '13.25%', top: '20.0%', width: '73.5%', height: '36.75%' }
                  : { left: '25.833%', top: '7.778%', width: '63.333%', height: '84.444%' };
                let offset = ctrl.overlay_offset || slaveData?.overlay_offset || slaveData?.ui_widget?.overlay_offset || defaultOffset;

                // For 2:1 daisy-chain displays (e.g. 128x64) in Board view, adapt overlay to 2:1 aspect ratio with vertical blank space
                const gridVal = slaveData?.grid_size || ctrl?.grid_size;
                const isWide2x1 = Array.isArray(gridVal) && gridVal[0] === 128 && gridVal[1] === 64;
                if (isWide2x1) {
                  offset = {
                    left: '25.833%',
                    top: '28.889%',
                    width: '63.333%',
                    height: '42.222%'
                  };
                }

                return (
                  <div key={dIdx} style={{
                    position: 'absolute',
                    left: offset.left,
                    top: offset.top,
                    width: offset.width,
                    height: offset.height,
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    overflow: 'hidden',
                    borderRadius: '3px',
                    pointerEvents: 'none',
                    boxSizing: 'border-box'
                  }}>
                    <FramebufferCanvas ctrl={ctrl} frameData={frameData} slaveData={slaveData} fitInside />
                  </div>
                );
              })}
            </div>
          );
        })()}

        {/* Screen View Mode (Direct Full Display) */}
        {viewMode === 'display' && displayCtrls.length > 0 && (() => {
          const mainDisplay = displayCtrls[0];
          const targetShmFile = mainDisplay.shm_file || slaveData?.shm_name || 'fbb_hub75_0';
          const frameData = peripheralFrames[targetShmFile] || peripheralFrames['/dev/shm/' + targetShmFile] || displayFrame || peripheralFrames['fbb_oled_0'];
          return (
            <FramebufferCanvas ctrl={mainDisplay} frameData={frameData} slaveData={slaveData} fullHeight />
          );
        })()}

        {/* Dynamic Controls (Sliders, Inputs, Toggles) */}
        {controls.length > 0 && (
          <div style={{
            width: '100%',
            maxWidth: '520px',
            display: 'flex',
            flexDirection: 'column',
            gap: '10px',
            marginTop: '12px'
          }}>
            {controls.filter(c => c.type === 'slider' || c.type === 'input_slider').map((ctrl, idx) => {
              const val = controlValues[ctrl.name] !== undefined ? controlValues[ctrl.name] : (ctrl.default !== undefined ? ctrl.default : (ctrl.min || 0));
              return (
                <div key={idx} style={{
                  backgroundColor: '#0f172a',
                  padding: '8px 12px',
                  borderRadius: '4px',
                  border: '1px solid #334155',
                  display: 'flex',
                  flexDirection: 'column',
                  gap: '4px'
                }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '11px', color: '#94a3b8' }}>
                    <span>{ctrl.label || ctrl.name}</span>
                    <span style={{ fontFamily: 'monospace', color: '#38bdf8', fontWeight: 'bold' }}>
                      {typeof val === 'number' ? val.toFixed(2) : val} {ctrl.unit || ''}
                    </span>
                  </div>
                  <input
                    type="range"
                    min={ctrl.min !== undefined ? ctrl.min : 0}
                    max={ctrl.max !== undefined ? ctrl.max : 3.3}
                    step={ctrl.step !== undefined ? ctrl.step : 0.01}
                    value={val}
                    onChange={(e) => handleControlChange(ctrl, e.target.value)}
                    style={{ width: '100%', accentColor: '#38bdf8', cursor: 'pointer' }}
                  />
                </div>
              );
            })}
          </div>
        )}

        {controls.length === 0 && !slaveData?.ui_widget?.board_svg_content && (
          <div style={{ color: '#94a3b8', fontSize: '12px', fontStyle: 'italic' }}>
            No UI controls or display widgets specified in fbb-plugin.json
          </div>
        )}
        </div>
      </div>
    </div>
  );
}

export default GenericPeripheralPane;
