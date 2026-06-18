import { useState } from 'react';
import { Cpu, ChevronDown, ChevronRight } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function RegisterMonitor() {
  const { registers, hiddenTraceKeys, toggleTraceKey } = useDashboard();
  const [expandedDevices, setExpandedDevices] = useState({});

  // Group by deviceName
  const groupedRegisters = registers.reduce((acc, reg) => {
    const dev = reg.deviceName || 'unknown';
    if (!acc[dev]) {
      acc[dev] = [];
    }
    acc[dev].push(reg);
    return acc;
  }, {});

  const isExpanded = (devName) => expandedDevices[devName] !== false;

  const toggleExpand = (devName) => {
    setExpandedDevices(prev => ({
      ...prev,
      [devName]: !isExpanded(devName)
    }));
  };

  const deviceNames = Object.keys(groupedRegisters);

  return (
    <div className="register-pane" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      <div className="panel-header"><Cpu size={16} /> Register Monitor</div>
      <div className="register-viewport" style={{ flex: 1, overflowY: 'auto', padding: '0.5rem' }}>
        {deviceNames.length === 0 ? (
          <div style={{ padding: '1rem', color: '#8b949e', fontSize: '0.75rem', textAlign: 'center' }}>
            No registers found.
          </div>
        ) : (
          deviceNames.map((devName) => {
            const devRegs = groupedRegisters[devName];
            const expanded = isExpanded(devName);

            return (
              <div key={devName} style={{ marginBottom: '0.75rem', border: '1px solid #30363d', borderRadius: '6px', overflow: 'hidden', background: '#161b22' }}>
                {/* Collapsible Header */}
                <div 
                  onClick={() => toggleExpand(devName)}
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'space-between',
                    padding: '8px 12px',
                    background: '#21262d',
                    cursor: 'pointer',
                    userSelect: 'none',
                    borderBottom: expanded ? '1px solid #30363d' : 'none'
                  }}
                >
                  <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                    {expanded ? <ChevronDown size={14} style={{ color: '#8b949e' }} /> : <ChevronRight size={14} style={{ color: '#8b949e' }} />}
                    <span style={{ fontSize: '0.75rem', fontWeight: 600, color: '#e6edf3', fontFamily: 'monospace' }}>
                      {devName}
                    </span>
                  </div>
                  <span style={{ 
                    fontSize: '0.65rem', 
                    backgroundColor: '#161b22', 
                    padding: '2px 6px', 
                    borderRadius: '10px',
                    color: '#8b949e',
                    border: '1px solid #30363d'
                  }}>
                    {devRegs.length} regs
                  </span>
                </div>

                {/* Collapsible Table Content */}
                {expanded && (
                  <div style={{ padding: '6px', background: '#0d1117' }}>
                    <table className="register-table">
                      <thead>
                        <tr>
                          <th style={{ width: '50px', textAlign: 'center', padding: '6px' }}>Trace</th>
                          <th style={{ padding: '6px' }}>Name</th>
                          <th style={{ padding: '6px' }}>Offset</th>
                          <th style={{ padding: '6px' }}>Value (Hex)</th>
                        </tr>
                      </thead>
                      <tbody>
                        {devRegs.map((reg, i) => {
                          const key = `${reg.deviceName}_${reg.name}`;
                          const isTraced = !hiddenTraceKeys[key];

                          return (
                            <tr key={i} style={{ background: isTraced ? 'transparent' : 'rgba(248, 81, 73, 0.02)' }}>
                              <td style={{ textAlign: 'center', padding: '6px' }}>
                                <input
                                  type="checkbox"
                                  checked={isTraced}
                                  onChange={() => toggleTraceKey(key)}
                                  title={isTraced ? "Disable tracing for this register" : "Enable tracing for this register"}
                                  style={{
                                    cursor: 'pointer',
                                    accentColor: '#58a6ff',
                                    verticalAlign: 'middle',
                                    margin: 0
                                  }}
                                />
                              </td>
                              <td className="reg-cell-name" style={{ fontWeight: 600, padding: '6px' }}>
                                {reg.name}
                              </td>
                              <td className="reg-offset" style={{ padding: '6px' }}>{reg.offset}</td>
                              <td className="reg-val" style={{ padding: '6px' }}>{reg.value}</td>
                            </tr>
                          );
                        })}
                      </tbody>
                    </table>
                  </div>
                )}
              </div>
            );
          })
        )}
      </div>
    </div>
  );
}

export default RegisterMonitor;
