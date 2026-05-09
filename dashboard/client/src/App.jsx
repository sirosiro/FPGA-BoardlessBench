import React, { useState, useEffect, useRef } from 'react';
import { io } from 'socket.io-client';
import { Activity, Cpu, Hash, Clock, Box, Terminal as TerminalIcon, Send, AlertCircle, ToggleRight } from 'lucide-react';
import './App.css';

const socket = io('http://' + window.location.hostname + ':8080');

function App() {
  const [registers, setRegisters] = useState([]);
  const [connected, setConnected] = useState(false);
  const [manifest, setManifest] = useState(null);
  const [uartLogs, setUartLogs] = useState({});
  const [activeUart, setActiveUart] = useState(null);
  const [uartInput, setUartInput] = useState("");
  const [sidebarWidth, setSidebarWidth] = useState(350);
  const [isResizing, setIsResizing] = useState(false);
  const [registerHeight, setRegisterHeight] = useState(250);
  const [isHResizing, setIsHResizing] = useState(false);
  const terminalViewportRef = useRef(null);

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

    fetch('/api/manifest').then(res => res.json()).then(setManifest);

    return () => {
      socket.off('connect'); socket.off('disconnect');
      socket.off('registers'); socket.off('uart-data'); socket.off('uart-init');
    };
  }, [activeUart]);

  useEffect(() => {
    const viewport = terminalViewportRef.current;
    if (viewport) viewport.scrollTop = viewport.scrollHeight;
  }, [uartLogs, activeUart]);

  // Resizing logic (Vanilla JS)
  const startResizing = () => setIsResizing(true);
  const stopResizing = () => setIsResizing(false);
  const resize = (e) => {
    if (isResizing) {
      const sidebarElement = document.querySelector('.sidebar');
      if (sidebarElement) {
        const sidebarRect = sidebarElement.getBoundingClientRect();
        const newWidth = e.clientX - sidebarRect.left;
        if (newWidth > 200 && newWidth < 800) setSidebarWidth(newWidth);
      }
    }
  };

  const startHResizing = () => setIsHResizing(true);
  const stopHResizing = () => setIsHResizing(false);
  const hResize = (e) => {
    if (isHResizing) {
      const sidebarElement = document.querySelector('.sidebar');
      if (sidebarElement) {
        const sidebarRect = sidebarElement.getBoundingClientRect();
        const newHeight = e.clientY - sidebarRect.top;
        if (newHeight > 100 && newHeight < sidebarRect.height - 100) setRegisterHeight(newHeight);
      }
    }
  };

  useEffect(() => {
    window.addEventListener('mousemove', resize);
    window.addEventListener('mouseup', stopResizing);
    window.addEventListener('mousemove', hResize);
    window.addEventListener('mouseup', stopHResizing);
    return () => {
      window.removeEventListener('mousemove', resize);
      window.removeEventListener('mouseup', stopResizing);
      window.removeEventListener('mousemove', hResize);
      window.removeEventListener('mouseup', stopHResizing);
    };
  }, [isResizing, isHResizing]);

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

  const gpioDevices = manifest?.devices?.filter(d => d.type === 'gpio' || d.type === 'uio') || [];

  return (
    <div className="dashboard-container" style={{'--sidebar-width': `${sidebarWidth}px`}}>
      <header className="main-header">
        <div className="brand">
          <span className="logo-text">FPGA-BoardlessBench (F-BB)</span>
          <span className="version-tag">v3.0 Premium</span>
        </div>
        <div className="system-meta">
          <div className="meta-item"><Box size={14} /> {manifest?.board || 'Loading...'}</div>
          <div className={`conn-status ${connected ? 'online' : 'offline'}`}>
            {connected ? '● LIVE' : '○ DISCONNECTED'}
          </div>
        </div>
      </header>

      <main className="content-layout">
        <aside className="sidebar">
          <div className="register-pane" style={{ height: `${registerHeight}px`, flexShrink: 0, display: 'flex', flexDirection: 'column' }}>
            <div className="panel-header"><Cpu size={16} /> Register Monitor</div>
            <div className="register-viewport">
              <table className="register-table">
                <thead>
                  <tr><th>Name</th><th>Offset</th><th>Value (Hex)</th></tr>
                </thead>
                <tbody>
                  {registers.map((reg, i) => (
                    <tr key={i}>
                      <td className="reg-cell-name" style={{ fontWeight: 600 }}>{reg.name}</td>
                      <td className="reg-offset">{reg.offset}</td>
                      <td className="reg-val">{reg.value}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </div>
          
          <div className="h-resizer" onMouseDown={startHResizing}></div>
          
          <div className="gpio-pane" style={{ flex: 1, display: 'flex', flexDirection: 'column', minHeight: 0 }}>
            <div className="panel-header"><ToggleRight size={16} /> GPIO / Pin Array (118ch)</div>
            <div className="gpio-viewport">
            {gpioDevices.map((dev, i) => {
              const devDataRegs = registers.filter(r => r.deviceName === dev.name && r.name.startsWith('DATA'));
              
              if (devDataRegs.length === 0) return null;

              return devDataRegs.map((dataReg, chIndex) => {
                // Find matching TRI register (e.g., DATA -> TRI, DATA2 -> TRI2)
                const suffix = dataReg.name.replace('DATA', '');
                const triReg = registers.find(r => r.deviceName === dev.name && r.name === `TRI${suffix}`);
                
                const dataVal = dataReg?.decimal || 0;
                const triVal = triReg?.decimal || 0;
                const labelName = devDataRegs.length > 1 ? `${dev.name} (${dataReg.name})` : dev.name;
                
                return (
                  <div key={`gpio-${i}-ch${chIndex}`} className="gpio-dev-group">
                    <div className="gpio-dev-label">{labelName}</div>
                    <div className="gpio-grid">
                      {Array.from({ length: 32 }).map((_, bitIndex) => {
                        const isInput = (triVal & (1 << bitIndex)) !== 0;
                        const isOn = (dataVal & (1 << bitIndex)) !== 0;
                        return (
                          <div 
                            key={bitIndex} 
                            className={`gpio-bit ${isInput ? 'input' : 'output'} ${isOn ? 'on' : 'off'}`}
                            onClick={() => isInput && handleGpioToggle(dev.name, bitIndex, isOn, dataReg.name)}
                            title={`${labelName} Bit ${bitIndex} (${isInput ? 'Input' : 'Output'})`}
                          >
                            <div className="gpio-indicator"></div>
                            <span className="gpio-label">B{bitIndex}</span>
                          </div>
                        );
                      })}
                    </div>
                  </div>
                );
              });
            })}
            </div>
          </div>
        </aside>

        <div className="resizer" onMouseDown={startResizing}></div>

        <section className="terminal-section">
          <div className="panel-header">
            <div className="tab-group">
              <TerminalIcon size={16} />
              {Object.keys(uartLogs).map(name => (
                <button key={name} className={`tab ${activeUart === name ? 'active' : ''}`} onClick={() => setActiveUart(name)}>
                  {name.replace('vfpga_uart_', 'UART ')}
                </button>
              ))}
            </div>
          </div>
          
          <div className="terminal-viewport" ref={terminalViewportRef}>
            <pre className="terminal-output">
              {activeUart ? uartLogs[activeUart] : 'Waiting for connection...'}
            </pre>
          </div>

          <form className="terminal-prompt" onSubmit={sendUart}>
            <Send size={16} className="prompt-icon" />
            <input type="text" placeholder="Send command..." value={uartInput} onChange={e => setUartInput(e.target.value)} disabled={!activeUart} />
          </form>
        </section>
      </main>

      <style>{`
        .dashboard-container { height: 100vh; width: 100vw; box-sizing: border-box; display: flex; flex-direction: column; background: #0d1117; color: #c9d1d9; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Helvetica, Arial, sans-serif; }
        .main-header { flex: 0 0 60px; padding: 0 2rem; background: #161b22; border-bottom: 1px solid #30363d; display: flex; justify-content: space-between; align-items: center; }
        .logo-text { font-weight: 800; font-size: 1.2rem; color: #58a6ff; }
        .version-tag { margin-left: 0.5rem; font-size: 0.7rem; color: #8b949e; border: 1px solid #30363d; padding: 2px 6px; border-radius: 4px; }
        .conn-status.online { color: #3fb950; }
        
        .content-layout { flex: 1; display: flex; overflow: hidden; }
        .sidebar { width: var(--sidebar-width); flex-shrink: 0; background: #0d1117; display: flex; flex-direction: column; border-right: 1px solid #30363d; }
        .resizer { width: 4px; cursor: col-resize; background: transparent; transition: background 0.2s; z-index: 10; }
        .resizer:hover { background: #58a6ff; }
        .h-resizer { height: 4px; cursor: row-resize; background: #21262d; border-top: 1px solid #30363d; border-bottom: 1px solid #30363d; transition: background 0.2s; flex-shrink: 0; z-index: 10; }
        .h-resizer:hover { background: #58a6ff; border-color: #58a6ff; }
        
        .register-viewport, .gpio-viewport { flex: 1; overflow-y: auto; padding: 1rem; }
        .register-table { width: 100%; border-collapse: collapse; font-size: 0.75rem; }
        .register-table th { text-align: left; color: #8b949e; border-bottom: 1px solid #30363d; padding: 8px; }
        .register-table td { padding: 8px; border-bottom: 1px solid #0d1117; }
        .reg-val { font-family: 'JetBrains Mono', monospace; color: #58a6ff; }
        
        .gpio-dev-group { margin-bottom: 1.5rem; }
        .gpio-dev-label { font-size: 0.7rem; font-weight: 700; color: #8b949e; margin-bottom: 0.5rem; text-transform: uppercase; }
        .gpio-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(36px, 1fr)); gap: 8px; }
        .gpio-bit { display: flex; flex-direction: column; align-items: center; cursor: default; }
        .gpio-bit.input { cursor: pointer; }
        
        /* Output is a round LED */
        .gpio-bit.output .gpio-indicator { width: 14px; height: 14px; border-radius: 50%; border: 2px solid #30363d; margin-bottom: 4px; transition: all 0.2s; }
        .gpio-bit.on.output .gpio-indicator { background: #3fb950; border-color: #3fb950; box-shadow: 0 0 8px rgba(63, 185, 80, 0.5); }
        
        /* Input is a pill toggle switch */
        .gpio-bit.input .gpio-indicator { width: 24px; height: 14px; border-radius: 7px; background: #30363d; position: relative; margin-bottom: 4px; transition: background 0.2s; border: 1px solid #161b22; box-sizing: border-box; }
        .gpio-bit.input .gpio-indicator::after { content: ''; position: absolute; top: 1px; left: 1px; width: 10px; height: 10px; background: #8b949e; border-radius: 50%; transition: left 0.2s; }
        .gpio-bit.on.input .gpio-indicator { background: #58a6ff; border-color: #58a6ff; box-shadow: 0 0 8px rgba(88, 166, 255, 0.3); }
        .gpio-bit.on.input .gpio-indicator::after { left: 11px; background: #ffffff; }
        
        .gpio-label { font-size: 0.6rem; color: #8b949e; }
        
        .terminal-section { flex: 1; display: flex; flex-direction: column; min-width: 0; background: #010409; }
        .terminal-viewport { flex: 1; overflow-y: auto; padding: 1.5rem; }
        .terminal-output { margin: 0; font-family: 'JetBrains Mono', monospace; font-size: 0.9rem; line-height: 1.5; white-space: pre-wrap; }
        .terminal-prompt { flex: 0 0 60px; padding: 0 1rem; background: #161b22; display: flex; align-items: center; gap: 1rem; }
        .terminal-prompt input { flex: 1; background: #0d1117; border: 1px solid #30363d; border-radius: 6px; padding: 8px 12px; color: white; outline: none; }
      `}</style>
    </div>
  );
}

export default App;
