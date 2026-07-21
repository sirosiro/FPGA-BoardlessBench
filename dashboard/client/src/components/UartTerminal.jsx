import { useState, useRef, useEffect } from 'react';
import { Terminal as TerminalIcon, Send } from 'lucide-react';
import { useDashboard } from './DashboardContext';

// Simple ANSI escape sequence parser for terminal colors and formatting
function parseAnsi(text) {
  if (!text) return text;

  // Clean non-color terminal control sequences (e.g. clear line, cursor movements)
  const cleanedText = text.replace(/\u001b\[[0-9;]*[a-hK-Z]/g, '');

  const ansiRegex = /\u001b\[([0-9;]*)m/;
  const parts = [];
  let remaining = cleanedText;

  let currentStyle = {
    color: null,
    fontWeight: null,
  };

  const ansiColors = {
    '30': '#0f1419', // Black
    '31': '#ff5555', // Red (Bright red for better visibility)
    '32': '#50fa7b', // Green
    '33': '#f1fa8c', // Yellow
    '34': '#bd93f9', // Blue
    '35': '#ff79c6', // Magenta
    '36': '#8be9fd', // Cyan
    '37': '#f8f8f2', // White
    '90': '#6272a4', // Grey
    '91': '#ff6e6e',
    '92': '#69ff94',
    '93': '#ffffa5',
    '94': '#d6acff',
    '95': '#ff92df',
    '96': '#a4ffff',
    '97': '#ffffff',
  };

  let match;
  let keyCounter = 0;
  while ((match = ansiRegex.exec(remaining)) !== null) {
    const matchIndex = match.index;
    const matchLength = match[0].length;
    const code = match[1];

    if (matchIndex > 0) {
      const textChunk = remaining.substring(0, matchIndex);
      parts.push(
        <span
          key={keyCounter++}
          style={{
            color: currentStyle.color || undefined,
            fontWeight: currentStyle.fontWeight || undefined,
          }}
        >
          {textChunk}
        </span>
      );
    }

    const codes = code.split(';');
    codes.forEach(c => {
      if (c === '0' || c === '') {
        currentStyle = { color: null, fontWeight: null };
      } else if (c === '1') {
        currentStyle.fontWeight = 'bold';
      } else if (ansiColors[c]) {
        currentStyle.color = ansiColors[c];
      }
    });

    remaining = remaining.substring(matchIndex + matchLength);
  }

  if (remaining.length > 0) {
    parts.push(
      <span
        key={keyCounter++}
        style={{
          color: currentStyle.color || undefined,
          fontWeight: currentStyle.fontWeight || undefined,
        }}
      >
        {remaining}
      </span>
    );
  }

  return parts;
}

function UartTerminal(props) {
  const {
    uartLogs,
    uartSettings,
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
  const settingsStr = uartSettings[deviceName];
  const isDefaultBaud = !settingsStr || settingsStr.includes('default');
  const displaySettings = settingsStr ? settingsStr.replace(' default', ' (default)') : '';

  return (
    <section className="terminal-section" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden', background: '#010409' }}>
      <div className="panel-header" style={{ justifyContent: 'space-between' }}>
        <div className="tab-group" style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', color: '#c9d1d9' }}>
          <TerminalIcon size={16} />
          <span style={{ fontSize: '0.85rem', fontWeight: 600 }}>
            {deviceName === 'default' ? 'UART Console' : deviceName.replace('vfpga_uart_', 'UART ')}
          </span>
        </div>
        {deviceName !== 'default' && settingsStr && (
          <div style={{
            fontSize: '0.75rem',
            fontFamily: "'JetBrains Mono', monospace",
            fontWeight: 'bold',
            padding: '2px 8px',
            borderRadius: '4px',
            background: isDefaultBaud ? 'rgba(248, 81, 73, 0.15)' : 'rgba(56, 139, 253, 0.15)',
            border: isDefaultBaud ? '1px solid #f85149' : '1px solid #388bfd',
            color: isDefaultBaud ? '#f85149' : '#58a6ff'
          }}>
            {isDefaultBaud ? 'Baud: Unconfigured' : `Baud: ${displaySettings}`}
          </div>
        )}
      </div>

      <div className="terminal-viewport" ref={terminalViewportRef} style={{ flex: 1, overflowY: 'auto', padding: '1.5rem' }}>
        <pre className="terminal-output" style={{ margin: 0, fontFamily: "'JetBrains Mono', monospace", fontSize: '0.9rem', lineHeight: 1.5, whiteSpace: 'pre-wrap' }}>
          {currentLog ? parseAnsi(currentLog) : 'Waiting for connection...'}
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
