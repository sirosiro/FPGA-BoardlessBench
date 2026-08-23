import { useState, useEffect } from 'react';
import { ShieldAlert, Trash2, Filter, Download, CheckCircle, AlertTriangle, Activity } from 'lucide-react';
import { useDashboard } from './DashboardContext';

const formatTimestamp = (raw) => {
  if (!raw) return new Date().toLocaleTimeString();
  if (/^\d+$/.test(raw)) {
    const d = new Date(parseInt(raw, 10));
    const ms = String(d.getMilliseconds()).padStart(3, '0');
    return d.toLocaleTimeString() + '.' + ms;
  }
  if (raw.includes('T')) {
    const d = new Date(raw);
    if (!isNaN(d.getTime())) {
      const ms = String(d.getMilliseconds()).padStart(3, '0');
      return d.toLocaleTimeString() + '.' + ms;
    }
  }
  return raw;
};

const parseLogEntry = (logStr, defaultTimestamp, index = 0) => {
  let timestamp = defaultTimestamp;
  let cleanLog = logStr;

  // Extract C-Shim timestamp "[ts=1787405898492]" or "[13:35:11.822]" if present
  const tsMatch = typeof logStr === 'string' ? logStr.match(/^\[(?:ts=)?(\d[\d:\.A-Za-z_-]*)\]\s*(.*)/) : null;
  if (tsMatch) {
    timestamp = tsMatch[1];
    cleanLog = tsMatch[2];
  }

  const isError = cleanLog && cleanLog.includes('PROTOCOL_VIOLATION');
  const isWrite = cleanLog && cleanLog.includes('WRITE');
  const type = isError ? 'PROTOCOL VIOLATION' : (isWrite ? 'WRITE' : 'READ');

  return {
    id: Date.now() + index + Math.random(),
    timestamp: formatTimestamp(timestamp),
    type,
    log: cleanLog || '',
    status: isError ? 'ERROR' : 'OK'
  };
};

const TransactionLoggerPane = () => {
  const { socket } = useDashboard();
  const [logs, setLogs] = useState([]);
  const [errorsOnly, setErrorsOnly] = useState(false);

  useEffect(() => {
    // Fetch historic transactions on load
    fetch('/api/violations')
      .then((res) => res.json())
      .then((data) => {
        if (Array.isArray(data) && data.length > 0) {
          const historic = data.map((d, i) => parseLogEntry(d.log, d.timestamp, i));
          setLogs(historic.reverse());
        }
      })
      .catch((e) => console.warn('[TransactionLogger] Failed to fetch transactions:', e));

    if (!socket) return;

    const handleViolation = (data) => {
      const logLine = typeof data === 'string' ? data : data?.log;
      const ts = typeof data === 'object' ? data?.timestamp : null;
      const parsedLog = parseLogEntry(logLine, ts);
      setLogs((prev) => [parsedLog, ...prev].slice(0, 500));
    };

    socket.on('protocol_violation', handleViolation);
    return () => {
      socket.off('protocol_violation', handleViolation);
    };
  }, [socket]);

  const handleClear = () => {
    setLogs([]);
  };

  const handleDownloadCsv = () => {
    if (logs.length === 0) return;
    const header = "Timestamp,Type,Log,Status\n";
    const body = logs.map(l => `"${l.timestamp}","${l.type}","${(l.log || '').replace(/"/g, '""')}","${l.status}"`).join("\n");
    const blob = new Blob([header + body], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `fbb_transaction_log_${Date.now()}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const filteredLogs = errorsOnly ? logs.filter(l => l.status === 'ERROR') : logs;
  const violationCount = logs.filter(l => l.status === 'ERROR').length;
  const totalCount = logs.length;

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', backgroundColor: '#0d1117', color: '#c9d1d9', fontFamily: 'monospace', fontSize: '12px', userSelect: 'none' }}>
      {/* Top Controls Bar */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '8px 12px', backgroundColor: '#161b22', borderBottom: '1px solid #30363d', gap: '12px' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '10px' }}>
          <ShieldAlert style={{ width: '16px', height: '16px', color: violationCount > 0 ? '#f85149' : '#3fb950', flexShrink: 0 }} />
          <span style={{ fontWeight: 600, color: '#f0f6fc', fontSize: '13px', marginRight: '6px' }}>
            Transaction & Violation Inspector
          </span>
          <span style={{
            padding: '2px 8px',
            borderRadius: '12px',
            fontSize: '11px',
            fontWeight: 'bold',
            backgroundColor: violationCount > 0 ? 'rgba(218, 54, 51, 0.25)' : 'rgba(46, 160, 67, 0.25)',
            color: violationCount > 0 ? '#ff7b72' : '#56d364',
            border: violationCount > 0 ? '1px solid #da3633' : '1px solid #238636',
            display: 'inline-block'
          }}>
            {violationCount} Violation{violationCount !== 1 ? 's' : ''} ({totalCount} Tx)
          </span>
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <button
            onClick={() => setErrorsOnly(!errorsOnly)}
            style={{
              display: 'flex', alignItems: 'center', gap: '4px', padding: '4px 8px', borderRadius: '6px', fontSize: '11px', cursor: 'pointer',
              backgroundColor: errorsOnly ? '#490202' : '#21262d', color: errorsOnly ? '#ff7b72' : '#c9d1d9', border: errorsOnly ? '1px solid #da3633' : '1px solid #30363d'
            }}
            title="Toggle Violations Only"
          >
            <Filter style={{ width: '12px', height: '12px' }} />
            <span>Errors Only</span>
          </button>

          <button
            onClick={handleDownloadCsv}
            disabled={logs.length === 0}
            style={{
              display: 'flex', alignItems: 'center', gap: '4px', padding: '4px 8px', borderRadius: '6px', fontSize: '11px', cursor: 'pointer',
              backgroundColor: '#21262d', color: '#c9d1d9', border: '1px solid #30363d', opacity: logs.length === 0 ? 0.5 : 1
            }}
            title="Export CSV"
          >
            <Download style={{ width: '12px', height: '12px' }} />
            <span>Export CSV</span>
          </button>

          <button
            onClick={handleClear}
            style={{
              display: 'flex', alignItems: 'center', gap: '4px', padding: '4px 8px', borderRadius: '6px', fontSize: '11px', cursor: 'pointer',
              backgroundColor: '#21262d', color: '#c9d1d9', border: '1px solid #30363d'
            }}
            title="Clear Logs"
          >
            <Trash2 style={{ width: '12px', height: '12px', color: '#8b949e' }} />
            <span>Clear</span>
          </button>
        </div>
      </div>

      {/* Log Feed Table */}
      <div style={{ flex: 1, overflowY: 'auto', padding: '8px', display: 'flex', flexDirection: 'column', gap: '6px' }}>
        {filteredLogs.length === 0 ? (
          <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', height: '100%', color: '#8b949e', gap: '8px', padding: '32px 0' }}>
            <CheckCircle style={{ width: '32px', height: '32px', color: '#484f58' }} />
            <span>No transactions recorded.</span>
          </div>
        ) : (
          filteredLogs.map((entry) => (
            <div
              key={entry.id}
              style={{
                display: 'flex', alignItems: 'flex-start', gap: '10px', padding: '8px 10px', borderRadius: '6px',
                backgroundColor: entry.status === 'ERROR' ? 'rgba(218, 54, 51, 0.18)' : '#161b22',
                border: entry.status === 'ERROR' ? '1px solid rgba(218, 54, 51, 0.7)' : '1px solid #30363d'
              }}
            >
              {entry.status === 'ERROR' ? (
                <AlertTriangle style={{ width: '16px', height: '16px', color: '#f85149', marginTop: '2px', flexShrink: 0 }} />
              ) : (
                <Activity style={{ width: '16px', height: '16px', color: '#58a6ff', marginTop: '2px', flexShrink: 0 }} />
              )}
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '3px' }}>
                  <span style={{ color: '#8b949e', fontSize: '11px' }}>{entry.timestamp}</span>
                  <span style={{
                    padding: '2px 6px', fontSize: '9px', fontWeight: 'bold', textTransform: 'uppercase', borderRadius: '4px',
                    backgroundColor: entry.status === 'ERROR' ? '#8c1d18' : '#1f6feb',
                    color: entry.status === 'ERROR' ? '#ffc5c2' : '#ffffff', display: 'inline-block'
                  }}>
                    {entry.type}
                  </span>
                </div>
                <div style={{ fontFamily: 'monospace', fontSize: '12px', wordBreak: 'break-all', color: entry.status === 'ERROR' ? '#ff7b72' : '#c9d1d9' }}>
                  {entry.log}
                </div>
              </div>
            </div>
          ))
        )}
      </div>
    </div>
  );
};

export default TransactionLoggerPane;
