import React, { useState, useEffect, useRef, useCallback } from 'react';
import { useDashboard } from './DashboardContext';
import {
  Navigation,
  AlertOctagon,
  Zap,
  ArrowUp,
  ArrowDown,
  ArrowLeft,
  ArrowRight,
  Square
} from 'lucide-react';

export default function Ros2TeleopConsolePane() {
  const { socket } = useDashboard();
  const [manifest, setManifest] = useState(null);
  const [linearCmd, setLinearCmd] = useState(0.0);
  const [angularCmd, setAngularCmd] = useState(0.0);
  const [maxLinear, setMaxLinear] = useState(0.5);
  const [maxAngular, setMaxAngular] = useState(2.0);
  const [isEstop, setIsEstop] = useState(false);

  // Joystick pad refs
  const joystickRef = useRef(null);
  const isDraggingRef = useRef(false);
  const presetTimerRef = useRef(null);

  // Clean up preset timer on unmount
  useEffect(() => {
    return () => {
      if (presetTimerRef.current) {
        clearTimeout(presetTimerRef.current);
      }
    };
  }, []);

  // Fetch robot manifest for mission presets & limits
  useEffect(() => {
    fetch('/api/scenario/robot-manifest')
      .then((res) => (res.ok ? res.json() : null))
      .then((data) => {
        if (data) {
          setManifest(data);
          const maxLin = data.chassis?.max_linear_speed ?? data.limits?.max_linear_velocity;
          if (maxLin) {
            setMaxLinear(maxLin);
          }
          const maxAng = data.chassis?.max_angular_speed ?? data.limits?.max_angular_velocity;
          if (maxAng) {
            setMaxAngular(maxAng);
          }
        }
      })
      .catch((err) => console.warn('[Teleop] Manifest fetch error:', err));
  }, []);

  // Listen to telemetry for safety & status
  useEffect(() => {
    if (!socket) return;
    const handleTelemetry = (data) => {
      if (data?.safety) {
        setIsEstop(Boolean(data.safety.estop ?? data.safety.estop_active ?? false));
      }
    };
    socket.on('amr:telemetry', handleTelemetry);
    return () => {
      socket.off('amr:telemetry', handleTelemetry);
    };
  }, [socket]);

  // Command transmission
  const sendCommand = useCallback(
    (lin, ang) => {
      if (!socket) return;
      const numLin = parseFloat(lin);
      const numAng = parseFloat(ang);
      const safeLin = Number.isFinite(numLin) ? numLin : 0.0;
      const safeAng = Number.isFinite(numAng) ? numAng : 0.0;
      const clampedLin = Math.max(-maxLinear, Math.min(maxLinear, safeLin));
      const clampedAng = Math.max(-maxAngular, Math.min(maxAngular, safeAng));
      setLinearCmd(Number(clampedLin.toFixed(3)));
      setAngularCmd(Number(clampedAng.toFixed(3)));
      socket.emit('amr:command', {
        linear: Number(clampedLin.toFixed(3)),
        angular: Number(clampedAng.toFixed(3)),
        v: Number(clampedLin.toFixed(3)),
        w: Number(clampedAng.toFixed(3))
      });
    },
    [socket, maxLinear, maxAngular]
  );

  const handleStop = useCallback(() => {
    if (presetTimerRef.current) {
      clearTimeout(presetTimerRef.current);
      presetTimerRef.current = null;
    }
    sendCommand(0, 0);
  }, [sendCommand]);

  const handleToggleEstop = () => {
    if (presetTimerRef.current) {
      clearTimeout(presetTimerRef.current);
      presetTimerRef.current = null;
    }
    const nextState = !isEstop;
    setIsEstop(nextState);
    if (socket) {
      socket.emit('amr:estop', { active: nextState });
    }
  };

  const handleExecutePreset = useCallback((preset) => {
    if (presetTimerRef.current) {
      clearTimeout(presetTimerRef.current);
      presetTimerRef.current = null;
    }
    const lin = preset.linear !== undefined ? preset.linear : (preset.cmd?.v !== undefined ? preset.cmd.v : 0.0);
    const ang = preset.angular !== undefined ? preset.angular : (preset.cmd?.w !== undefined ? preset.cmd.w : 0.0);
    const dur = preset.duration !== undefined ? preset.duration : (preset.cmd?.duration !== undefined ? preset.cmd.duration : null);

    sendCommand(lin, ang);

    if (dur && Number.isFinite(dur) && dur > 0) {
      presetTimerRef.current = setTimeout(() => {
        sendCommand(0, 0);
        presetTimerRef.current = null;
      }, dur * 1000);
    }
  }, [sendCommand]);

  // Keyboard navigation (W, A, S, D, Space)
  useEffect(() => {
    const handleKeyDown = (e) => {
      if (['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName)) return;
      if (presetTimerRef.current) {
        clearTimeout(presetTimerRef.current);
        presetTimerRef.current = null;
      }

      const stepLin = maxLinear * 0.25;
      const stepAng = maxAngular * 0.25;

      switch (e.key.toLowerCase()) {
        case 'w':
        case 'arrowup':
          e.preventDefault();
          sendCommand(linearCmd + stepLin, angularCmd);
          break;
        case 's':
        case 'arrowdown':
          e.preventDefault();
          sendCommand(linearCmd - stepLin, angularCmd);
          break;
        case 'a':
        case 'arrowleft':
          e.preventDefault();
          sendCommand(linearCmd, angularCmd + stepAng);
          break;
        case 'd':
        case 'arrowright':
          e.preventDefault();
          sendCommand(linearCmd, angularCmd - stepAng);
          break;
        case ' ':
          e.preventDefault();
          handleStop();
          break;
        default:
          break;
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => {
      window.removeEventListener('keydown', handleKeyDown);
    };
  }, [linearCmd, angularCmd, maxLinear, maxAngular, sendCommand, handleStop]);

  // Virtual Joystick pointer handlers
  const updateJoystickPosition = (clientX, clientY) => {
    if (!joystickRef.current) return;
    const rect = joystickRef.current.getBoundingClientRect();
    const centerX = rect.left + rect.width / 2;
    const centerY = rect.top + rect.height / 2;
    const radius = rect.width / 2;

    const dx = clientX - centerX;
    const dy = clientY - centerY;

    const normX = Math.max(-1, Math.min(1, dx / radius));
    const normY = Math.max(-1, Math.min(1, dy / radius));

    const lin = -normY * maxLinear;
    const ang = -normX * maxAngular;

    sendCommand(lin, ang);
  };

  const handlePointerDown = (e) => {
    if (presetTimerRef.current) {
      clearTimeout(presetTimerRef.current);
      presetTimerRef.current = null;
    }
    isDraggingRef.current = true;
    updateJoystickPosition(e.clientX, e.clientY);
  };

  const handlePointerMove = (e) => {
    if (!isDraggingRef.current) return;
    updateJoystickPosition(e.clientX, e.clientY);
  };

  const handlePointerUp = () => {
    if (isDraggingRef.current) {
      isDraggingRef.current = false;
      handleStop();
    }
  };

  const missionPresets = manifest?.mission_presets || [
    { id: 'fwd', name: '直進 1.0m (Forward)', cmd: { v: 0.20, w: 0.0 } },
    { id: 'rev', name: '後退 1.0m (Reverse)', cmd: { v: -0.20, w: 0.0 } },
    { id: 'pivot_left', name: '左旋回 (Pivot Left)', cmd: { v: 0.0, w: 0.50 } },
    { id: 'spin_360', name: '360° 超信地旋回 (Spin 360°)', cmd: { v: 0.0, w: 1.00 } },
    { id: 's_curve', name: 'S字スラローム (S-Curve)', cmd: { v: 0.20, w: 0.40 } }
  ];

  return (
    <div
      style={{
        display: 'flex',
        flexDirection: 'column',
        height: '100%',
        backgroundColor: '#0d1117',
        color: '#e6edf3',
        fontFamily: 'Inter, -apple-system, sans-serif',
        fontSize: '12px',
        overflowY: 'auto',
        boxSizing: 'border-box',
        padding: '12px',
        gap: '12px',
        userSelect: 'none'
      }}
      onPointerUp={handlePointerUp}
    >
      {/* Header Bar */}
      <div style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        paddingBottom: '8px',
        borderBottom: '1px solid #30363d'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <Navigation size={16} color="#58a6ff" />
          <span style={{ fontWeight: 600, fontSize: '13px', color: '#f0f6fc', letterSpacing: '0.2px' }}>
            ROS 2 Teleop & E-STOP Console
          </span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
          <span style={{ fontSize: '11px', color: '#8b949e' }}>Topic:</span>
          <span style={{
            fontFamily: 'monospace',
            backgroundColor: '#161b22',
            border: '1px solid #30363d',
            padding: '2px 8px',
            borderRadius: '6px',
            color: '#58a6ff',
            fontSize: '11px'
          }}>
            /diff_drive_controller/cmd_vel
          </span>
        </div>
      </div>

      {/* Big Guarded E-STOP Bar */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
        <button
          onClick={handleToggleEstop}
          style={{
            flex: 1,
            padding: '10px 16px',
            borderRadius: '8px',
            fontWeight: 700,
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            gap: '8px',
            cursor: 'pointer',
            backgroundColor: isEstop ? '#b62324' : '#30181b',
            color: isEstop ? '#ffffff' : '#ff7b72',
            border: isEstop ? '2px solid #ff7b72' : '1px solid #f85149',
            boxShadow: isEstop ? '0 0 12px rgba(248, 81, 73, 0.6)' : 'none',
            fontSize: '13px',
            letterSpacing: '0.5px'
          }}
        >
          <AlertOctagon size={18} />
          <span>{isEstop ? 'EMERGENCY STOP ENGAGED (CLICK TO CLEAR)' : 'HARDWARE E-STOP'}</span>
        </button>

        <button
          onClick={handleStop}
          title="Zero Twist (Space)"
          style={{
            padding: '10px 16px',
            borderRadius: '8px',
            backgroundColor: '#21262d',
            border: '1px solid #30363d',
            color: '#e6edf3',
            fontWeight: 600,
            display: 'flex',
            alignItems: 'center',
            gap: '6px',
            cursor: 'pointer',
            fontSize: '12px'
          }}
        >
          <Square size={14} color="#d29922" fill="#d29922" />
          <span>ZERO (Space)</span>
        </button>
      </div>

      {/* Control Split: Joystick on Left, Presets & D-Pad on Right */}
      <div style={{
        display: 'grid',
        gridTemplateColumns: '1fr 1fr',
        gap: '10px',
        flex: 1,
        minHeight: '200px'
      }}>
        {/* Left: Virtual Joystick */}
        <div style={{
          backgroundColor: '#161b22',
          border: '1px solid #30363d',
          borderRadius: '8px',
          padding: '12px',
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center'
        }}>
          <div style={{ fontSize: '11px', color: '#8b949e', marginBottom: '8px', fontWeight: 500 }}>
            Virtual 2D Joystick (Touch / Drag / WASD)
          </div>

          <div
            ref={joystickRef}
            onPointerDown={handlePointerDown}
            onPointerMove={handlePointerMove}
            style={{
              width: '120px',
              height: '120px',
              borderRadius: '50%',
              border: '2px solid #30363d',
              backgroundColor: '#0d1117',
              position: 'relative',
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              cursor: 'crosshair',
              boxShadow: 'inset 0 0 10px rgba(0,0,0,0.5)'
            }}
          >
            {/* Crosshairs */}
            <div style={{ position: 'absolute', width: '100%', height: '1px', backgroundColor: '#21262d' }} />
            <div style={{ position: 'absolute', height: '100%', width: '1px', backgroundColor: '#21262d' }} />

            {/* Handle Thumb */}
            <div
              style={{
                width: '32px',
                height: '32px',
                borderRadius: '50%',
                backgroundColor: '#58a6ff',
                border: '2px solid #79c0ff',
                boxShadow: '0 0 10px rgba(88, 166, 255, 0.5)',
                pointerEvents: 'none',
                transform: `translate(${-((angularCmd / maxAngular) * 44)}px, ${-((linearCmd / maxLinear) * 44)}px)`,
                transition: 'transform 0.05s ease-out'
              }}
            />
          </div>

          <div style={{
            display: 'flex',
            alignItems: 'center',
            gap: '12px',
            marginTop: '10px',
            fontFamily: 'monospace',
            fontSize: '11px'
          }}>
            <span style={{ color: '#58a6ff' }}>v: {linearCmd.toFixed(2)} m/s</span>
            <span style={{ color: '#3fb950' }}>ω: {angularCmd.toFixed(2)} rad/s</span>
          </div>
        </div>

        {/* Right: Presets & Nudge D-Pad */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '8px' }}>
          {/* Mission Presets */}
          <div style={{
            backgroundColor: '#161b22',
            border: '1px solid #30363d',
            borderRadius: '8px',
            padding: '10px',
            flex: 1
          }}>
            <div style={{
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              marginBottom: '8px'
            }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600, fontSize: '11px', color: '#f0f6fc' }}>
                <Zap size={14} color="#d29922" />
                <span>Mission Presets</span>
              </div>
              <span style={{ fontSize: '10px', color: '#8b949e' }}>amr_manifest.json</span>
            </div>

            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '6px' }}>
              {missionPresets.map((preset, idx) => {
                const label = preset.name || preset.label || `Preset ${idx + 1}`;
                const lin = preset.linear !== undefined ? preset.linear : (preset.cmd?.v !== undefined ? preset.cmd.v : 0.0);
                const ang = preset.angular !== undefined ? preset.angular : (preset.cmd?.w !== undefined ? preset.cmd.w : 0.0);
                return (
                  <button
                    key={preset.id || preset.name || idx}
                    onClick={() => handleExecutePreset(preset)}
                    style={{
                      backgroundColor: '#21262d',
                      border: '1px solid #30363d',
                      borderRadius: '6px',
                      padding: '6px 8px',
                      textAlign: 'left',
                      cursor: 'pointer',
                      display: 'flex',
                      flexDirection: 'column',
                      color: '#e6edf3',
                      transition: 'background-color 0.15s, border-color 0.15s'
                    }}
                    onMouseEnter={(e) => {
                      e.currentTarget.style.backgroundColor = '#30363d';
                      e.currentTarget.style.borderColor = '#58a6ff';
                    }}
                    onMouseLeave={(e) => {
                      e.currentTarget.style.backgroundColor = '#21262d';
                      e.currentTarget.style.borderColor = '#30363d';
                    }}
                  >
                    <span style={{ fontWeight: 600, fontSize: '11px', color: '#f0f6fc' }}>{label}</span>
                    <span style={{ fontFamily: 'monospace', fontSize: '10px', color: '#8b949e', marginTop: '2px' }}>
                      v={lin} | ω={ang}{preset.cmd?.duration ? ` (${preset.cmd.duration}s)` : ''}
                    </span>
                  </button>
                );
              })}
            </div>
          </div>

          {/* D-Pad Buttons for Quick Nudge */}
          <div style={{
            backgroundColor: '#161b22',
            border: '1px solid #30363d',
            borderRadius: '8px',
            padding: '8px',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-around'
          }}>
            <button
              onClick={() => sendCommand(linearCmd, angularCmd + maxAngular * 0.2)}
              title="Turn Left (A)"
              style={{
                backgroundColor: '#21262d',
                border: '1px solid #30363d',
                borderRadius: '6px',
                padding: '8px',
                color: '#3fb950',
                cursor: 'pointer'
              }}
            >
              <ArrowLeft size={16} />
            </button>

            <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
              <button
                onClick={() => sendCommand(linearCmd + maxLinear * 0.2, angularCmd)}
                title="Forward (W)"
                style={{
                  backgroundColor: '#21262d',
                  border: '1px solid #30363d',
                  borderRadius: '6px',
                  padding: '8px',
                  color: '#58a6ff',
                  cursor: 'pointer'
                }}
              >
                <ArrowUp size={16} />
              </button>
              <button
                onClick={() => sendCommand(linearCmd - maxLinear * 0.2, angularCmd)}
                title="Backward (S)"
                style={{
                  backgroundColor: '#21262d',
                  border: '1px solid #30363d',
                  borderRadius: '6px',
                  padding: '8px',
                  color: '#58a6ff',
                  cursor: 'pointer'
                }}
              >
                <ArrowDown size={16} />
              </button>
            </div>

            <button
              onClick={() => sendCommand(linearCmd, angularCmd - maxAngular * 0.2)}
              title="Turn Right (D)"
              style={{
                backgroundColor: '#21262d',
                border: '1px solid #30363d',
                borderRadius: '6px',
                padding: '8px',
                color: '#3fb950',
                cursor: 'pointer'
              }}
            >
              <ArrowRight size={16} />
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
