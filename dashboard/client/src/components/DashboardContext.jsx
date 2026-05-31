/* eslint-disable react-refresh/only-export-components */
import { createContext, useContext, useState, useEffect } from 'react';
import { io } from 'socket.io-client';

const DashboardContext = createContext(null);

const socket = io('http://' + window.location.hostname + ':8080');

export const DashboardProvider = ({ children }) => {
  const [registers, setRegisters] = useState([]);
  const [connected, setConnected] = useState(false);
  const [manifest, setManifest] = useState(null);
  const [uartLogs, setUartLogs] = useState({});
  const [activeUart, setActiveUart] = useState(null);
  const [uartInput, setUartInput] = useState("");
  const [traceHistory, setTraceHistory] = useState([]);

  useEffect(() => {
    socket.on('connect', () => setConnected(true));
    socket.on('disconnect', () => setConnected(false));
    socket.on('registers', (data) => setRegisters(data));
    socket.on('uart-init', (data) => {
      setUartLogs(data);
      if (!activeUart && Object.keys(data).length > 0) setActiveUart(Object.keys(data)[0]);
    });
    socket.on('uart-data', ({ name, text }) => {
      setUartLogs(prev => ({ ...prev, [name]: (prev[name] || "") + text }));
      if (!activeUart) setActiveUart(name);
    });

    socket.on('trace-history-init', (data) => setTraceHistory(data));
    socket.on('trace-history-update', (snapshot) => {
      setTraceHistory(prev => [...prev, snapshot].slice(-500));
    });

    fetch('/api/manifest').then(res => res.json()).then(setManifest);

    return () => {
      socket.off('connect');
      socket.off('disconnect');
      socket.off('registers');
      socket.off('uart-data');
      socket.off('uart-init');
      socket.off('trace-history-init');
      socket.off('trace-history-update');
    };
  }, [activeUart]);

  const sendUart = (e) => {
    e.preventDefault();
    if (activeUart && uartInput) {
      socket.emit('uart-send', { name: activeUart, text: uartInput + '\n' });
      setUartInput("");
    }
  };

  const handleGpioToggle = (deviceName, bitIndex, currentOn, dataRegName = 'DATA') => {
    socket.emit('gpio-inject', { deviceName, bitIndex, value: !currentOn, dataRegName });
  };

  const handleClearTrace = () => {
    socket.emit('trace-history-clear');
  };

  const gpioDevices = manifest?.devices?.filter(d => d.type === 'gpio' || d.type === 'uio') || [];

  return (
    <DashboardContext.Provider
      value={{
        registers,
        connected,
        manifest,
        uartLogs,
        activeUart,
        setActiveUart,
        uartInput,
        setUartInput,
        traceHistory,
        gpioDevices,
        sendUart,
        handleGpioToggle,
        handleClearTrace,
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
