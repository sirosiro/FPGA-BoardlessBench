import React, { useState } from 'react';

/**
 * GenericPeripheralPane - F-BB Generic Peripheral View Spec Component (PPA ADR #005)
 * Dynamically renders JSON declarative UI widgets or streaming canvas data provided by vendor plugins.
 */
export const GenericPeripheralPane = ({ pluginId, manifest, socket }) => {
  const title = manifest?.ui_widget?.title || manifest?.name || pluginId || "Generic Peripheral";
  const controls = manifest?.ui_widget?.controls || [];

  const [controlValues, setControlValues] = useState({});

  const handleSliderChange = (name, value) => {
    setControlValues((prev) => ({ ...prev, [name]: value }));
    if (socket && socket.emit) {
      socket.emit("peripheral:action", {
        pluginId,
        action: "update_control",
        control: name,
        value: parseFloat(value),
      });
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
        <span style={{ fontSize: '11px', color: '#94a3b8', fontWeight: 'normal' }}>PPA Plugin</span>
      </div>

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

            if (ctrl.type === 'hex_editor') {
              return (
                <div key={idx} style={{ backgroundColor: '#0f172a', padding: '8px', borderRadius: '4px' }}>
                  <div style={{ fontSize: '12px', marginBottom: '4px', color: '#94a3b8' }}>Memory Editor ({ctrl.size} Bytes)</div>
                  <div style={{ fontFamily: 'monospace', fontSize: '11px', color: '#10b981' }}>
                    0000: 10 10 10 10 10 10 10 10 ...
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
