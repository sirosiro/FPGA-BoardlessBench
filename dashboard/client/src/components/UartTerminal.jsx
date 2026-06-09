import { useState, useRef, useEffect } from 'react';
import { Terminal as TerminalIcon, Send } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function UartTerminal(props) {
  const {
    uartLogs,
    sendUart,
  } = useDashboard();

  const deviceName = props.params?.deviceName || 'default';
  const [localInput, setLocalInput] = useState("");
  const terminalViewportRef = useRef(null);

  useEffect(() => {
    const viewport = terminalViewportRef.current;
    if (viewport) viewport.scrollTop = viewport.scrollHeight;
  }, [uartLogs, deviceName]);

  const handleSubmit = (e) => {
    e.preventDefault();
    if (deviceName !== 'default' && localInput) {
      sendUart(deviceName, localInput + '\n');
      setLocalInput("");
    }
  };

  const currentLog = uartLogs[deviceName];

  return (
    <section className="terminal-section" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden', background: '#010409' }}>
      <div className="panel-header">
        <div className="tab-group" style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', color: '#c9d1d9' }}>
          <TerminalIcon size={16} />
          <span style={{ fontSize: '0.85rem', fontWeight: 600 }}>
            {deviceName === 'default' ? 'UART Console' : deviceName.replace('vfpga_uart_', 'UART ')}
          </span>
        </div>
      </div>
      
      <div className="terminal-viewport" ref={terminalViewportRef} style={{ flex: 1, overflowY: 'auto', padding: '1.5rem' }}>
        <pre className="terminal-output" style={{ margin: 0, fontFamily: "'JetBrains Mono', monospace", fontSize: '0.9rem', lineHeight: 1.5, whiteSpace: 'pre-wrap' }}>
          {currentLog || 'Waiting for connection...'}
        </pre>
      </div>

      <form className="terminal-prompt" onSubmit={handleSubmit} style={{ flex: '0 0 60px', padding: '0 1rem', background: '#161b22', display: 'flex', alignItems: 'center', gap: '1rem' }}>
        <Send size={16} className="prompt-icon" />
        <input 
          type="text" 
          placeholder="Send command..." 
          value={localInput} 
          onChange={e => setLocalInput(e.target.value)} 
          disabled={deviceName === 'default'} 
          style={{ flex: 1, background: '#0d1117', border: '1px solid #30363d', borderRadius: '6px', padding: '8px 12px', color: 'white', outline: 'none' }}
        />
      </form>
    </section>
  );
}

export default UartTerminal;
