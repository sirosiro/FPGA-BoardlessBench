import React, { useState, useEffect, useRef, useCallback } from 'react';
import { useDashboard } from './DashboardContext';
import { MapPin, RotateCcw, ZoomIn, ZoomOut, Crosshair, Trash2 } from 'lucide-react';

const MAX_TRAIL_LENGTH = 1000;

export default function Ros2PoseMap2DPane() {
  const { socket } = useDashboard();
  const canvasRef = useRef(null);
  const containerRef = useRef(null);

  const [manifest, setManifest] = useState(null);
  const [pose, setPose] = useState({ x: 0, y: 0, theta: 0, linear_vel: 0, angular_vel: 0 });
  const trailRef = useRef([]);

  // Viewport transform: zoom (pixels per meter), pan offset (pixels in screen space)
  const [zoom, setZoom] = useState(250);
  const [pan, setPan] = useState({ x: 0, y: 0 });
  const isPanningRef = useRef(false);
  const lastMouseRef = useRef({ x: 0, y: 0 });

  // Fetch robot manifest for chassis dimensions
  useEffect(() => {
    fetch('/api/scenario/robot-manifest')
      .then((res) => (res.ok ? res.json() : null))
      .then((data) => {
        if (data) setManifest(data);
      })
      .catch((err) => console.warn('[PoseMap2D] Manifest fetch error:', err));
  }, []);

  // Socket telemetry listener
  useEffect(() => {
    if (!socket) return;

    const handleTelemetry = (data) => {
      if (!data || !data.pose) return;
      const p = data.pose;
      setPose(p);

      // Append to trail
      const trail = trailRef.current;
      const last = trail[trail.length - 1];
      if (!last || Math.hypot(p.x - last.x, p.y - last.y) > 0.005) {
        trail.push({ x: p.x, y: p.y, theta: p.theta });
        if (trail.length > MAX_TRAIL_LENGTH) {
          trail.shift();
        }
      }
    };

    socket.on('amr:telemetry', handleTelemetry);
    return () => {
      socket.off('amr:telemetry', handleTelemetry);
    };
  }, [socket]);

  // Center on robot
  const handleCenterRobot = useCallback(() => {
    if (!containerRef.current) return;
    setPan({
      x: -pose.x * zoom,
      y: pose.y * zoom
    });
  }, [pose.x, pose.y, zoom]);

  // Reset view to (0,0) origin
  const handleResetOrigin = () => {
    setPan({ x: 0, y: 0 });
    setZoom(250);
  };

  const handleClearTrail = () => {
    trailRef.current = [];
  };

  // Mouse pan/zoom handlers
  const handleWheel = (e) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.15 : 0.85;
    setZoom((prev) => Math.max(50, Math.min(800, prev * factor)));
  };

  const handleMouseDown = (e) => {
    if (e.button === 0) {
      isPanningRef.current = true;
      lastMouseRef.current = { x: e.clientX, y: e.clientY };
    }
  };

  const handleMouseMove = (e) => {
    if (!isPanningRef.current) return;
    const dx = e.clientX - lastMouseRef.current.x;
    const dy = e.clientY - lastMouseRef.current.y;
    lastMouseRef.current = { x: e.clientX, y: e.clientY };
    setPan((prev) => ({ x: prev.x + dx, y: prev.y + dy }));
  };

  const handleMouseUp = () => {
    isPanningRef.current = false;
  };

  // Canvas drawing effect
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !containerRef.current) return;

    const ctx = canvas.getContext('2d');
    const width = containerRef.current.clientWidth;
    const height = containerRef.current.clientHeight;

    const dpr = window.devicePixelRatio || 1;
    canvas.width = width * dpr;
    canvas.height = height * dpr;
    ctx.scale(dpr, dpr);

    // Dark background matching F-BB
    ctx.fillStyle = '#0a0e14';
    ctx.fillRect(0, 0, width, height);

    const originX = width / 2 + pan.x;
    const originY = height / 2 + pan.y;

    const toScreen = (wx, wy) => ({
      x: originX + wx * zoom,
      y: originY - wy * zoom
    });

    // 1. Draw Grid Lines
    const gridStep = 0.5;
    const minMetersX = -(originX) / zoom;
    const maxMetersX = (width - originX) / zoom;
    const minMetersY = -(height - originY) / zoom;
    const maxMetersY = (originY) / zoom;

    const startGridX = Math.floor(minMetersX / gridStep) * gridStep;
    const endGridX = Math.ceil(maxMetersX / gridStep) * gridStep;
    const startGridY = Math.floor(minMetersY / gridStep) * gridStep;
    const endGridY = Math.ceil(maxMetersY / gridStep) * gridStep;

    ctx.lineWidth = 1;
    for (let gx = startGridX; gx <= endGridX; gx += gridStep) {
      const sx = originX + gx * zoom;
      const isMajor = Math.abs(Math.round(gx) - gx) < 0.001;
      ctx.strokeStyle = isMajor ? '#21262d' : '#161b22';
      ctx.beginPath();
      ctx.moveTo(sx, 0);
      ctx.lineTo(sx, height);
      ctx.stroke();

      if (isMajor && zoom > 120) {
        ctx.fillStyle = '#6e7681';
        ctx.font = '9px monospace';
        ctx.fillText(`${gx.toFixed(1)}m`, sx + 3, height - 6);
      }
    }

    for (let gy = startGridY; gy <= endGridY; gy += gridStep) {
      const sy = originY - gy * zoom;
      const isMajor = Math.abs(Math.round(gy) - gy) < 0.001;
      ctx.strokeStyle = isMajor ? '#21262d' : '#161b22';
      ctx.beginPath();
      ctx.moveTo(0, sy);
      ctx.lineTo(width, sy);
      ctx.stroke();

      if (isMajor && zoom > 120) {
        ctx.fillStyle = '#6e7681';
        ctx.font = '9px monospace';
        ctx.fillText(`${gy.toFixed(1)}m`, 6, sy - 3);
      }
    }

    // World Axes at (0,0)
    ctx.lineWidth = 1.5;
    // X axis (Red)
    ctx.strokeStyle = '#f85149';
    ctx.beginPath();
    ctx.moveTo(originX, originY);
    ctx.lineTo(originX + 0.3 * zoom, originY);
    ctx.stroke();

    // Y axis (Green)
    ctx.strokeStyle = '#3fb950';
    ctx.beginPath();
    ctx.moveTo(originX, originY);
    ctx.lineTo(originX, originY - 0.3 * zoom);
    ctx.stroke();

    // 2. Draw Odometry Trajectory Trail
    const trail = trailRef.current;
    if (trail.length > 1) {
      ctx.lineWidth = 2;
      ctx.strokeStyle = '#58a6ff';
      ctx.beginPath();
      const firstPt = toScreen(trail[0].x, trail[0].y);
      ctx.moveTo(firstPt.x, firstPt.y);
      for (let i = 1; i < trail.length; i++) {
        const pt = toScreen(trail[i].x, trail[i].y);
        ctx.lineTo(pt.x, pt.y);
      }
      ctx.stroke();
    }

    // 3. Draw Robot Chassis
    const chassisWidth = manifest?.chassis?.chassis_width_m || 0.22;
    const chassisLength = manifest?.chassis?.chassis_length_m || 0.26;
    const wheelSeparation = manifest?.chassis?.wheel_separation_m || 0.16;
    const wheelRadius = manifest?.chassis?.wheel_radius_m || 0.033;

    const botScreen = toScreen(pose.x, pose.y);

    ctx.save();
    ctx.translate(botScreen.x, botScreen.y);
    ctx.rotate(-pose.theta);

    // Chassis Body (Rounded Rect)
    const bodyW = chassisLength * zoom;
    const bodyH = chassisWidth * zoom;
    ctx.fillStyle = '#161b22';
    ctx.strokeStyle = '#58a6ff';
    ctx.lineWidth = 2;

    ctx.beginPath();
    ctx.roundRect(-bodyW / 2, -bodyH / 2, bodyW, bodyH, 6);
    ctx.fill();
    ctx.stroke();

    // Wheels (Left and Right)
    const wheelW = wheelRadius * 2 * zoom;
    const wheelH = 0.03 * zoom;
    const wheelDistY = (wheelSeparation / 2) * zoom;

    ctx.fillStyle = '#8b949e';
    ctx.fillRect(-wheelW / 2, -wheelDistY - wheelH / 2, wheelW, wheelH);
    ctx.fillRect(-wheelW / 2, wheelDistY - wheelH / 2, wheelW, wheelH);

    // Front Caster Wheel
    ctx.fillStyle = '#c9d1d9';
    ctx.beginPath();
    ctx.arc(bodyW * 0.35, 0, 0.015 * zoom, 0, Math.PI * 2);
    ctx.fill();

    // Orientation Heading Arrow
    ctx.strokeStyle = '#d29922';
    ctx.fillStyle = '#d29922';
    ctx.lineWidth = 2.5;
    ctx.beginPath();
    ctx.moveTo(0, 0);
    ctx.lineTo(bodyW * 0.55, 0);
    ctx.stroke();

    // Arrowhead
    ctx.beginPath();
    ctx.moveTo(bodyW * 0.55, 0);
    ctx.lineTo(bodyW * 0.45, -4);
    ctx.lineTo(bodyW * 0.45, 4);
    ctx.closePath();
    ctx.fill();

    ctx.restore();
  }, [pose, pan, zoom, manifest]);

  const thetaDeg = ((pose.theta * 180) / Math.PI).toFixed(1);

  return (
    <div
      ref={containerRef}
      style={{
        display: 'flex',
        flexDirection: 'column',
        height: '100%',
        backgroundColor: '#0a0e14',
        color: '#e6edf3',
        overflow: 'hidden',
        position: 'relative',
        userSelect: 'none',
        fontFamily: 'Inter, -apple-system, sans-serif'
      }}
    >
      {/* Top Floating Telemetry Overlay */}
      <div style={{
        position: 'absolute',
        top: '8px',
        left: '8px',
        right: '8px',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        pointerEvents: 'none',
        zIndex: 10
      }}>
        <div style={{
          backgroundColor: 'rgba(22, 27, 34, 0.9)',
          border: '1px solid #30363d',
          borderRadius: '6px',
          padding: '6px 12px',
          boxShadow: '0 4px 12px rgba(0,0,0,0.5)',
          display: 'flex',
          alignItems: 'center',
          gap: '12px'
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '6px', color: '#58a6ff', fontWeight: 600, fontSize: '12px' }}>
            <MapPin size={14} />
            <span>2D Odometry Map</span>
          </div>
          <div style={{ width: '1px', height: '12px', backgroundColor: '#30363d' }} />
          <div style={{ fontFamily: 'monospace', fontSize: '11px', color: '#e6edf3', display: 'flex', alignItems: 'center', gap: '12px' }}>
            <span>
              X: <b style={{ color: '#58a6ff' }}>{pose.x.toFixed(3)}</b> m
            </span>
            <span>
              Y: <b style={{ color: '#3fb950' }}>{pose.y.toFixed(3)}</b> m
            </span>
            <span>
              θ: <b style={{ color: '#d29922' }}>{thetaDeg}°</b>
            </span>
            <span>
              v: <b style={{ color: '#f0f6fc' }}>{pose.linear_vel?.toFixed(2) || 0}</b> m/s
            </span>
          </div>
        </div>

        {/* Viewport Action Buttons */}
        <div style={{
          display: 'flex',
          alignItems: 'center',
          gap: '4px',
          pointerEvents: 'auto',
          backgroundColor: 'rgba(22, 27, 34, 0.9)',
          border: '1px solid #30363d',
          borderRadius: '6px',
          padding: '3px 6px'
        }}>
          <button
            onClick={handleCenterRobot}
            title="Center on Robot"
            style={{
              backgroundColor: 'transparent',
              border: 'none',
              color: '#c9d1d9',
              padding: '4px',
              borderRadius: '4px',
              cursor: 'pointer'
            }}
          >
            <Crosshair size={14} />
          </button>
          <button
            onClick={() => setZoom((z) => Math.min(800, z * 1.2))}
            title="Zoom In"
            style={{
              backgroundColor: 'transparent',
              border: 'none',
              color: '#c9d1d9',
              padding: '4px',
              borderRadius: '4px',
              cursor: 'pointer'
            }}
          >
            <ZoomIn size={14} />
          </button>
          <button
            onClick={() => setZoom((z) => Math.max(50, z * 0.8))}
            title="Zoom Out"
            style={{
              backgroundColor: 'transparent',
              border: 'none',
              color: '#c9d1d9',
              padding: '4px',
              borderRadius: '4px',
              cursor: 'pointer'
            }}
          >
            <ZoomOut size={14} />
          </button>
          <button
            onClick={handleResetOrigin}
            title="Reset Origin (0,0)"
            style={{
              backgroundColor: 'transparent',
              border: 'none',
              color: '#c9d1d9',
              padding: '4px',
              borderRadius: '4px',
              cursor: 'pointer'
            }}
          >
            <RotateCcw size={14} />
          </button>
          <button
            onClick={handleClearTrail}
            title="Clear Trajectory Trail"
            style={{
              backgroundColor: 'transparent',
              border: 'none',
              color: '#8b949e',
              padding: '4px',
              borderRadius: '4px',
              cursor: 'pointer'
            }}
          >
            <Trash2 size={14} />
          </button>
        </div>
      </div>

      {/* Main Interactive Canvas */}
      <canvas
        ref={canvasRef}
        onWheel={handleWheel}
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={handleMouseUp}
        style={{ width: '100%', height: '100%', cursor: 'grab' }}
      />

      {/* Bottom Right Scale / Info Tag */}
      <div style={{
        position: 'absolute',
        bottom: '8px',
        right: '8px',
        backgroundColor: 'rgba(22, 27, 34, 0.8)',
        border: '1px solid #30363d',
        color: '#8b949e',
        fontSize: '10px',
        padding: '2px 8px',
        borderRadius: '4px',
        pointerEvents: 'none',
        fontFamily: 'monospace'
      }}>
        Scale: 1m = {Math.round(zoom)}px | {manifest?.robot_type || 'AMR Differential Drive'}
      </div>
    </div>
  );
}
