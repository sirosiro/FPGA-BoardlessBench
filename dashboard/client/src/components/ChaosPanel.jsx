import { useState, useEffect, useRef } from 'react';
import { 
  Zap, Play, Square, RotateCcw, Copy, Check, Dices, 
  Terminal, ShieldAlert, Cpu, AlertTriangle, RefreshCw, Trash2 
} from 'lucide-react';
import { useDashboard } from './DashboardContext';

const ChaosPanel = () => {
  const { socket, manifest } = useDashboard();

  // Scenario & Core lifecycle state
  const [scenarioName, setScenarioName] = useState('');
  const [isRunning, setIsRunning] = useState(false);
  const [cores, setCores] = useState([{ id: 'acore', label: 'A-Core (Linux / test_bin)', running: false }]);
  const [selectedCores, setSelectedCores] = useState({ acore: true });

  // Chaos engine configuration
  const [chaosEnabled, setChaosEnabled] = useState(false);
  const [chaosMode, setChaosMode] = useState('random'); // 'off', 'random', 'fixed'
  const [seed, setSeed] = useState('');
  const [rate, setRate] = useState(20); // 20%
  const [targets, setTargets] = useState({
    i2c: true,
    spi: true,
    uio: true,
    can: true,
    cdma: true
  });

  // Logs & Injected faults
  const [logs, setLogs] = useState([]);
  const [faultEvents, setFaultEvents] = useState([]);
  const [copied, setCopied] = useState(false);
  const logEndRef = useRef(null);

  // Auto-scroll logs
  useEffect(() => {
    if (logEndRef.current) {
      logEndRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [logs, faultEvents]);

  // Initial fetch and socket listeners
  useEffect(() => {
    // Fetch initial status via REST API
    fetch('/api/scenario/status')
      .then(r => r.json())
      .then(data => {
        if (data) {
          setIsRunning(data.running);
          setScenarioName(data.scenario || (manifest?.model ? manifest.model.replace('zynq7000-vfpga-', '') : ''));
          if (data.cores && data.cores.length > 0) {
            setCores(data.cores);
            const sel = {};
            data.cores.forEach(c => sel[c.id] = true);
            setSelectedCores(sel);
          }
          if (data.chaos) {
            setChaosEnabled(data.chaos.enabled);
            setChaosMode(data.chaos.mode || (data.chaos.enabled ? 'random' : 'off'));
            setSeed(data.chaos.seed || '');
            if (data.chaos.rate !== undefined) setRate(Math.round(data.chaos.rate * 100));
            if (data.chaos.targets) setTargets(data.chaos.targets);
          }
        }
      })
      .catch(() => {});

    // Socket listeners
    const handleStatus = (data) => {
      setIsRunning(data.running);
      if (data.scenario) setScenarioName(data.scenario);
      if (data.cores) {
        setCores(data.cores);
        setSelectedCores(prev => {
          const next = { ...prev };
          data.cores.forEach(c => {
            if (next[c.id] === undefined) next[c.id] = true;
          });
          return next;
        });
      }
      if (data.chaos) {
        setChaosEnabled(data.chaos.enabled);
        setChaosMode(data.chaos.mode || (data.chaos.enabled ? 'random' : 'off'));
        if (data.chaos.seed) setSeed(data.chaos.seed);
      }
    };

    const handleLog = ({ type, text }) => {
      setLogs(prev => [...prev.slice(-400), { type, text, id: Math.random() }]);
    };

    const handleChaosEvent = ({ timestamp, log }) => {
      setFaultEvents(prev => [...prev.slice(-100), { timestamp, log, id: Math.random() }]);
    };

    socket.on('scenario:status', handleStatus);
    socket.on('scenario:log', handleLog);
    socket.on('scenario:chaos_event', handleChaosEvent);

    return () => {
      socket.off('scenario:status', handleStatus);
      socket.off('scenario:log', handleLog);
      socket.off('scenario:chaos_event', handleChaosEvent);
    };
  }, [socket, manifest]);

  // Actions
  const handleStart = () => {
    const payload = {
      chaos: {
        enabled: chaosMode !== 'off',
        mode: chaosMode,
        seed: seed || Math.floor(Math.random() * Number.MAX_SAFE_INTEGER).toString(),
        rate: rate / 100.0,
        targets
      }
    };
    socket.emit('scenario:start', payload);
  };

  const handleStop = () => {
    socket.emit('scenario:stop', { cores: selectedCores });
  };

  const handleRestart = () => {
    const activeSeed = chaosMode === 'fixed' && seed 
      ? seed 
      : Math.floor(Math.random() * Number.MAX_SAFE_INTEGER).toString();
    if (chaosMode !== 'fixed') setSeed(activeSeed);

    const payload = {
      cores: selectedCores,
      chaos: {
        enabled: chaosMode !== 'off',
        mode: chaosMode,
        seed: activeSeed,
        rate: rate / 100.0,
        targets
      }
    };
    socket.emit('scenario:restart', payload);
  };

  const handleRerollSeed = () => {
    const newSeed = Math.floor(Math.random() * Number.MAX_SAFE_INTEGER).toString();
    setSeed(newSeed);
  };

  const handleCopyReproduction = () => {
    const activeSeed = seed || '12345';
    const scn = scenarioName || 'active_scenario';
    const cmd = `./run.sh --chaos --seed=${activeSeed} # or: bin/fbb test ${scn} --chaos --seed=${activeSeed}`;
    navigator.clipboard.writeText(cmd);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const toggleCore = (coreId) => {
    setSelectedCores(prev => ({
      ...prev,
      [coreId]: !prev[coreId]
    }));
  };

  const toggleTarget = (key) => {
    setTargets(prev => ({
      ...prev,
      [key]: !prev[key]
    }));
  };

  const clearConsole = () => {
    setLogs([]);
    setFaultEvents([]);
  };

  const anyRunning = isRunning || (cores && cores.some(c => c.running));

  return (
    <div style={{
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      backgroundColor: '#0f172a',
      color: '#f8fafc',
      fontFamily: 'Inter, system-ui, sans-serif',
      fontSize: '13px',
      overflow: 'hidden'
    }}>
      {/* Top Header Bar */}
      <div style={{
        padding: '10px 16px',
        backgroundColor: '#1e293b',
        borderBottom: '1px solid #334155',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        flexWrap: 'wrap',
        gap: '10px'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <div style={{
            width: '28px',
            height: '28px',
            borderRadius: '6px',
            background: 'linear-gradient(135deg, #a855f7 0%, #ec4899 100%)',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            boxShadow: '0 0 10px rgba(168, 85, 247, 0.4)'
          }}>
            <Zap size={16} color="#fff" />
          </div>
          <div>
            <div style={{ fontWeight: '600', fontSize: '14px', letterSpacing: '0.3px', display: 'flex', alignItems: 'center', gap: '8px' }}>
              Chaos & Fault Injection Engine
              <span style={{
                fontSize: '11px',
                padding: '2px 8px',
                borderRadius: '12px',
                backgroundColor: anyRunning ? 'rgba(34, 197, 94, 0.2)' : 'rgba(100, 116, 139, 0.2)',
                color: anyRunning ? '#4ade80' : '#94a3b8',
                border: anyRunning ? '1px solid #22c55e' : '1px solid #475569',
                display: 'flex',
                alignItems: 'center',
                gap: '4px'
              }}>
                <span style={{
                  width: '6px',
                  height: '6px',
                  borderRadius: '50%',
                  backgroundColor: anyRunning ? '#22c55e' : '#64748b'
                }} />
                {anyRunning ? 'RUNNING' : 'STOPPED'}
              </span>
            </div>
            <div style={{ fontSize: '11px', color: '#94a3b8' }}>
              Active Scenario: <strong style={{ color: '#e2e8f0' }}>{scenarioName || 'None'}</strong>
            </div>
          </div>
        </div>

        {/* Action Controls */}
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <button
            onClick={handleStart}
            disabled={anyRunning}
            style={{
              padding: '6px 12px',
              borderRadius: '6px',
              backgroundColor: anyRunning ? '#334155' : '#16a34a',
              color: anyRunning ? '#64748b' : '#fff',
              border: 'none',
              cursor: anyRunning ? 'not-allowed' : 'pointer',
              display: 'flex',
              alignItems: 'center',
              gap: '6px',
              fontWeight: '500',
              fontSize: '12px',
              transition: 'background 0.2s'
            }}
          >
            <Play size={14} /> Start
          </button>

          <button
            onClick={handleStop}
            disabled={!anyRunning}
            style={{
              padding: '6px 12px',
              borderRadius: '6px',
              backgroundColor: !anyRunning ? '#334155' : '#dc2626',
              color: !anyRunning ? '#64748b' : '#fff',
              border: 'none',
              cursor: !anyRunning ? 'not-allowed' : 'pointer',
              display: 'flex',
              alignItems: 'center',
              gap: '6px',
              fontWeight: '500',
              fontSize: '12px',
              transition: 'background 0.2s'
            }}
          >
            <Square size={14} /> Stop
          </button>

          <button
            onClick={handleRestart}
            style={{
              padding: '6px 14px',
              borderRadius: '6px',
              background: 'linear-gradient(135deg, #7c3aed 0%, #db2777 100%)',
              color: '#fff',
              border: 'none',
              cursor: 'pointer',
              display: 'flex',
              alignItems: 'center',
              gap: '6px',
              fontWeight: '600',
              fontSize: '12px',
              boxShadow: '0 2px 8px rgba(124, 58, 237, 0.4)',
              transition: 'transform 0.1s'
            }}
          >
            <RotateCcw size={14} /> Restart Scenario
          </button>
        </div>
      </div>

      {/* Main Body (Split into Left Config and Right Logs) */}
      <div style={{
        display: 'flex',
        flex: 1,
        overflow: 'hidden'
      }}>
        {/* Left Column: Settings Panel */}
        <div style={{
          width: '380px',
          minWidth: '340px',
          maxWidth: '420px',
          borderRight: '1px solid #334155',
          backgroundColor: '#111827',
          padding: '16px',
          overflowY: 'auto',
          display: 'flex',
          flexDirection: 'column',
          gap: '16px'
        }}>
          {/* Section 1: Multi-Core Process Lifecycle */}
          <div style={{
            backgroundColor: '#1f2937',
            padding: '12px',
            borderRadius: '8px',
            border: '1px solid #374151'
          }}>
            <div style={{ fontWeight: '600', color: '#cbd5e1', marginBottom: '8px', display: 'flex', alignItems: 'center', gap: '6px' }}>
              <Cpu size={15} color="#38bdf8" /> Target Cores for Restart:
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
              {cores.map(core => (
                <label key={core.id} style={{
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'space-between',
                  padding: '6px 8px',
                  borderRadius: '4px',
                  backgroundColor: selectedCores[core.id] ? 'rgba(56, 189, 248, 0.1)' : 'transparent',
                  border: selectedCores[core.id] ? '1px solid rgba(56, 189, 248, 0.3)' : '1px solid transparent',
                  cursor: 'pointer'
                }}>
                  <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                    <input
                      type="checkbox"
                      checked={!!selectedCores[core.id]}
                      onChange={() => toggleCore(core.id)}
                      style={{ cursor: 'pointer', accentColor: '#38bdf8' }}
                    />
                    <span style={{ fontWeight: '500' }}>{core.label}</span>
                  </div>
                  <span style={{
                    fontSize: '10px',
                    padding: '2px 6px',
                    borderRadius: '4px',
                    backgroundColor: core.running ? 'rgba(34, 197, 94, 0.2)' : 'rgba(148, 163, 184, 0.2)',
                    color: core.running ? '#4ade80' : '#94a3b8'
                  }}>
                    {core.running ? (core.pid ? `PID ${core.pid}` : 'RUNNING') : 'IDLE'}
                  </span>
                </label>
              ))}
            </div>
          </div>

          {/* Section 2: Chaos Injection Mode & Seed */}
          <div style={{
            backgroundColor: '#1f2937',
            padding: '12px',
            borderRadius: '8px',
            border: '1px solid #374151'
          }}>
            <div style={{ fontWeight: '600', color: '#cbd5e1', marginBottom: '8px', display: 'flex', alignItems: 'center', gap: '6px' }}>
              <Zap size={15} color="#c084fc" /> Fault Injection Mode:
            </div>
            
            {/* Mode Tabs */}
            <div style={{ display: 'flex', gap: '4px', marginBottom: '12px', backgroundColor: '#111827', padding: '3px', borderRadius: '6px' }}>
              {[
                { id: 'off', label: 'Off' },
                { id: 'random', label: 'Random Seed' },
                { id: 'fixed', label: 'Fixed Seed' }
              ].map(m => (
                <button
                  key={m.id}
                  onClick={() => setChaosMode(m.id)}
                  style={{
                    flex: 1,
                    padding: '6px 0',
                    border: 'none',
                    borderRadius: '4px',
                    backgroundColor: chaosMode === m.id ? (m.id === 'off' ? '#475569' : '#9333ea') : 'transparent',
                    color: chaosMode === m.id ? '#fff' : '#94a3b8',
                    cursor: 'pointer',
                    fontSize: '11px',
                    fontWeight: '600',
                    transition: 'all 0.2s'
                  }}
                >
                  {m.label}
                </button>
              ))}
            </div>

            {/* Seed Input & Reroll */}
            {chaosMode !== 'off' && (
              <div style={{ display: 'flex', flexDirection: 'column', gap: '6px' }}>
                <div style={{ fontSize: '11px', color: '#94a3b8' }}>
                  Deterministic PRNG Seed (xorshift128+):
                </div>
                <div style={{ display: 'flex', gap: '6px' }}>
                  <input
                    type="text"
                    value={seed}
                    onChange={(e) => { setSeed(e.target.value); setChaosMode('fixed'); }}
                    placeholder="Enter seed (e.g. 7080345091724213768)"
                    style={{
                      flex: 1,
                      backgroundColor: '#111827',
                      border: '1px solid #4b5563',
                      borderRadius: '6px',
                      padding: '6px 10px',
                      color: '#a855f7',
                      fontFamily: 'monospace',
                      fontSize: '11px',
                      fontWeight: '600'
                    }}
                  />
                  <button
                    onClick={handleRerollSeed}
                    title="Generate new random seed"
                    style={{
                      padding: '6px 10px',
                      backgroundColor: '#374151',
                      border: '1px solid #4b5563',
                      borderRadius: '6px',
                      color: '#e2e8f0',
                      cursor: 'pointer',
                      display: 'flex',
                      alignItems: 'center',
                      gap: '4px'
                    }}
                  >
                    <Dices size={14} />
                  </button>
                </div>

                {/* Reproduction copy button */}
                <button
                  onClick={handleCopyReproduction}
                  style={{
                    marginTop: '4px',
                    padding: '6px 10px',
                    backgroundColor: copied ? '#15803d' : '#2e1065',
                    border: copied ? '1px solid #22c55e' : '1px solid #7e22ce',
                    borderRadius: '6px',
                    color: copied ? '#86efac' : '#d8b4fe',
                    cursor: 'pointer',
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'center',
                    gap: '6px',
                    fontSize: '11px',
                    fontWeight: '500',
                    transition: 'all 0.2s'
                  }}
                >
                  {copied ? <Check size={14} /> : <Copy size={14} />}
                  {copied ? 'Command Copied to Clipboard!' : 'Copy Reproduction CLI Command'}
                </button>
              </div>
            )}
          </div>

          {/* Section 3: Fault Injection Targets & Rates */}
          {chaosMode !== 'off' && (
            <div style={{
              backgroundColor: '#1f2937',
              padding: '12px',
              borderRadius: '8px',
              border: '1px solid #374151'
            }}>
              <div style={{ fontWeight: '600', color: '#cbd5e1', marginBottom: '8px', display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
                <span style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
                  <AlertTriangle size={15} color="#f59e0b" /> Fault Rate:
                </span>
                <span style={{
                  fontSize: '12px',
                  fontWeight: '700',
                  color: '#f59e0b',
                  backgroundColor: 'rgba(245, 158, 11, 0.15)',
                  padding: '2px 8px',
                  borderRadius: '10px'
                }}>
                  {rate}%
                </span>
              </div>

              <input
                type="range"
                min="1"
                max="100"
                value={rate}
                onChange={(e) => setRate(parseInt(e.target.value, 10))}
                style={{ width: '100%', accentColor: '#f59e0b', cursor: 'pointer' }}
              />

              <div style={{ fontSize: '11px', color: '#94a3b8', marginTop: '10px', marginBottom: '6px' }}>
                Active Target Interfaces:
              </div>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '6px' }}>
                {[
                  { key: 'i2c', label: 'I2C (NACK)' },
                  { key: 'spi', label: 'SPI (Bit-Flip)' },
                  { key: 'uio', label: 'UIO (Timeout)' },
                  { key: 'can', label: 'CAN (Drop)' },
                  { key: 'cdma', label: 'CDMA (DecErr)' }
                ].map(t => (
                  <label key={t.key} style={{
                    display: 'flex',
                    alignItems: 'center',
                    gap: '6px',
                    fontSize: '11px',
                    color: targets[t.key] ? '#e2e8f0' : '#64748b',
                    cursor: 'pointer'
                  }}>
                    <input
                      type="checkbox"
                      checked={!!targets[t.key]}
                      onChange={() => toggleTarget(t.key)}
                      style={{ accentColor: '#a855f7', cursor: 'pointer' }}
                    />
                    {t.label}
                  </label>
                ))}
              </div>
            </div>
          )}
        </div>

        {/* Right Column: Real-time Execution & Fault Injection Logs */}
        <div style={{
          flex: 1,
          display: 'flex',
          flexDirection: 'column',
          backgroundColor: '#030712',
          overflow: 'hidden'
        }}>
          {/* Console Header */}
          <div style={{
            padding: '8px 16px',
            backgroundColor: '#111827',
            borderBottom: '1px solid #1f2937',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-between'
          }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
              <Terminal size={14} color="#94a3b8" />
              <span style={{ fontWeight: '600', fontSize: '12px', color: '#cbd5e1' }}>
                Scenario Execution & Injected Fault Feed
              </span>
              {faultEvents.length > 0 && (
                <span style={{
                  fontSize: '11px',
                  backgroundColor: 'rgba(239, 68, 68, 0.2)',
                  color: '#f87171',
                  border: '1px solid #ef4444',
                  padding: '1px 6px',
                  borderRadius: '10px',
                  fontWeight: '600'
                }}>
                  {faultEvents.length} Faults Injected
                </span>
              )}
            </div>

            <button
              onClick={clearConsole}
              style={{
                background: 'none',
                border: 'none',
                color: '#64748b',
                cursor: 'pointer',
                display: 'flex',
                alignItems: 'center',
                gap: '4px',
                fontSize: '11px'
              }}
              title="Clear Output"
            >
              <Trash2 size={13} /> Clear
            </button>
          </div>

          {/* Console Output Area */}
          <div style={{
            flex: 1,
            padding: '12px',
            overflowY: 'auto',
            fontFamily: 'ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace',
            fontSize: '12px',
            lineHeight: '1.6',
            color: '#e2e8f0'
          }}>
            {logs.length === 0 && faultEvents.length === 0 && (
              <div style={{ color: '#475569', fontStyle: 'italic', padding: '20px' }}>
                Ready to execute. Click [Restart Scenario] or [Start] to begin test execution with live chaos fault tracing.
              </div>
            )}

            {logs.map((item) => {
              const isChaos = item.text.includes('[F-BB CHAOS]');
              const isError = item.text.includes('FAIL') || item.text.includes('Error') || item.type === 'stderr';
              const isSuccess = item.text.includes('SUCCESS') || item.text.includes('PASSED');

              let color = '#94a3b8';
              if (isChaos) color = '#ec4899';
              else if (isError) color = '#f87171';
              else if (isSuccess) color = '#4ade80';

              return (
                <div key={item.id} style={{
                  color,
                  fontWeight: isChaos || isError || isSuccess ? '600' : 'normal',
                  whiteSpace: 'pre-wrap',
                  wordBreak: 'break-word'
                }}>
                  {item.text}
                </div>
              );
            })}
            <div ref={logEndRef} />
          </div>
        </div>
      </div>
    </div>
  );
};

export default ChaosPanel;
