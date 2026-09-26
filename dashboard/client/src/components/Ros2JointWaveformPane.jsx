import { useState, useEffect, useRef } from 'react';
import { useDashboard } from './DashboardContext';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer
} from 'recharts';
import { Activity, Play, Pause, Trash2 } from 'lucide-react';

const MAX_HISTORY_POINTS = 50;

export default function Ros2JointWaveformPane() {
  const { socket } = useDashboard();
  const [history, setHistory] = useState([]);
  const [isPaused, setIsPaused] = useState(false);
  const [selectedJoint, setSelectedJoint] = useState('all');
  const isPausedRef = useRef(false);

  useEffect(() => {
    isPausedRef.current = isPaused;
  }, [isPaused]);

  useEffect(() => {
    if (!socket) return;

    const handleTelemetry = (data) => {
      if (isPausedRef.current || !data || !data.joints) return;

      const timeLabel = new Date(data.timestamp_ms || Date.now()).toLocaleTimeString([], {
        hour12: false,
        minute: '2-digit',
        second: '2-digit',
        fractionalSecondDigits: 1
      });

      const entry = {
        time: timeLabel,
        cycle: data.cycle_count
      };

      data.joints.forEach((joint) => {
        const prefix = joint.name.includes('left') ? 'L' : joint.name.includes('right') ? 'R' : joint.name;
        const cmdVal = joint.cmd_vel !== undefined ? joint.cmd_vel : (joint.command !== undefined ? joint.command : 0);
        const velVal = joint.state_vel !== undefined ? joint.state_vel : 0;
        const pwmVal = joint.pwm_duty !== undefined ? joint.pwm_duty : (joint.pwm !== undefined ? joint.pwm : 0);
        entry[`${prefix}_cmd`] = Number(cmdVal.toFixed(2));
        entry[`${prefix}_vel`] = Number(velVal.toFixed(2));
        entry[`${prefix}_pwm`] = Number(pwmVal);
        entry[`${prefix}_err`] = Number((cmdVal - velVal).toFixed(2));
      });

      setHistory((prev) => {
        const updated = [...prev, entry];
        if (updated.length > MAX_HISTORY_POINTS) {
          return updated.slice(updated.length - MAX_HISTORY_POINTS);
        }
        return updated;
      });
    };

    socket.on('amr:telemetry', handleTelemetry);
    return () => {
      socket.off('amr:telemetry', handleTelemetry);
    };
  }, [socket]);

  const handleClear = () => {
    setHistory([]);
  };

  const latest = history[history.length - 1] || {};

  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      backgroundColor: '#0d1117',
      color: '#e6edf3',
      fontFamily: 'Inter, -apple-system, sans-serif',
      fontSize: '12px',
      overflow: 'hidden',
      boxSizing: 'border-box',
      padding: '12px',
      gap: '10px'
    }}>
      {/* Header Bar */}
      <div style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        paddingBottom: '8px',
        borderBottom: '1px solid #30363d'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <Activity size={16} color="#3fb950" />
          <span style={{ fontWeight: 600, fontSize: '13px', color: '#f0f6fc', letterSpacing: '0.2px' }}>
            ROS 2 Joint Tracking Waveform
          </span>
        </div>

        {/* Controls */}
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <select
            value={selectedJoint}
            onChange={(e) => setSelectedJoint(e.target.value)}
            style={{
              backgroundColor: '#161b22',
              color: '#f0f6fc',
              border: '1px solid #30363d',
              borderRadius: '6px',
              padding: '4px 8px',
              fontSize: '11px',
              outline: 'none',
              cursor: 'pointer'
            }}
          >
            <option value="all">All Joints (L/R)</option>
            <option value="left">Left Wheel Joint</option>
            <option value="right">Right Wheel Joint</option>
            <option value="error">Tracking Errors (Δv)</option>
          </select>

          <button
            onClick={() => setIsPaused((prev) => !prev)}
            title={isPaused ? 'Resume Waveform' : 'Pause Waveform'}
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '4px',
              backgroundColor: isPaused ? 'rgba(210, 153, 34, 0.2)' : '#21262d',
              color: isPaused ? '#d29922' : '#c9d1d9',
              border: isPaused ? '1px solid #d29922' : '1px solid #30363d',
              borderRadius: '6px',
              padding: '4px 8px',
              fontSize: '11px',
              cursor: 'pointer',
              transition: 'background-color 0.2s'
            }}
          >
            {isPaused ? <Play size={12} /> : <Pause size={12} />}
            <span>{isPaused ? 'Resume' : 'Pause'}</span>
          </button>

          <button
            onClick={handleClear}
            title="Clear history"
            style={{
              display: 'flex',
              alignItems: 'center',
              backgroundColor: '#21262d',
              color: '#8b949e',
              border: '1px solid #30363d',
              borderRadius: '6px',
              padding: '4px 8px',
              cursor: 'pointer'
            }}
          >
            <Trash2 size={13} />
          </button>
        </div>
      </div>

      {/* Instantaneous KPI Ribbon */}
      <div style={{
        display: 'grid',
        gridTemplateColumns: 'repeat(4, 1fr)',
        gap: '8px'
      }}>
        <div style={{
          backgroundColor: '#161b22',
          border: '1px solid #30363d',
          borderRadius: '6px',
          padding: '6px 10px',
          display: 'flex',
          flexDirection: 'column'
        }}>
          <span style={{ fontSize: '10px', color: '#8b949e' }}>Left Cmd / Vel</span>
          <span style={{ fontFamily: 'monospace', color: '#58a6ff', fontWeight: 600, fontSize: '11px', marginTop: '2px' }}>
            {latest.L_cmd ?? 0} / {latest.L_vel ?? 0} <span style={{ fontSize: '9px', color: '#8b949e', fontWeight: 400 }}>rad/s</span>
          </span>
        </div>

        <div style={{
          backgroundColor: '#161b22',
          border: '1px solid #30363d',
          borderRadius: '6px',
          padding: '6px 10px',
          display: 'flex',
          flexDirection: 'column'
        }}>
          <span style={{ fontSize: '10px', color: '#8b949e' }}>Right Cmd / Vel</span>
          <span style={{ fontFamily: 'monospace', color: '#3fb950', fontWeight: 600, fontSize: '11px', marginTop: '2px' }}>
            {latest.R_cmd ?? 0} / {latest.R_vel ?? 0} <span style={{ fontSize: '9px', color: '#8b949e', fontWeight: 400 }}>rad/s</span>
          </span>
        </div>

        <div style={{
          backgroundColor: '#161b22',
          border: '1px solid #30363d',
          borderRadius: '6px',
          padding: '6px 10px',
          display: 'flex',
          flexDirection: 'column'
        }}>
          <span style={{ fontSize: '10px', color: '#8b949e' }}>Left / Right PWM</span>
          <span style={{ fontFamily: 'monospace', color: '#bc8cff', fontWeight: 600, fontSize: '11px', marginTop: '2px' }}>
            {latest.L_pwm ?? 0} / {latest.R_pwm ?? 0} <span style={{ fontSize: '9px', color: '#8b949e', fontWeight: 400 }}>/1000</span>
          </span>
        </div>

        <div style={{
          backgroundColor: '#161b22',
          border: '1px solid #30363d',
          borderRadius: '6px',
          padding: '6px 10px',
          display: 'flex',
          flexDirection: 'column'
        }}>
          <span style={{ fontSize: '10px', color: '#8b949e' }}>Tracking Error Δv</span>
          <span style={{ fontFamily: 'monospace', color: '#d29922', fontWeight: 600, fontSize: '11px', marginTop: '2px' }}>
            L:{latest.L_err ?? 0} | R:{latest.R_err ?? 0} <span style={{ fontSize: '9px', color: '#8b949e', fontWeight: 400 }}>rad/s</span>
          </span>
        </div>
      </div>

      {/* Chart Canvas */}
      <div style={{ flex: 1, width: '100%', minHeight: '160px', backgroundColor: '#161b22', border: '1px solid #30363d', borderRadius: '8px', padding: '8px', boxSizing: 'border-box' }}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart data={history} margin={{ top: 5, right: 20, left: -10, bottom: 0 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="#21262d" />
            <XAxis dataKey="time" stroke="#8b949e" tick={{ fontSize: 9 }} minTickGap={25} />
            <YAxis
              stroke="#8b949e"
              tick={{ fontSize: 9 }}
              domain={['auto', 'auto']}
              unit=" r/s"
            />
            <Tooltip
              contentStyle={{
                backgroundColor: '#161b22',
                borderColor: '#30363d',
                fontSize: '11px',
                borderRadius: '6px',
                color: '#f0f6fc'
              }}
            />
            <Legend
              wrapperStyle={{ fontSize: '10px', paddingTop: '4px' }}
              iconType="circle"
            />

            {(selectedJoint === 'all' || selectedJoint === 'left') && (
              <Line
                type="monotone"
                dataKey="L_cmd"
                name="L Cmd Vel"
                stroke="#58a6ff"
                strokeWidth={1.5}
                dot={false}
                strokeDasharray="4 2"
                isAnimationActive={false}
              />
            )}
            {(selectedJoint === 'all' || selectedJoint === 'left') && (
              <Line
                type="monotone"
                dataKey="L_vel"
                name="L State Vel"
                stroke="#1f6feb"
                strokeWidth={2}
                dot={false}
                isAnimationActive={false}
              />
            )}

            {(selectedJoint === 'all' || selectedJoint === 'right') && (
              <Line
                type="monotone"
                dataKey="R_cmd"
                name="R Cmd Vel"
                stroke="#3fb950"
                strokeWidth={1.5}
                dot={false}
                strokeDasharray="4 2"
                isAnimationActive={false}
              />
            )}
            {(selectedJoint === 'all' || selectedJoint === 'right') && (
              <Line
                type="monotone"
                dataKey="R_vel"
                name="R State Vel"
                stroke="#238636"
                strokeWidth={2}
                dot={false}
                isAnimationActive={false}
              />
            )}

            {selectedJoint === 'error' && (
              <Line
                type="monotone"
                dataKey="L_err"
                name="L Error (Δv)"
                stroke="#d29922"
                strokeWidth={2}
                dot={false}
                isAnimationActive={false}
              />
            )}
            {selectedJoint === 'error' && (
              <Line
                type="monotone"
                dataKey="R_err"
                name="R Error (Δv)"
                stroke="#f85149"
                strokeWidth={2}
                dot={false}
                isAnimationActive={false}
              />
            )}
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}
