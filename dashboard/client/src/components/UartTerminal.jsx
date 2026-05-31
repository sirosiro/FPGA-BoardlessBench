import { useRef, useEffect } from 'react';
import { Terminal as TerminalIcon, Send } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function UartTerminal() {
  const {
    uartLogs,
    activeUart,
    setActiveUart,
    uartInput,
    setUartInput,
    sendUart,
  } = useDashboard();

  const terminalViewportRef = useRef(null);

  useEffect(() => {
    const viewport = terminalViewportRef.current;
    if (viewport) viewport.scrollTop = viewport.scrollHeight;
  }, [uartLogs, activeUart]);

  return (
    <section className="terminal-section" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden', background: '#010409' }}>
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
      
      <div className="terminal-viewport" ref={terminalViewportRef} style={{ flex: 1, overflowY: 'auto', padding: '1.5rem' }}>
        <pre className="terminal-output" style={{ margin: 0, fontFamily: "'JetBrains Mono', monospace", fontSize: '0.9rem', lineHeight: 1.5, whiteSpace: 'pre-wrap' }}>
          {activeUart ? uartLogs[activeUart] : 'Waiting for connection...'}
        </pre>
      </div>

      <form className="terminal-prompt" onSubmit={sendUart} style={{ flex: '0 0 60px', padding: '0 1rem', background: '#161b22', display: 'flex', alignItems: 'center', gap: '1rem' }}>
        <Send size={16} className="prompt-icon" />
        <input 
          type="text" 
          placeholder="Send command..." 
          value={uartInput} 
          onChange={e => setUartInput(e.target.value)} 
          disabled={!activeUart} 
          style={{ flex: 1, background: '#0d1117', border: '1px solid #30363d', borderRadius: '6px', padding: '8px 12px', color: 'white', outline: 'none' }}
        />
      </form>
    </section>
  );
}

export default UartTerminal;
