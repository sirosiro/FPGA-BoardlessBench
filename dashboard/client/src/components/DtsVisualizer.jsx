import { useState, useEffect } from 'react';
import { Cpu, Layers, AlertTriangle, CheckCircle, Sparkles, RefreshCw, ChevronDown, ChevronRight, MapPin, PlusCircle } from 'lucide-react';

function formatBytes(bytes) {
  if (bytes <= 0) return '0 B';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`;
}

function DtsVisualizer() {
  const [dtsData, setDtsData] = useState(null);
  const [loading, setLoading] = useState(true);
  const [activeTab, setActiveTab] = useState('map'); // 'map' | 'ai'
  const [aiLoading, setAiLoading] = useState(false);
  const [aiResult, setAiResult] = useState(null);
  const [customErrorInput, setCustomErrorInput] = useState('');
  const [expandedDevices, setExpandedDevices] = useState({});

  const fetchDtsData = async () => {
    setLoading(true);
    try {
      const res = await fetch('/api/dts/tree');
      if (res.ok) {
        const data = await res.json();
        setDtsData(data);
      }
    } catch (e) {
      console.error('Failed to fetch DTS tree data:', e);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchDtsData();
  }, []);

  const toggleExpand = (deviceName) => {
    setExpandedDevices(prev => ({
      ...prev,
      [deviceName]: !prev[deviceName]
    }));
  };

  const handleRunAiDiagnose = async () => {
    setAiLoading(true);
    setAiResult(null);
    try {
      const res = await fetch('/api/dts/diagnose', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          dtsContent: dtsData?.rawDts || '',
          errorMessage: customErrorInput || dtsData?.lastError || ''
        })
      });
      if (res.ok) {
        const result = await res.json();
        setAiResult(result);
      } else {
        setAiResult({
          success: false,
          summary: 'Diagnosis API Error',
          detailed_explanation: 'Failed to contact AI Diagnostic service.',
          suggested_diff: ''
        });
      }
    } catch (e) {
      setAiResult({
        success: false,
        summary: 'Network Error',
        detailed_explanation: e.message,
        suggested_diff: ''
      });
    } finally {
      setAiLoading(false);
    }
  };

  if (loading) {
    return (
      <div className="dts-pane" style={{ padding: '1.5rem', color: '#ccc', background: '#181818', height: '100%' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '0.85rem' }}>
          <RefreshCw className="animate-spin" size={16} />
          <span>Loading Hardware DeviceTree Diagram...</span>
        </div>
      </div>
    );
  }

  const memoryBlocks = dtsData?.memory_map || dtsData?.memoryMap || [];
  const hasOverlap = dtsData?.has_overlap || dtsData?.hasOverlap || false;
  const devices = dtsData?.devices || [];

  const palette = ['#007acc', '#388a34', '#b5ce28', '#ce9178', '#c586c0', '#4ec9b0'];

  // Map device extra metadata by name
  const devMap = {};
  devices.forEach(d => { devMap[d.name] = d; });

  // Sort blocks by Base Address Descending (High Address -> Low Address)
  const sortedBlocks = [...memoryBlocks].map(blk => {
    const dev = devMap[blk.name] || {};
    const base = blk.base_addr !== undefined ? blk.base_addr : parseInt(blk.hexStart || '0', 16);
    const size = blk.sizeBytes || 4096;
    const end = base + size - 1;
    return {
      ...blk,
      devInfo: dev,
      baseAddr: base,
      endAddr: end,
      sizeBytes: size,
      hexStart: blk.hexStart || `0x${base.toString(16).padStart(8, '0')}`,
      hexEnd: `0x${end.toString(16).padStart(8, '0')}`
    };
  }).sort((a, b) => b.baseAddr - a.baseAddr);

  // Calculate gaps between memory blocks (High to Low)
  const mapItems = [];
  for (let i = 0; i < sortedBlocks.length; i++) {
    const current = sortedBlocks[i];

    // Gap above current block (if not first block, gap between prev block base and current block end)
    if (i > 0) {
      const prev = sortedBlocks[i - 1];
      const gapStart = current.endAddr + 1;
      const gapEnd = prev.baseAddr - 1;
      const gapSize = prev.baseAddr - gapStart;
      if (gapSize > 0) {
        mapItems.push({
          isGap: true,
          gapStart,
          gapEnd,
          gapSize,
          hexStart: `0x${gapStart.toString(16).padStart(8, '0')}`,
          hexEnd: `0x${gapEnd.toString(16).padStart(8, '0')}`
        });
      }
    }

    mapItems.push({
      isGap: false,
      ...current
    });
  }

  // Calculate High Unmapped Gap (from 0xFFFFFFFF to highest block end)
  const highestEnd = sortedBlocks.length > 0 ? sortedBlocks[0].endAddr : 0;
  const highGapSize = 0xFFFFFFFF - highestEnd;
  const highGapStart = highestEnd + 1;

  // Calculate Low Unmapped Gap (from lowest block base to 0x00000000)
  const lowestBase = sortedBlocks.length > 0 ? sortedBlocks[sortedBlocks.length - 1].baseAddr : 0;
  const lowGapSize = lowestBase;
  const lowGapEnd = lowestBase > 0 ? lowestBase - 1 : 0;

  return (
    <div className="dts-pane" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden', background: '#181818', color: '#cccccc', fontFamily: 'Inter, system-ui, -apple-system, sans-serif' }}>
      {/* Header Bar */}
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '8px 12px', background: '#252526', borderBottom: '1px solid #2d2d2d', flexShrink: 0 }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px', fontWeight: 600, fontSize: '0.85rem', color: '#ffffff' }}>
          <Cpu size={16} style={{ color: '#007acc' }} />
          <span>DTS Visualizer</span>
        </div>
        <div style={{ display: 'flex', gap: '4px' }}>
          <button 
            onClick={() => setActiveTab('map')} 
            style={{ 
              background: activeTab === 'map' ? '#007acc' : '#333333', 
              color: '#ffffff', border: 'none', padding: '4px 8px', borderRadius: '4px', cursor: 'pointer', fontSize: '0.75rem', fontWeight: 500, display: 'flex', alignItems: 'center', gap: '4px' 
            }}
          >
            <Layers size={13} /> Address Map
          </button>
          <button 
            onClick={() => setActiveTab('ai')} 
            style={{ 
              background: activeTab === 'ai' ? '#007acc' : '#333333', 
              color: '#ffffff', border: 'none', padding: '4px 8px', borderRadius: '4px', cursor: 'pointer', fontSize: '0.75rem', fontWeight: 500, display: 'flex', alignItems: 'center', gap: '4px' 
            }}
          >
            <Sparkles size={13} style={{ color: '#e5c07b' }} /> AI Checker
          </button>
          <button onClick={fetchDtsData} title="Refresh" style={{ background: '#333333', color: '#aaaaaa', border: 'none', padding: '4px 6px', borderRadius: '4px', cursor: 'pointer' }}>
            <RefreshCw size={13} />
          </button>
        </div>
      </div>

      {/* Scrollable Content Body */}
      <div style={{ flex: 1, overflowY: 'auto', padding: '10px' }}>
        {activeTab === 'map' && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px' }}>
            {/* Status Summary Banner */}
            {hasOverlap ? (
              <div style={{ background: '#451515', border: '1px solid #f44747', padding: '8px 10px', borderRadius: '6px', display: 'flex', alignItems: 'center', gap: '8px' }}>
                <AlertTriangle style={{ color: '#f44747', flexShrink: 0 }} size={18} />
                <div>
                  <div style={{ fontWeight: 600, color: '#ff8888', fontSize: '0.8rem' }}>Address Conflict Detected</div>
                  <div style={{ fontSize: '0.72rem', color: '#d4d4d4' }}>Overlapping memory regions found. Review highlighted conflicts below.</div>
                </div>
              </div>
            ) : (
              <div style={{ background: '#17301e', border: '1px solid #2e7d32', padding: '6px 10px', borderRadius: '6px', display: 'flex', alignItems: 'center', gap: '6px', fontSize: '0.78rem', color: '#81c784' }}>
                <CheckCircle size={15} />
                <span>All {memoryBlocks.length} peripherals cleanly mapped (Zero Collisions)</span>
              </div>
            )}

            {/* Unified Hardware Memory Map Diagram */}
            <div style={{ background: '#1e1e1e', border: '1px solid #2d2d2d', borderRadius: '6px', padding: '10px' }}>
              <div style={{ fontSize: '0.75rem', fontWeight: 600, color: '#888888', marginBottom: '8px', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                <span style={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
                  <MapPin size={13} style={{ color: '#007acc' }} /> Datasheet 32-bit Address Space Map
                </span>
                <span style={{ fontSize: '0.7rem', color: '#666', fontFamily: 'monospace' }}>High (0xFFFFFFFF) → Low (0x0)</span>
              </div>

              {/* Memory Layout Frame */}
              <div style={{ border: '1px solid #333333', borderRadius: '4px', padding: '6px', background: '#121212', display: 'flex', flexDirection: 'column', gap: '4px' }}>
                
                {/* System High Address Label */}
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.68rem', fontFamily: 'monospace', color: '#666666', borderBottom: '1px dashed #262626', paddingBottom: '3px' }}>
                  <span>0xFFFFFFFF</span>
                  <span>System High Reserved</span>
                </div>

                {/* High Unmapped Region */}
                {highGapSize > 0 && (
                  <div style={{ background: 'repeating-linear-gradient(45deg, #161616, #161616 6px, #1a1a1a 6px, #1a1a1a 12px)', border: '1px dashed #2a2a2a', padding: '6px 8px', borderRadius: '4px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', color: '#666666', fontSize: '0.68rem', fontFamily: 'monospace' }}>
                    <span>Unmapped High Space ({formatBytes(highGapSize)})</span>
                    <span>0x{highGapStart.toString(16).padStart(8, '0')}</span>
                  </div>
                )}

                {/* Interleaved Gaps & Mapped Devices */}
                {mapItems.map((item, idx) => {
                  if (item.isGap) {
                    return (
                      <div 
                        key={`gap-${idx}`}
                        style={{ 
                          background: 'repeating-linear-gradient(45deg, #161616, #161616 6px, #1a1a1a 6px, #1a1a1a 12px)', 
                          border: '1px dashed #2a2a2a', 
                          borderRadius: '4px', 
                          padding: '5px 8px', 
                          display: 'flex', 
                          justifyContent: 'space-between', 
                          alignItems: 'center', 
                          fontSize: '0.68rem', 
                          fontFamily: 'monospace', 
                          color: '#888888' 
                        }}
                      >
                        <span style={{ display: 'flex', alignItems: 'center', gap: '4px' }}>
                          <PlusCircle size={11} style={{ color: '#666666' }} />
                          Unmapped Gap: <strong>{formatBytes(item.gapSize)}</strong> (Available for new IP)
                        </span>
                        <span style={{ color: '#666666', fontSize: '0.65rem' }}>{item.hexStart} — {item.hexEnd}</span>
                      </div>
                    );
                  }

                  // Render Mapped Device Box with Expandable Accordion
                  const dev = item;
                  const isExpanded = !!expandedDevices[dev.name];
                  const color = dev.overlap ? '#f44747' : palette[idx % palette.length];
                  const devObj = dev.devInfo || {};
                  const regCount = devObj.registers?.length || 0;

                  return (
                    <div 
                      key={`dev-${dev.name}`} 
                      style={{ 
                        background: dev.overlap ? 'rgba(244,71,71,0.12)' : '#181818', 
                        border: isExpanded ? `1px solid ${color}` : dev.overlap ? '1px solid #f44747' : '1px solid #2d2d2d', 
                        borderLeft: `5px solid ${color}`,
                        borderRadius: '4px', 
                        overflow: 'hidden',
                        transition: 'all 0.15s ease'
                      }}
                    >
                      {/* Compact Device Card Header (Click to Expand) */}
                      <div 
                        onClick={() => toggleExpand(dev.name)}
                        style={{ 
                          padding: '7px 10px', 
                          display: 'flex', 
                          justifyContent: 'space-between', 
                          alignItems: 'center', 
                          cursor: 'pointer',
                          background: isExpanded ? 'rgba(255,255,255,0.03)' : 'transparent'
                        }}
                        title="Click to toggle details"
                      >
                        <div style={{ display: 'flex', alignItems: 'center', gap: '6px', minWidth: 0 }}>
                          <span style={{ fontWeight: 700, color: color, fontSize: '0.82rem', fontFamily: 'monospace', textOverflow: 'ellipsis', overflow: 'hidden', whiteSpace: 'nowrap' }}>
                            {dev.name}
                          </span>
                          {dev.overlap && (
                            <span style={{ background: '#f44747', color: '#fff', fontSize: '0.62rem', padding: '1px 4px', borderRadius: '3px', fontWeight: 'bold' }}>
                              CONFLICT
                            </span>
                          )}
                        </div>

                        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                          <span style={{ fontSize: '0.7rem', color: '#9cdcfe', fontFamily: 'monospace', background: '#111111', padding: '1px 5px', borderRadius: '3px', border: '1px solid #2a2a2a' }}>
                            {dev.hexStart}
                          </span>
                          {isExpanded ? <ChevronDown size={14} style={{ color: color }} /> : <ChevronRight size={14} style={{ color: '#666666' }} />}
                        </div>
                      </div>

                      {/* Expandable Details Container */}
                      {isExpanded && (
                        <div style={{ padding: '8px 10px 10px 10px', borderTop: '1px solid #252526', background: '#111111', fontSize: '0.75rem' }}>
                          <div style={{ marginBottom: '6px', color: '#aaaaaa' }}>
                            <span style={{ color: '#666666' }}>compatible: </span>
                            <span style={{ color: '#ce9178', fontFamily: 'monospace' }}>
                              "{devObj.extra?.compatible || devObj.type || 'generic'}"
                            </span>
                          </div>

                          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '4px', marginBottom: '8px', color: '#aaaaaa', fontFamily: 'monospace' }}>
                            <div>Range: <span style={{ color: '#b5ce28' }}>{dev.hexStart} — {dev.hexEnd}</span></div>
                            <div>Size: <span style={{ color: '#9cdcfe' }}>{formatBytes(dev.sizeBytes)}</span></div>
                            <div>UIO Label: <span style={{ color: '#4ec9b0' }}>{devObj.path || 'N/A'}</span></div>
                          </div>

                          {regCount > 0 && (
                            <div style={{ borderTop: '1px dashed #262626', paddingTop: '6px' }}>
                              <div style={{ color: '#777777', fontSize: '0.7rem', marginBottom: '4px' }}>
                                Defined Registers ({regCount}):
                              </div>
                              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '4px' }}>
                                {devObj.registers.map((r, rIdx) => (
                                  <span 
                                    key={rIdx} 
                                    style={{ 
                                      background: '#1a1a1a', 
                                      border: '1px solid #333333', 
                                      padding: '2px 6px', 
                                      borderRadius: '3px', 
                                      fontSize: '0.7rem', 
                                      fontFamily: 'monospace' 
                                    }}
                                  >
                                    <strong style={{ color: '#dcdcaa' }}>{r.name}</strong>
                                    {r.logical_name && r.logical_name !== r.name && <span style={{ color: '#4ec9b0' }}> ({r.logical_name})</span>}
                                    <span style={{ color: '#b5ce28' }}> @ {r.offset}</span>
                                    {r.direction_mode && <span style={{ color: '#ce9178', marginLeft: '4px' }}>[{r.direction_mode}]</span>}
                                  </span>
                                ))}
                              </div>
                            </div>
                          )}

                          {/* Attached Bus Slaves (Included from DTSI files) */}
                          {((devObj.i2c_slaves && devObj.i2c_slaves.length > 0) || (devObj.spi_slaves && devObj.spi_slaves.length > 0)) && (
                            <div style={{ borderTop: '1px dashed #262626', paddingTop: '6px', marginTop: '6px' }}>
                              <div style={{ color: '#58a6ff', fontSize: '0.7rem', marginBottom: '4px', fontWeight: 600 }}>
                                Attached Bus Peripherals (DTSI Included):
                              </div>
                              <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
                                {(devObj.i2c_slaves || []).map((slave, sIdx) => (
                                  <div 
                                    key={`i2c-slave-${sIdx}`}
                                    style={{
                                      background: '#161b22',
                                      border: '1px solid #30363d',
                                      borderRadius: '4px',
                                      padding: '4px 8px',
                                      display: 'flex',
                                      alignItems: 'center',
                                      justifyContent: 'space-between',
                                      fontSize: '0.72rem',
                                      fontFamily: 'monospace'
                                    }}
                                  >
                                    <span style={{ color: '#ce9178' }}>"{slave.compatible}"</span>
                                    <span style={{ color: '#b5ce28', fontWeight: 'bold' }}>I2C @ 0x{slave.addr.toString(16).toUpperCase()}</span>
                                  </div>
                                ))}
                                {(devObj.spi_slaves || []).map((slave, sIdx) => (
                                  <div 
                                    key={`spi-slave-${sIdx}`}
                                    style={{
                                      background: '#161b22',
                                      border: '1px solid #30363d',
                                      borderRadius: '4px',
                                      padding: '4px 8px',
                                      display: 'flex',
                                      alignItems: 'center',
                                      justifyContent: 'space-between',
                                      fontSize: '0.72rem',
                                      fontFamily: 'monospace'
                                    }}
                                  >
                                    <span style={{ color: '#ce9178' }}>"{slave.compatible}"</span>
                                    <span style={{ color: '#b5ce28', fontWeight: 'bold' }}>SPI CS {slave.cs}</span>
                                  </div>
                                ))}
                              </div>
                            </div>
                          )}
                        </div>
                      )}
                    </div>
                  );
                })}

                {/* Low Unmapped Region */}
                {lowGapSize > 0 && (
                  <div style={{ background: 'repeating-linear-gradient(45deg, #161616, #161616 6px, #1a1a1a 6px, #1a1a1a 12px)', border: '1px dashed #2a2a2a', padding: '6px 8px', borderRadius: '4px', display: 'flex', justifyContent: 'space-between', alignItems: 'center', color: '#666666', fontSize: '0.68rem', fontFamily: 'monospace' }}>
                    <span>Unmapped Low Space ({formatBytes(lowGapSize)})</span>
                    <span>0x00000000 — 0x{lowGapEnd.toString(16).padStart(8, '0')}</span>
                  </div>
                )}

                {/* System Low Address Label */}
                <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: '0.68rem', fontFamily: 'monospace', color: '#666666', borderTop: '1px dashed #262626', paddingTop: '3px' }}>
                  <span>0x00000000</span>
                  <span>System Base Low</span>
                </div>
              </div>
            </div>
          </div>
        )}

        {activeTab === 'ai' && (
          <div style={{ display: 'flex', flexDirection: 'column', gap: '10px' }}>
            <div style={{ background: '#1e1e1e', border: '1px solid #2d2d2d', padding: '10px', borderRadius: '6px' }}>
              <div style={{ fontWeight: 600, fontSize: '0.82rem', color: '#e5c07b', marginBottom: '4px', display: 'flex', alignItems: 'center', gap: '6px' }}>
                <Sparkles size={15} />
                <span>AI Smart DTS Diagnostics</span>
              </div>
              <div style={{ fontSize: '0.75rem', color: '#888888', marginBottom: '8px' }}>
                Paste compiler error log to get root cause diagnosis & diff.
              </div>

              <textarea 
                value={customErrorInput}
                onChange={(e) => setCustomErrorInput(e.target.value)}
                placeholder="Optional: Paste dtc error log..."
                style={{ width: '100%', height: '70px', background: '#121212', color: '#d4d4d4', border: '1px solid #2d2d2d', borderRadius: '4px', padding: '6px', fontSize: '0.75rem', fontFamily: 'monospace', resize: 'vertical' }}
              />

              <button 
                onClick={handleRunAiDiagnose} 
                disabled={aiLoading}
                style={{ 
                  marginTop: '8px', 
                  width: '100%',
                  background: aiLoading ? '#444444' : '#007acc', 
                  color: '#ffffff', border: 'none', padding: '6px 12px', borderRadius: '4px', cursor: aiLoading ? 'not-allowed' : 'pointer', fontWeight: 600, fontSize: '0.78rem', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '6px' 
                }}
              >
                {aiLoading ? <RefreshCw className="animate-spin" size={14} /> : <Sparkles size={14} />}
                <span>{aiLoading ? 'Analyzing DTS...' : 'Run AI DTS Diagnosis'}</span>
              </button>
            </div>

            {/* Diagnosis Result Card */}
            {aiResult && (
              <div style={{ background: '#1e1e1e', border: aiResult.success ? '1px solid #2e7d32' : '1px solid #f44747', padding: '10px', borderRadius: '6px' }}>
                <div style={{ fontWeight: 600, fontSize: '0.85rem', color: aiResult.success ? '#81c784' : '#ff8888', marginBottom: '6px' }}>
                  {aiResult.summary || 'DTS Diagnosis Report'}
                </div>

                <div style={{ fontSize: '0.78rem', color: '#cccccc', lineHeight: 1.5, marginBottom: '8px', whiteSpace: 'pre-wrap' }}>
                  {aiResult.detailed_explanation}
                </div>

                {aiResult.suggested_diff && (
                  <div>
                    <div style={{ fontSize: '0.72rem', fontWeight: 600, color: '#9cdcfe', marginBottom: '4px' }}>Suggested Patch:</div>
                    <pre style={{ background: '#121212', border: '1px solid #2d2d2d', padding: '8px', borderRadius: '4px', color: '#ce9178', fontSize: '0.72rem', fontFamily: 'monospace', overflowX: 'auto' }}>
                      {aiResult.suggested_diff}
                    </pre>
                  </div>
                )}
              </div>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

export default DtsVisualizer;
