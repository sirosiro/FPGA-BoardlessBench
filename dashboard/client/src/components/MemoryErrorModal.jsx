import { useDashboard } from './DashboardContext';
import { AlertTriangle, RefreshCw } from 'lucide-react';

export default function MemoryErrorModal() {
  const { memoryError, setMemoryError } = useDashboard();

  if (!memoryError) return null;

  return (
    <div style={{
      position: 'fixed',
      top: 0,
      left: 0,
      width: '100vw',
      height: '100vh',
      backgroundColor: 'rgba(10, 10, 10, 0.85)',
      backdropFilter: 'blur(8px)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      zIndex: 9999,
      fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif'
    }}>
      <div style={{
        width: '500px',
        background: 'rgba(30, 20, 20, 0.9)',
        border: '2px solid #f85149',
        borderRadius: '12px',
        padding: '30px',
        boxShadow: '0 8px 32px rgba(248, 81, 73, 0.25)',
        color: '#f85149',
        textAlign: 'center'
      }}>
        <div style={{ display: 'flex', justifyContent: 'center', marginBottom: '20px' }}>
          <AlertTriangle size={64} color="#f85149" />
        </div>
        <h2 style={{
          fontSize: '1.35rem',
          fontWeight: 'bold',
          marginBottom: '15px',
          letterSpacing: '0.05em'
        }}>
          [FATAL ERROR] Out-Of-Bounds Memory Access Detected
        </h2>
        <p style={{ color: '#c9d1d9', fontSize: '0.9rem', marginBottom: '25px', lineHeight: '1.5' }}>
          The firmware attempted to access an address outside the virtual FPGA register space (MMIO). The F-BB protection mechanism (Guard Page) has successfully trapped the violation.
        </p>

        <div style={{
          background: 'rgba(0, 0, 0, 0.4)',
          border: '1px solid rgba(248, 81, 73, 0.3)',
          borderRadius: '8px',
          padding: '15px',
          marginBottom: '25px',
          textAlign: 'left',
          fontSize: '0.9rem',
          fontFamily: '"JetBrains Mono", monospace',
          color: '#c9d1d9'
        }}>
          <div style={{ marginBottom: '8px' }}>
            <span style={{ color: '#8b949e' }}>Fault Address:</span> <strong style={{ color: '#ff7b72' }}>{memoryError.address}</strong>
          </div>
          <div style={{ marginBottom: '8px' }}>
            <span style={{ color: '#8b949e' }}>Target Device:</span> <span style={{ color: '#58a6ff' }}>{memoryError.device}</span>
          </div>
          <div>
            <span style={{ color: '#8b949e' }}>Device Size:</span> <span style={{ color: '#58a6ff' }}>{memoryError.size} Bytes</span>
          </div>
        </div>

        <button 
          onClick={() => setMemoryError(null)}
          style={{
            background: '#f85149',
            color: '#ffffff',
            border: 'none',
            borderRadius: '6px',
            padding: '10px 20px',
            fontSize: '0.9rem',
            fontWeight: 'bold',
            cursor: 'pointer',
            display: 'inline-flex',
            alignItems: 'center',
            gap: '8px',
            transition: 'background 0.2s',
            boxShadow: '0 4px 12px rgba(248, 81, 73, 0.2)'
          }}
          onMouseOver={(e) => e.target.style.background = '#da3633'}
          onMouseOut={(e) => e.target.style.background = '#f85149'}
        >
          <RefreshCw size={14} />
          Clear Warning & Resume
        </button>
      </div>
    </div>
  );
}
