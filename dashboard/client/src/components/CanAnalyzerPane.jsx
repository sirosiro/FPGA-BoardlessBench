import React, { useState, useEffect, useRef } from 'react';
import { useDashboard } from './DashboardContext';
import { Play, Pause, Trash2, Send, Filter, Radio, RefreshCw, Car } from 'lucide-react';

export default function CanAnalyzerPane() {
  const { socket } = useDashboard();
  const [frames, setFrames] = useState([]);
  const [filterId, setFilterId] = useState('');
  const [filterChannel, setFilterChannel] = useState('all');
  const [isPaused, setIsPaused] = useState(false);
  const [autoScroll, setAutoScroll] = useState(true);

  // Injection state
  const [txBus, setTxBus] = useState('0');
  const [txId, setTxId] = useState('7DF');
  const [txDlc, setTxDlc] = useState(8);
  const [txData, setTxData] = useState('02 01 0D 00 00 00 00 00');
  const [txStatus, setTxStatus] = useState('');

  const tableEndRef = useRef(null);

  useEffect(() => {
    if (!socket) return;

    const handleCanFrames = (msg) => {
      if (isPaused) return;
      const incoming = (msg.frames || []).map((f, idx) => ({
        ...f,
        uid: `${Date.now()}_${Math.random()}_${idx}`,
        bus: msg.bus || '0'
      }));

      setFrames((prev) => {
        const next = [...prev, ...incoming];
        return next.length > 500 ? next.slice(next.length - 500) : next;
      });
    };

    socket.on('can:frames', handleCanFrames);
    return () => {
      socket.off('can:frames', handleCanFrames);
    };
  }, [socket, isPaused]);

  useEffect(() => {
    if (autoScroll && tableEndRef.current) {
      tableEndRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [frames, autoScroll]);

  const handleSend = (e) => {
    e?.preventDefault();
    if (!socket) return;

    const hexBytes = txData
      .trim()
      .split(/\s+/)
      .map((b) => parseInt(b, 16) || 0);

    while (hexBytes.length < txDlc) hexBytes.push(0);

    socket.emit('can:send', {
      busId: parseInt(txBus, 10) || 0,
      canId: txId,
      dlc: txDlc,
      data: hexBytes.slice(0, txDlc)
    });

    setTxStatus('Sent!');
    setTimeout(() => setTxStatus(''), 2000);
  };

  const applyPreset = (id, data, dlc = 8) => {
    setTxId(id);
    setTxData(data);
    setTxDlc(dlc);
  };

  const filteredFrames = frames.filter((f) => {
    if (filterChannel !== 'all' && f.bus !== filterChannel) return false;
    if (filterId.trim()) {
      const hexStr = f.can_id.toString(16).toUpperCase();
      if (!hexStr.includes(filterId.trim().toUpperCase())) return false;
    }
    return true;
  });

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', backgroundColor: '#111827', color: '#E5E7EB', fontFamily: 'monospace', fontSize: '12px' }}>
      {/* Header Toolbar */}
      <div style={{ padding: '8px 12px', backgroundColor: '#1F2937', borderBottom: '1px solid #374151', display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px', flexWrap: 'wrap' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <Car size={16} color="#60A5FA" />
          <span style={{ fontWeight: 'bold', color: '#F3F4F6', letterSpacing: '0.05em' }}>CAN BUS ANALYZER</span>
          <span style={{ padding: '2px 6px', backgroundColor: '#374151', borderRadius: '4px', color: '#9CA3AF', fontSize: '10px' }}>
            {frames.length} pkts
          </span>
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
          {/* Channel Filter */}
          <select
            value={filterChannel}
            onChange={(e) => setFilterChannel(e.target.value)}
            style={{ backgroundColor: '#374151', color: '#F3F4F6', border: '1px solid #4B5563', borderRadius: '4px', padding: '2px 6px', fontSize: '11px' }}
          >
            <option value="all">All Channels</option>
            <option value="0">can0</option>
            <option value="1">can1</option>
          </select>

          {/* ID Filter */}
          <div style={{ display: 'flex', alignItems: 'center', backgroundColor: '#374151', borderRadius: '4px', padding: '2px 6px', border: '1px solid #4B5563' }}>
            <Filter size={12} color="#9CA3AF" style={{ marginRight: '4px' }} />
            <input
              type="text"
              placeholder="Filter ID (e.g. 7DF)"
              value={filterId}
              onChange={(e) => setFilterId(e.target.value)}
              style={{ background: 'transparent', border: 'none', color: '#F3F4F6', outline: 'none', width: '90px', fontSize: '11px' }}
            />
          </div>

          {/* Pause / Resume */}
          <button
            onClick={() => setIsPaused(!isPaused)}
            title={isPaused ? 'Resume live capture' : 'Pause capture'}
            style={{ backgroundColor: isPaused ? '#D97706' : '#374151', color: '#F3F4F6', border: '1px solid #4B5563', borderRadius: '4px', padding: '4px 8px', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '4px' }}
          >
            {isPaused ? <Play size={12} /> : <Pause size={12} />}
            <span style={{ fontSize: '11px' }}>{isPaused ? 'Resume' : 'Pause'}</span>
          </button>

          {/* Clear */}
          <button
            onClick={() => setFrames([])}
            title="Clear capture history"
            style={{ backgroundColor: '#374151', color: '#F3F4F6', border: '1px solid #4B5563', borderRadius: '4px', padding: '4px 8px', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '4px' }}
          >
            <Trash2 size={12} />
          </button>
        </div>
      </div>

      {/* Packet Table */}
      <div style={{ flex: 1, overflowY: 'auto', padding: '4px 8px' }}>
        <table style={{ width: '100%', borderCollapse: 'collapse', textAlign: 'left' }}>
          <thead>
            <tr style={{ color: '#9CA3AF', borderBottom: '1px solid #374151', fontSize: '11px' }}>
              <th style={{ padding: '6px 4px' }}>Time (us)</th>
              <th style={{ padding: '6px 4px' }}>Ch</th>
              <th style={{ padding: '6px 4px' }}>ID (Hex)</th>
              <th style={{ padding: '6px 4px' }}>Type</th>
              <th style={{ padding: '6px 4px' }}>DLC</th>
              <th style={{ padding: '6px 4px' }}>Payload (Hex Bytes)</th>
              <th style={{ padding: '6px 4px' }}>ASCII</th>
            </tr>
          </thead>
          <tbody>
            {filteredFrames.length === 0 ? (
              <tr>
                <td colSpan={7} style={{ textAlign: 'center', padding: '32px', color: '#6B7280' }}>
                  No CAN packets captured yet. Waiting for traffic...
                </td>
              </tr>
            ) : (
              filteredFrames.map((f) => {
                const hexId = '0x' + f.can_id.toString(16).toUpperCase().padStart(3, '0');
                const isDiagReq = f.can_id === 0x7df;
                const isDiagResp = f.can_id === 0x7e8 || (f.can_id >= 0x7e8 && f.can_id <= 0x7ef);
                const isTelemetry = f.can_id === 0x100 || f.can_id === 0x101;

                let idBadgeColor = '#4B5563';
                let idTextColor = '#E5E7EB';
                if (isDiagReq) {
                  idBadgeColor = '#1E3A8A';
                  idTextColor = '#93C5FD';
                } else if (isDiagResp) {
                  idBadgeColor = '#065F46';
                  idTextColor = '#6EE7B7';
                } else if (isTelemetry) {
                  idBadgeColor = '#78350F';
                  idTextColor = '#FCD34D';
                }

                const dataBytes = f.data.slice(0, f.can_dlc);
                const asciiStr = dataBytes
                  .map((b) => (b >= 32 && b <= 126 ? String.fromCharCode(b) : '.'))
                  .join('');

                return (
                  <tr key={f.uid} style={{ borderBottom: '1px solid #1F2937' }}>
                    <td style={{ padding: '4px', color: '#9CA3AF', whiteSpace: 'nowrap' }}>
                      {(f.timestamp_us % 1000000000).toLocaleString()}
                    </td>
                    <td style={{ padding: '4px', color: '#60A5FA' }}>can{f.bus}</td>
                    <td style={{ padding: '4px' }}>
                      <span style={{ padding: '1px 5px', borderRadius: '3px', backgroundColor: idBadgeColor, color: idTextColor, fontWeight: 'bold' }}>
                        {hexId}
                      </span>
                    </td>
                    <td style={{ padding: '4px', color: '#9CA3AF' }}>{f.can_id > 0x7ff ? 'EXT' : 'STD'}</td>
                    <td style={{ padding: '4px', color: '#E5E7EB' }}>{f.can_dlc}</td>
                    <td style={{ padding: '4px' }}>
                      <div style={{ display: 'flex', gap: '4px' }}>
                        {dataBytes.map((b, i) => (
                          <span
                            key={i}
                            style={{
                              padding: '1px 4px',
                              backgroundColor: '#1F2937',
                              borderRadius: '2px',
                              border: '1px solid #374151',
                              color: b === 0 ? '#6B7280' : '#F9FAFB'
                            }}
                          >
                            {b.toString(16).toUpperCase().padStart(2, '0')}
                          </span>
                        ))}
                      </div>
                    </td>
                    <td style={{ padding: '4px', color: '#9CA3AF' }}>{asciiStr}</td>
                  </tr>
                );
              })
            )}
            <tr ref={tableEndRef} />
          </tbody>
        </table>
      </div>

      {/* Packet Injector Panel */}
      <div style={{ padding: '8px 12px', backgroundColor: '#1F2937', borderTop: '1px solid #374151' }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: '6px' }}>
          <div style={{ display: 'flex', alignItems: 'center', gap: '6px', fontSize: '11px', color: '#9CA3AF', fontWeight: 'bold' }}>
            <Radio size={12} color="#10B981" />
            <span>TRANSMIT / INJECT PACKET</span>
            {txStatus && <span style={{ color: '#10B981' }}>{txStatus}</span>}
          </div>

          {/* Quick Presets */}
          <div style={{ display: 'flex', gap: '4px' }}>
            <button
              onClick={() => applyPreset('7DF', '02 01 0D 00 00 00 00 00')}
              style={{ fontSize: '10px', padding: '2px 6px', backgroundColor: '#374151', border: '1px solid #4B5563', borderRadius: '3px', color: '#93C5FD', cursor: 'pointer' }}
            >
              OBD2 Speed Req
            </button>
            <button
              onClick={() => applyPreset('7DF', '02 01 0C 00 00 00 00 00')}
              style={{ fontSize: '10px', padding: '2px 6px', backgroundColor: '#374151', border: '1px solid #4B5563', borderRadius: '3px', color: '#93C5FD', cursor: 'pointer' }}
            >
              OBD2 RPM Req
            </button>
            <button
              onClick={() => applyPreset('100', '50 00 0C 80 00 04 12 34')}
              style={{ fontSize: '10px', padding: '2px 6px', backgroundColor: '#374151', border: '1px solid #4B5563', borderRadius: '3px', color: '#FCD34D', cursor: 'pointer' }}
            >
              Telemetry Demo
            </button>
          </div>
        </div>

        <form onSubmit={handleSend} style={{ display: 'flex', gap: '6px', alignItems: 'center' }}>
          <select
            value={txBus}
            onChange={(e) => setTxBus(e.target.value)}
            style={{ backgroundColor: '#111827', color: '#F3F4F6', border: '1px solid #4B5563', borderRadius: '4px', padding: '4px 6px', fontSize: '11px' }}
          >
            <option value="0">can0</option>
            <option value="1">can1</option>
          </select>

          <input
            type="text"
            placeholder="ID (Hex)"
            value={txId}
            onChange={(e) => setTxId(e.target.value)}
            style={{ backgroundColor: '#111827', color: '#60A5FA', border: '1px solid #4B5563', borderRadius: '4px', padding: '4px 6px', width: '70px', fontSize: '11px', fontWeight: 'bold' }}
          />

          <select
            value={txDlc}
            onChange={(e) => setTxDlc(parseInt(e.target.value, 10))}
            style={{ backgroundColor: '#111827', color: '#F3F4F6', border: '1px solid #4B5563', borderRadius: '4px', padding: '4px 6px', fontSize: '11px' }}
          >
            {[1, 2, 3, 4, 5, 6, 7, 8].map((n) => (
              <option key={n} value={n}>
                DLC: {n}
              </option>
            ))}
          </select>

          <input
            type="text"
            placeholder="Data Hex (e.g. 02 01 0D 00 00 00 00 00)"
            value={txData}
            onChange={(e) => setTxData(e.target.value)}
            style={{ flex: 1, backgroundColor: '#111827', color: '#F3F4F6', border: '1px solid #4B5563', borderRadius: '4px', padding: '4px 8px', fontSize: '11px' }}
          />

          <button
            type="submit"
            style={{ backgroundColor: '#2563EB', color: '#FFFFFF', border: 'none', borderRadius: '4px', padding: '4px 12px', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '4px', fontWeight: 'bold', fontSize: '11px' }}
          >
            <Send size={12} />
            <span>Send</span>
          </button>
        </form>
      </div>
    </div>
  );
}
