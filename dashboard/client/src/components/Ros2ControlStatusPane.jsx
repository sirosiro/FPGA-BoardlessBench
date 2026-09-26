import { useState, useEffect } from 'react';
import { useDashboard } from './DashboardContext';
import { Gauge, CheckCircle2, AlertTriangle, ShieldCheck, Activity, Cpu } from 'lucide-react';

export default function Ros2ControlStatusPane() {
  const { socket } = useDashboard();
  const [telemetry, setTelemetry] = useState(null);

  useEffect(() => {
    if (!socket) return;
    const handleTelemetry = (data) => {
      setTelemetry(data);
    };
    socket.on('amr:telemetry', handleTelemetry);
    return () => {
      socket.off('amr:telemetry', handleTelemetry);
    };
  }, [socket]);

  const rtMetrics = telemetry?.rt_metrics || {
    target_hz: 1000,
    actual_hz: 1000,
    avg_jitter_us: 0,
    max_jitter_us: 0
  };

  const controllers = telemetry?.controllers || [
    { name: 'diff_drive_controller', type: 'diff_drive_controller/DiffDriveController', state: 'active' }
  ];

  const joints = telemetry?.joints || [];
  const cycleCount = telemetry?.cycle_count || 0;
  const isEstop = Boolean(telemetry?.safety?.estop ?? telemetry?.safety?.estop_active ?? false);
  const isFault = Boolean(telemetry?.safety?.fault ?? telemetry?.safety?.hardware_fault ?? false);

  const jitterColor = rtMetrics.avg_jitter_us < 200 ? '#3fb950' : rtMetrics.avg_jitter_us < 1000 ? '#d29922' : '#f85149';

  return (
    <div style={{
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
      gap: '12px'
    }}>
      {/* Header Banner */}
      <div style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        paddingBottom: '8px',
        borderBottom: '1px solid #30363d'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <Gauge size={16} color="#58a6ff" />
          <span style={{ fontWeight: 600, fontSize: '13px', color: '#f0f6fc', letterSpacing: '0.2px' }}>
            ros2_control Manager & Jitter Monitor
          </span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
          <span style={{ fontSize: '11px', color: '#8b949e' }}>Cycles:</span>
          <span style={{
            fontFamily: 'monospace',
            backgroundColor: '#161b22',
            border: '1px solid #30363d',
            padding: '2px 8px',
            borderRadius: '6px',
            color: '#58a6ff',
            fontWeight: 600,
            fontSize: '11px'
          }}>
            {cycleCount.toLocaleString()}
          </span>
        </div>
      </div>

      {/* 1kHz Real-time Metrics Cards */}
      <div style={{
        display: 'grid',
        gridTemplateColumns: 'repeat(3, 1fr)',
        gap: '8px'
      }}>
        <div style={{
          backgroundColor: '#161b22',
          border: '1px solid #30363d',
          borderRadius: '8px',
          padding: '10px 12px'
        }}>
          <div style={{ fontSize: '10px', color: '#8b949e', marginBottom: '4px' }}>Control Target</div>
          <div style={{ fontSize: '16px', fontFamily: 'monospace', fontWeight: 700, color: '#f0f6fc' }}>
            {rtMetrics.target_hz} <span style={{ fontSize: '10px', color: '#8b949e', fontWeight: 400 }}>Hz (1ms)</span>
          </div>
          <div style={{ fontSize: '10px', color: '#58a6ff', marginTop: '2px' }}>PREEMPT_RT UIO</div>
        </div>

        <div style={{
          backgroundColor: '#161b22',
          border: '1px solid #30363d',
          borderRadius: '8px',
          padding: '10px 12px'
        }}>
          <div style={{ fontSize: '10px', color: '#8b949e', marginBottom: '4px' }}>Avg Jitter</div>
          <div style={{ fontSize: '16px', fontFamily: 'monospace', fontWeight: 700, color: jitterColor }}>
            {rtMetrics.avg_jitter_us.toFixed(1)} <span style={{ fontSize: '10px', color: '#8b949e', fontWeight: 400 }}>μs</span>
          </div>
          <div style={{ fontSize: '10px', color: '#8b949e', marginTop: '2px' }}>Deterministic Sync</div>
        </div>

        <div style={{
          backgroundColor: '#161b22',
          border: '1px solid #30363d',
          borderRadius: '8px',
          padding: '10px 12px'
        }}>
          <div style={{ fontSize: '10px', color: '#8b949e', marginBottom: '4px' }}>Max Jitter</div>
          <div style={{ fontSize: '16px', fontFamily: 'monospace', fontWeight: 700, color: '#d29922' }}>
            {rtMetrics.max_jitter_us.toFixed(1)} <span style={{ fontSize: '10px', color: '#8b949e', fontWeight: 400 }}>μs</span>
          </div>
          <div style={{ fontSize: '10px', color: '#8b949e', marginTop: '2px' }}>Worst Latency Spike</div>
        </div>
      </div>

      {/* Controllers Lifecycle Card */}
      <div style={{
        backgroundColor: '#161b22',
        border: '1px solid #30363d',
        borderRadius: '8px',
        padding: '10px 12px'
      }}>
        <div style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          marginBottom: '8px',
          borderBottom: '1px solid #21262d',
          paddingBottom: '6px'
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600, fontSize: '11px', color: '#f0f6fc' }}>
            <Cpu size={14} color="#58a6ff" />
            <span>Active Controllers</span>
          </div>
          <span style={{ fontSize: '10px', color: '#8b949e' }}>Managed Lifecycle</span>
        </div>

        {controllers.map((ctrl) => (
          <div
            key={ctrl.name}
            style={{
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'space-between',
              backgroundColor: '#0d1117',
              border: '1px solid #21262d',
              borderRadius: '6px',
              padding: '6px 10px',
              marginBottom: '4px'
            }}
          >
            <div>
              <div style={{ fontWeight: 600, color: '#58a6ff', fontFamily: 'monospace', fontSize: '11px' }}>
                {ctrl.name}
              </div>
              <div style={{ fontSize: '10px', color: '#8b949e' }}>{ctrl.type}</div>
            </div>
            <span style={{
              display: 'flex',
              alignItems: 'center',
              gap: '4px',
              fontSize: '10px',
              fontWeight: 600,
              padding: '2px 8px',
              borderRadius: '999px',
              backgroundColor: 'rgba(63, 185, 80, 0.15)',
              color: '#3fb950',
              border: '1px solid #3fb950'
            }}>
              <CheckCircle2 size={11} />
              {ctrl.state}
            </span>
          </div>
        ))}
      </div>

      {/* Hardware Interface Joint Table */}
      <div style={{
        backgroundColor: '#161b22',
        border: '1px solid #30363d',
        borderRadius: '8px',
        padding: '10px 12px',
        flex: 1
      }}>
        <div style={{
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          marginBottom: '8px',
          borderBottom: '1px solid #21262d',
          paddingBottom: '6px'
        }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600, fontSize: '11px', color: '#f0f6fc' }}>
            <Activity size={14} color="#3fb950" />
            <span>Hardware Interface Handles</span>
          </div>
          <span style={{ fontSize: '10px', color: '#8b949e' }}>SystemInterface (/dev/uio0)</span>
        </div>

        <table style={{ width: '100%', borderCollapse: 'collapse', textAlign: 'left', fontSize: '11px' }}>
          <thead>
            <tr style={{ color: '#8b949e', borderBottom: '1px solid #30363d' }}>
              <th style={{ padding: '6px 4px' }}>Joint</th>
              <th style={{ padding: '6px 4px', textAlign: 'right' }}>Command (vel)</th>
              <th style={{ padding: '6px 4px', textAlign: 'right' }}>State (pos)</th>
              <th style={{ padding: '6px 4px', textAlign: 'right' }}>State (vel)</th>
              <th style={{ padding: '6px 4px', textAlign: 'right' }}>PWM</th>
            </tr>
          </thead>
          <tbody>
            {joints.length === 0 ? (
              <tr>
                <td colSpan={5} style={{ padding: '16px', textAlign: 'center', color: '#8b949e' }}>
                  Waiting for telemetry stream from /tmp/fbb_amr_telemetry.json...
                </td>
              </tr>
            ) : (
              joints.map((joint) => (
                <tr key={joint.name} style={{ borderBottom: '1px solid #21262d' }}>
                  <td style={{ padding: '6px 4px', fontWeight: 600, color: '#f0f6fc', fontFamily: 'monospace' }}>
                    {joint.name}
                  </td>
                  <td style={{ padding: '6px 4px', textAlign: 'right', fontFamily: 'monospace', color: '#58a6ff' }}>
                    {(joint.cmd_vel !== undefined ? joint.cmd_vel : joint.command) !== undefined ? Number(joint.cmd_vel !== undefined ? joint.cmd_vel : joint.command).toFixed(2) : '-'} <span style={{ fontSize: '9px', color: '#8b949e' }}>r/s</span>
                  </td>
                  <td style={{ padding: '6px 4px', textAlign: 'right', fontFamily: 'monospace', color: '#3fb950' }}>
                    {joint.state_pos !== undefined ? joint.state_pos.toFixed(1) : '-'} <span style={{ fontSize: '9px', color: '#8b949e' }}>rad</span>
                  </td>
                  <td style={{ padding: '6px 4px', textAlign: 'right', fontFamily: 'monospace', color: '#3fb950' }}>
                    {joint.state_vel !== undefined ? joint.state_vel.toFixed(2) : '-'} <span style={{ fontSize: '9px', color: '#8b949e' }}>r/s</span>
                  </td>
                  <td style={{ padding: '6px 4px', textAlign: 'right', fontFamily: 'monospace', color: '#bc8cff' }}>
                    {(joint.pwm_duty !== undefined ? joint.pwm_duty : joint.pwm) !== undefined ? (joint.pwm_duty !== undefined ? joint.pwm_duty : joint.pwm) : '-'}
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      </div>

      {/* Safety & Protocol Guard Status Footer */}
      <div style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        padding: '8px 12px',
        backgroundColor: '#161b22',
        border: '1px solid #30363d',
        borderRadius: '6px',
        fontSize: '11px'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
          <ShieldCheck size={14} color="#3fb950" />
          <span style={{ color: '#8b949e' }}>32-bit Wrap-around Guard:</span>
          <span style={{ color: '#3fb950', fontWeight: 600 }}>Active (Two's Compl.)</span>
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
          {isEstop ? (
            <span style={{
              display: 'flex',
              alignItems: 'center',
              gap: '4px',
              padding: '2px 8px',
              borderRadius: '999px',
              backgroundColor: 'rgba(248, 81, 73, 0.2)',
              color: '#f85149',
              fontWeight: 700,
              border: '1px solid #f85149'
            }}>
              <AlertTriangle size={12} /> E-STOP ENGAGED
            </span>
          ) : isFault ? (
            <span style={{
              display: 'flex',
              alignItems: 'center',
              gap: '4px',
              padding: '2px 8px',
              borderRadius: '999px',
              backgroundColor: 'rgba(210, 153, 34, 0.2)',
              color: '#d29922',
              fontWeight: 700,
              border: '1px solid #d29922'
            }}>
              <AlertTriangle size={12} /> FAULT DETECTED
            </span>
          ) : (
            <span style={{
              display: 'flex',
              alignItems: 'center',
              gap: '4px',
              padding: '2px 8px',
              borderRadius: '999px',
              backgroundColor: 'rgba(63, 185, 80, 0.15)',
              color: '#3fb950',
              fontWeight: 600,
              border: '1px solid #3fb950'
            }}>
              <CheckCircle2 size={12} /> SYSTEM HEALTHY
            </span>
          )}
        </div>
      </div>
    </div>
  );
}
