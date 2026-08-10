import React, { useState } from 'react';
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

/**
 * GenericPeripheralPane - F-BB Generic Peripheral View Spec Component (PPA ADR #005)
 * Decoupled PPA 2.0 component that renders declarative UI widgets and SVG board overlays provided by vendor plugins.
 */
export const GenericPeripheralPane = ({ pluginId, manifest: propManifest, socket, params }) => {
  const { manifest: globalManifest, peripheralFrames, display7SegFrame } = useDashboard();

  const i2cSlaves = globalManifest?.devices?.flatMap(d => d.i2c_slaves || []) || [];
  const spiSlaves = globalManifest?.devices?.flatMap(d => d.spi_slaves || []) || [];
  const allSlaves = [...i2cSlaves, ...spiSlaves];

  const pId = params?.pluginId || pluginId;
  const fallbackSlave = pId 
    ? allSlaves.find(s => s.compatible?.includes(pId) || s.compatible?.includes('ht16k33') || s.ui_widget?.title?.toLowerCase().includes('7-segment'))
    : allSlaves[0];

  const slaveData = params?.manifest || propManifest || fallbackSlave;
  const title = slaveData?.ui_widget?.title || slaveData?.name || pId || "Generic Peripheral";
  const controls = slaveData?.ui_widget?.controls || [];

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
      overflowY: 'auto',
      boxSizing: 'border-box',
      fontFamily: 'Inter, system-ui, sans-serif'
    }}>
      <div style={{
        fontSize: '14px',
        fontWeight: '600',
        marginBottom: '12px',
        borderBottom: '1px solid #334155',
        paddingBottom: '6px',
        color: '#38bdf8',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between'
      }}>
        <span>{title}</span>
        <span style={{ fontSize: '11px', color: '#94a3b8', fontWeight: 'normal' }}>PPA 2.0 Plugin</span>
      </div>

      {/* SVG Board Background Overlay (Integrated Vector ViewBox) */}
      {slaveData?.ui_widget?.board_svg_content && (
        <div 
          style={{ 
            marginBottom: '12px', 
            borderRadius: '6px', 
            overflow: 'hidden', 
            display: 'flex', 
            justifyContent: 'center',
            backgroundColor: '#090d16',
            padding: '8px',
            border: '1px solid #1e293b',
            maxHeight: '260px'
          }}
        >
          <svg viewBox="0 0 500 270" width="100%" height="100%">
            <g dangerouslySetInnerHTML={{ __html: extractSvgInnerHtml(slaveData.ui_widget.board_svg_content) }} />
            
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
        </div>
      )}

      {controls.length === 0 ? (
        <div style={{ color: '#94a3b8', fontSize: '12px', fontStyle: 'italic' }}>
          No custom UI controls specified in fbb-plugin.json
        </div>
      ) : (
        <div style={{ display: 'flex', flexDirection: 'column', gap: '12px' }}>
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

            if (ctrl.type === 'canvas_stream') {
              return (
                <div key={idx} style={{ backgroundColor: '#000', padding: '8px', borderRadius: '4px', textAlign: 'center' }}>
                  <div style={{ fontSize: '11px', color: '#64748b', marginBottom: '4px' }}>{ctrl.name} (Realtime Framebuffer)</div>
                  <canvas
                    width={ctrl.width || 128}
                    height={ctrl.height || 64}
                    style={{
                      border: '1px solid #334155',
                      imageRendering: 'pixelated',
                      width: '100%',
                      maxHeight: '180px',
                      backgroundColor: '#050505'
                    }}
                  />
                </div>
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

            return (
              <div key={idx} style={{ fontSize: '12px', color: '#94a3b8' }}>
                Widget [{ctrl.type}]: {ctrl.name}
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
};

export default GenericPeripheralPane;
