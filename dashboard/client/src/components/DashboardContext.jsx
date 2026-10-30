import { createContext, useContext, useState, useEffect, useMemo } from 'react';
import { io } from 'socket.io-client';

const DashboardContext = createContext(null);

export const socket = io('http://' + window.location.hostname + ':8080', {
  transports: ['websocket']
});

export const DashboardProvider = ({ children }) => {
  const [registers, setRegisters] = useState([]);
  const [connected, setConnected] = useState(false);
  const [manifest, setManifest] = useState(null);
  const [scenarioTelemetry, setScenarioTelemetry] = useState(null);
  const [uartLogs, setUartLogs] = useState({});
  const [uartSettings, setUartSettings] = useState({});
  const [traceHistory, setTraceHistory] = useState([]);
  const [hdmiFrame, setHdmiFrame] = useState(null);
  const [displayFrame, setDisplayFrame] = useState(null);
  const [display7SegFrame, setDisplay7SegFrame] = useState(null);
  const [peripheralFrames, setPeripheralFrames] = useState({});
  const [hiddenTraceKeys, setHiddenTraceKeys] = useState({});
  const [memoryError, setMemoryError] = useState(null);
  const [timeTravelState, setTimeTravelState] = useState({
    isLive: true,
    selectedIndex: null,
    snapshot: null
  });


  useEffect(() => {
    socket.on('connect', () => setConnected(true));
    socket.on('disconnect', () => setConnected(false));
    socket.on('scenario:telemetry', (data) => setScenarioTelemetry(data));
    socket.on('registers', (data) => {
      setRegisters(data);
    });
    socket.on('uart-init', (data) => {
      setUartLogs(data);
    });
    socket.on('uart-data', ({ name, text }) => {
      setUartLogs(prev => ({ ...prev, [name]: (prev[name] || "") + text }));
    });
    socket.on('uart-settings', (data) => {
      setUartSettings(data);
    });
    socket.on('memory-error', (data) => {
      setMemoryError(data);
    });

    socket.on('trace-history-init', (data) => setTraceHistory(data));
    socket.on('trace-history-update', (snapshot) => {
      setTraceHistory(prev => [...prev, snapshot].slice(-500));
    });
    socket.on('hdmi-frame', (data) => setHdmiFrame(data));
    socket.on('display-frame', (data) => setDisplayFrame(data));
    socket.on('display-7seg-frame', (data) => setDisplay7SegFrame(data));
    socket.on('peripheral:frame', ({ name, data }) => {
      setPeripheralFrames(prev => ({ ...prev, [name]: data }));
    });


    fetch('/api/manifest').then(res => res.json()).then(setManifest);

    return () => {
      socket.off('connect');
      socket.off('disconnect');
      socket.off('scenario:telemetry');
      socket.off('registers');
      socket.off('uart-data');
      socket.off('uart-init');
      socket.off('uart-settings');
      socket.off('memory-error');
      socket.off('trace-history-init');
      socket.off('trace-history-update');
      socket.off('hdmi-frame');
      socket.off('display-frame');
      socket.off('display-7seg-frame');
      socket.off('peripheral:frame');
    };
  }, []);


  const sendUart = (name, text) => {
    if (name && text) {
      socket.emit('uart-send', { name, text });
    }
  };

  const handleGpioToggle = (deviceName, bitIndex, currentOn, dataRegName = 'DATA') => {
    console.log(`[UI-DEBUG] Pin clicked: device=${deviceName}, bit=${bitIndex}, currentOn=${currentOn}, sending value=${!currentOn}`);
    socket.emit('gpio-inject', { deviceName, bitIndex, value: !currentOn, dataRegName });
  };
  
  // Cross-window BroadcastChannel synchronization
  useEffect(() => {
    let channel;
    try {
      if (typeof window !== 'undefined' && 'BroadcastChannel' in window) {
        channel = new BroadcastChannel('fbb_dashboard_sync');
        channel.onmessage = (event) => {
          const { type, payload } = event.data || {};
          if (type === 'SYNC_HIDDEN_TRACE_KEYS' && payload) {
            setHiddenTraceKeys(payload);
          } else if (type === 'SYNC_TIME_TRAVEL' && payload) {
            setTimeTravelState(payload);
          }
        };
      }
    } catch (e) {
      console.warn('[DashboardContext] BroadcastChannel init error:', e);
    }
    return () => {
      if (channel) {
        channel.close();
      }
    };
  }, []);

  const scrubTo = (snapshot) => {
    if (!snapshot) return;
    const newState = {
      isLive: false,
      selectedIndex: snapshot.index,
      snapshot: snapshot
    };
    setTimeTravelState(newState);
    try {
      if (typeof window !== 'undefined' && 'BroadcastChannel' in window) {
        const ch = new BroadcastChannel('fbb_dashboard_sync');
        ch.postMessage({ type: 'SYNC_TIME_TRAVEL', payload: newState });
        ch.close();
      }
    } catch {}
  };

  const returnToLive = () => {
    const newState = {
      isLive: true,
      selectedIndex: null,
      snapshot: null
    };
    setTimeTravelState(newState);
    try {
      if (typeof window !== 'undefined' && 'BroadcastChannel' in window) {
        const ch = new BroadcastChannel('fbb_dashboard_sync');
        ch.postMessage({ type: 'SYNC_TIME_TRAVEL', payload: newState });
        ch.close();
      }
    } catch {}
  };

  const isTimeTravelActive = !timeTravelState.isLive && !!timeTravelState.snapshot;

  const effectiveRegisters = useMemo(() => {
    if (!isTimeTravelActive) return registers;
    const snap = timeTravelState.snapshot;
    return registers.map(reg => {
      const regKey = `${reg.deviceName}_${reg.name}`;
      if (snap[regKey] !== undefined) {
        const val = snap[regKey];
        return {
          ...reg,
          decimal: val,
          value: `0x${(Number(val) >>> 0).toString(16).padStart(8, '0')}`,
          isHistorical: true
        };
      }
      return { ...reg, isHistorical: true };
    });
  }, [registers, isTimeTravelActive, timeTravelState.snapshot]);

  const effectiveTelemetry = useMemo(() => {
    if (isTimeTravelActive && timeTravelState.snapshot?.telemetry) {
      return timeTravelState.snapshot.telemetry;
    }
    return scenarioTelemetry;
  }, [isTimeTravelActive, timeTravelState.snapshot, scenarioTelemetry]);

  // Dispatch custom event for external panes (e.g., 3D Orbit Viewer)
  useEffect(() => {
    if (typeof window !== 'undefined' && effectiveTelemetry) {
      window.dispatchEvent(new CustomEvent('fbb:telemetry:effective', { detail: effectiveTelemetry }));
    }
  }, [effectiveTelemetry]);

  const handleSpiAdcInject = (channel, value) => {
    socket.emit('spi-adc-inject', { channel, value });
  };

  const handleClearTrace = () => {
    socket.emit('trace-history-clear');
  };

  const toggleTraceKey = (key) => {
    setHiddenTraceKeys(prev => {
      const updated = {
        ...prev,
        [key]: !prev[key]
      };
      try {
        if (typeof window !== 'undefined' && 'BroadcastChannel' in window) {
          const ch = new BroadcastChannel('fbb_dashboard_sync');
          ch.postMessage({ type: 'SYNC_HIDDEN_TRACE_KEYS', payload: updated });
          ch.close();
        }
      } catch {
        // ignore
      }
      return updated;
    });
  };

  const isGpioDev = (d) => {
    if (!d) return false;
    if (d.type === 'gpio') return true;
    const name = (d.name || '').toLowerCase();
    const compat = (d.compatible || '').toLowerCase();
    if (name.includes('memory') || name.includes('bram') || name.includes('dma') || compat.includes('bram') || compat.includes('dma')) {
      return false;
    }
    return (
      compat.includes('gpio') || 
      compat.includes('matrix') || 
      compat.includes('hub75') || 
      name.includes('gpio') || 
      name.includes('pin') || 
      name.includes('matrix') || 
      name.includes('hub75')
    );
  };

  const gpioDevices = manifest?.devices?.filter(isGpioDev) || [];

  return (
    <DashboardContext.Provider
      value={{
        socket,
        registers,
        effectiveRegisters,
        connected,
        manifest,
        scenarioTelemetry,
        effectiveTelemetry,
        uartLogs,
        uartSettings,
        traceHistory,
        timeTravelState,
        isTimeTravelActive,
        scrubTo,
        returnToLive,
        gpioDevices,
        sendUart,
        handleGpioToggle,
        handleSpiAdcInject,
        handleClearTrace,
        hdmiFrame,
        displayFrame,
        display7SegFrame,
        peripheralFrames,
        hiddenTraceKeys,
        toggleTraceKey,
        memoryError,
        setMemoryError,
      }}

    >
      {children}
    </DashboardContext.Provider>
  );
};

export const useDashboard = () => {
  const context = useContext(DashboardContext);
  if (!context) {
    throw new Error('useDashboard must be used within a DashboardProvider');
  }
  return context;
};

export const useScenarioTelemetry = () => {
  const { effectiveTelemetry } = useDashboard();
  return effectiveTelemetry;
};
