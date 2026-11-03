import { useState, useMemo } from 'react';
import { 
  Cpu, 
  ChevronDown, 
  ChevronRight, 
  AlertTriangle, 
  Layers, 
  LayoutGrid, 
  List, 
  Search, 
  X, 
  ChevronsUpDown 
} from 'lucide-react';
import { useDashboard } from './DashboardContext';

function RegisterMonitor() {
  const { 
    effectiveRegisters: registers, 
    hiddenTraceKeys, 
    toggleTraceKey, 
    isTimeTravelActive, 
    timeTravelState, 
    setTraceKeysVisibility,
    monitorState,
    updateMonitorState
  } = useDashboard();

  const [searchQuery, setSearchQuery] = useState('');

  const layoutMode = monitorState?.layoutMode || 'grid';
  const expandedDevices = monitorState?.expandedDevices || {};
  const expandedGroups = monitorState?.expandedGroups || {};

  // Group by deviceName
  const groupedRegisters = useMemo(() => {
    return registers.reduce((acc, reg) => {
      const dev = reg.deviceName || 'unknown';
      if (!acc[dev]) {
        acc[dev] = [];
      }
      acc[dev].push(reg);
      return acc;
    }, {});
  }, [registers]);

  const deviceNames = Object.keys(groupedRegisters);

  const filterQuery = searchQuery.trim().toLowerCase();

  const isExpanded = (devName) => expandedDevices[devName] !== false;

  const toggleExpand = (devName) => {
    if (updateMonitorState) {
      updateMonitorState({
        expandedDevices: {
          ...expandedDevices,
          [devName]: !isExpanded(devName)
        }
      });
    }
  };

  const isGroupExpanded = (groupKey) => {
    if (filterQuery) return true; // Auto-expand matching groups during search
    return expandedGroups[groupKey] !== false;
  };

  const toggleGroupExpand = (groupKey) => {
    if (updateMonitorState) {
      updateMonitorState({
        expandedGroups: {
          ...expandedGroups,
          [groupKey]: !isGroupExpanded(groupKey)
        }
      });
    }
  };

  const toggleLayoutMode = () => {
    if (updateMonitorState) {
      updateMonitorState({
        layoutMode: layoutMode === 'grid' ? 'list' : 'grid'
      });
    }
  };

  const handleToggleExpandAll = () => {
    if (!updateMonitorState) return;

    // Check if any group is currently collapsed
    let anyCollapsed = false;
    deviceNames.forEach(devName => {
      const devRegs = groupedRegisters[devName] || [];
      const subGroups = devRegs.reduce((acc, r) => {
        acc[r.group || 'General'] = true;
        return acc;
      }, {});
      Object.keys(subGroups).forEach(grp => {
        if (expandedGroups[`${devName}_${grp}`] === false) {
          anyCollapsed = true;
        }
      });
    });

    const targetState = anyCollapsed;
    const newExpanded = {};
    deviceNames.forEach(devName => {
      const devRegs = groupedRegisters[devName] || [];
      const subGroups = devRegs.reduce((acc, r) => {
        acc[r.group || 'General'] = true;
        return acc;
      }, {});
      Object.keys(subGroups).forEach(grp => {
        newExpanded[`${devName}_${grp}`] = targetState;
      });
    });

    updateMonitorState({
      expandedGroups: newExpanded
    });
  };

  const filterRegister = (r) => {
    if (!filterQuery) return true;
    if (r.name && r.name.toLowerCase().includes(filterQuery)) return true;
    if (r.offset && r.offset.toLowerCase().includes(filterQuery)) return true;
    if (r.value && r.value.toLowerCase().includes(filterQuery)) return true;
    if (r.group && r.group.toLowerCase().includes(filterQuery)) return true;
    return false;
  };

  const renderRegisterTable = (regs, maxHeight = '420px') => (
    <div style={{ maxHeight: maxHeight, overflowY: 'auto' }}>
      <table className="register-table" style={{ width: '100%', borderCollapse: 'collapse', tableLayout: 'fixed' }}>
        <thead style={{ position: 'sticky', top: 0, background: '#1c2128', zIndex: 2, boxShadow: '0 1px 0 #30363d' }}>
          <tr>
            <th style={{ width: '38px', textAlign: 'center', padding: '5px 2px' }}>Trace</th>
            <th style={{ padding: '5px 6px' }}>Name</th>
            <th style={{ width: '68px', padding: '5px 4px' }}>Offset</th>
            <th style={{ width: '85px', padding: '5px 6px', textAlign: 'right' }}>Value</th>
          </tr>
        </thead>
        <tbody>
          {regs.map((reg, i) => {
            const key = `${reg.deviceName}_${reg.name}`;
            const isTraced = !hiddenTraceKeys[key];
            const isGhost = !!reg.isUndeclared;

            return (
              <tr key={i} style={{ 
                background: isGhost 
                  ? 'rgba(218, 54, 51, 0.14)' 
                  : (isTraced ? 'transparent' : 'rgba(248, 81, 73, 0.02)'),
                borderLeft: isGhost ? '3px solid #da3633' : 'none'
              }}>
                <td style={{ textAlign: 'center', padding: '5px 2px' }}>
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
                <td className="reg-cell-name" style={{ 
                  fontWeight: 600, 
                  padding: '5px 6px',
                  overflow: 'hidden',
                  textOverflow: 'ellipsis',
                  whiteSpace: 'nowrap'
                }} title={reg.name}>
                  <div style={{ display: 'flex', alignItems: 'center', gap: '4px', color: isGhost ? '#ff7b72' : undefined }}>
                    {isGhost && <AlertTriangle size={12} style={{ color: '#f85149', flexShrink: 0 }} />}
                    <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                      {reg.name}
                    </span>
                  </div>
                  {isGhost && (
                    <div style={{ fontSize: '0.62rem', color: '#d29922', fontWeight: 'normal', marginTop: '1px' }}>
                      ⚠️ Not in DTS (Ghost)
                    </div>
                  )}
                </td>
                <td className="reg-offset" style={{ 
                  padding: '5px 4px', 
                  color: isGhost ? '#ff7b72' : undefined,
                  fontWeight: isGhost ? 600 : 'normal',
                  fontSize: '0.72rem'
                }}>
                  {reg.offset}
                </td>
                <td className="reg-val" style={{ 
                  padding: '5px 6px',
                  color: isGhost ? '#ffa198' : (reg.isHistorical ? '#00f2fe' : undefined),
                  fontWeight: (isGhost || reg.isHistorical) ? 600 : 'normal',
                  fontFamily: 'monospace',
                  textAlign: 'right',
                  fontSize: '0.72rem'
                }}>
                  {reg.value}
                </td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );

  return (
    <div className="register-pane" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      {/* Panel Header */}
      <div className="panel-header" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
          <Cpu size={16} /> 
          <span>Register Monitor</span>
          <span style={{ 
            fontSize: '0.65rem', 
            background: '#21262d', 
            color: '#8b949e', 
            padding: '1px 6px', 
            borderRadius: '10px',
            border: '1px solid #30363d' 
          }}>
            {registers.length}
          </span>
        </div>
        {isTimeTravelActive && (
          <span style={{
            fontSize: '0.65rem',
            backgroundColor: 'rgba(0, 242, 254, 0.15)',
            border: '1px solid #00f2fe',
            color: '#00f2fe',
            padding: '1px 6px',
            borderRadius: '10px',
            fontWeight: 'bold',
            display: 'flex',
            alignItems: 'center',
            gap: '4px'
          }}>
            🔒 TIME-TRAVEL #{timeTravelState?.snapshot?.index} (READ-ONLY)
          </span>
        )}
      </div>

      {/* Toolbar Strip: Search, Layout Toggle, Expand/Collapse */}
      <div style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        padding: '5px 8px',
        background: '#161b22',
        borderBottom: '1px solid #30363d',
        gap: '6px',
        flexShrink: 0
      }}>
        {/* Search input */}
        <div style={{
          display: 'flex',
          alignItems: 'center',
          gap: '6px',
          background: '#0d1117',
          border: '1px solid #30363d',
          borderRadius: '4px',
          padding: '2px 8px',
          flex: 1,
          maxWidth: '260px'
        }}>
          <Search size={12} style={{ color: '#8b949e', flexShrink: 0 }} />
          <input
            type="text"
            value={searchQuery}
            onChange={(e) => setSearchQuery(e.target.value)}
            placeholder="Filter registers..."
            style={{
              background: 'transparent',
              border: 'none',
              color: '#e6edf3',
              fontSize: '0.72rem',
              outline: 'none',
              width: '100%',
              minWidth: '60px'
            }}
          />
          {searchQuery && (
            <X 
              size={12} 
              style={{ color: '#8b949e', cursor: 'pointer', flexShrink: 0 }} 
              onClick={() => setSearchQuery('')}
              title="Clear filter"
            />
          )}
        </div>

        {/* Action Controls */}
        <div style={{ display: 'flex', alignItems: 'center', gap: '5px', flexShrink: 0 }}>
          {/* Layout Mode Toggle */}
          <button
            type="button"
            onClick={toggleLayoutMode}
            title={layoutMode === 'grid' ? "Switch to Single-Column List" : "Switch to Multi-Column Grid"}
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '4px',
              padding: '2px 7px',
              background: '#21262d',
              border: '1px solid #30363d',
              borderRadius: '4px',
              color: layoutMode === 'grid' ? '#58a6ff' : '#8b949e',
              fontSize: '0.68rem',
              cursor: 'pointer'
            }}
          >
            {layoutMode === 'grid' ? <LayoutGrid size={13} /> : <List size={13} />}
            <span>{layoutMode === 'grid' ? 'Grid' : 'List'}</span>
          </button>

          {/* Expand / Collapse All */}
          <button
            type="button"
            onClick={handleToggleExpandAll}
            title="Expand or Collapse All Groups"
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: '4px',
              padding: '2px 7px',
              background: '#21262d',
              border: '1px solid #30363d',
              borderRadius: '4px',
              color: '#8b949e',
              fontSize: '0.68rem',
              cursor: 'pointer'
            }}
          >
            <ChevronsUpDown size={13} />
            <span>Toggle All</span>
          </button>
        </div>
      </div>

      {/* Register Viewport */}
      <div className="register-viewport" style={{ flex: 1, overflowY: 'auto', padding: '0.5rem' }}>
        {deviceNames.length === 0 ? (
          <div style={{ padding: '1rem', color: '#8b949e', fontSize: '0.75rem', textAlign: 'center' }}>
            No registers found.
          </div>
        ) : (
          deviceNames.map((devName) => {
            const devRegs = groupedRegisters[devName];
            const ghostRegs = devRegs.filter(r => r.isUndeclared);
            const expanded = isExpanded(devName);

            // Group by register group property
            const subGroups = devRegs.reduce((acc, r) => {
              const grp = r.group || 'General';
              if (!acc[grp]) acc[grp] = [];
              acc[grp].push(r);
              return acc;
            }, {});
            const subGroupNames = Object.keys(subGroups);
            const hasMultipleGroups = subGroupNames.length > 1;

            // When searching, find which groups have matching registers
            const matchingSubGroups = subGroupNames.filter((grpName) => {
              const grpRegs = subGroups[grpName];
              return grpRegs.some(filterRegister);
            });

            if (filterQuery && matchingSubGroups.length === 0) {
              return (
                <div key={devName} style={{ padding: '1.5rem', textAlign: 'center', color: '#8b949e', fontSize: '0.75rem' }}>
                  No registers matching "{searchQuery}" in {devName}.
                </div>
              );
            }

            return (
              <div key={devName} style={{ 
                marginBottom: '0.75rem', 
                border: ghostRegs.length > 0 ? '1px solid #da3633' : '1px solid #30363d', 
                borderRadius: '6px', 
                overflow: 'hidden', 
                background: '#161b22' 
              }}>
                {/* Device Level Header */}
                <div 
                  onClick={() => toggleExpand(devName)}
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    justifyContent: 'space-between',
                    padding: '7px 12px',
                    background: ghostRegs.length > 0 ? 'rgba(218, 54, 51, 0.12)' : '#21262d',
                    cursor: 'pointer',
                    userSelect: 'none',
                    borderBottom: expanded ? (ghostRegs.length > 0 ? '1px solid #da3633' : '1px solid #30363d') : 'none'
                  }}
                >
                  <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
                    {expanded ? <ChevronDown size={14} style={{ color: '#8b949e' }} /> : <ChevronRight size={14} style={{ color: '#8b949e' }} />}
                    <span style={{ fontSize: '0.75rem', fontWeight: 600, color: '#e6edf3', fontFamily: 'monospace' }}>
                      {devName}
                    </span>
                    {ghostRegs.length > 0 && (
                      <span style={{
                        fontSize: '0.62rem',
                        backgroundColor: 'rgba(218, 54, 51, 0.25)',
                        border: '1px solid #da3633',
                        color: '#ff7b72',
                        padding: '1px 6px',
                        borderRadius: '10px',
                        fontWeight: 'bold',
                        display: 'flex',
                        alignItems: 'center',
                        gap: '4px'
                      }}>
                        <AlertTriangle size={11} /> {ghostRegs.length} UNDECLARED IN DTS
                      </span>
                    )}
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

                {/* Device Table / Subgroups Content */}
                {expanded && (
                  <div style={{ padding: '6px', background: '#0d1117' }}>
                    {!hasMultipleGroups ? (
                      renderRegisterTable(devRegs.filter(filterRegister))
                    ) : (
                      <div style={{
                        display: 'grid',
                        gridTemplateColumns: layoutMode === 'list' 
                          ? '1fr' 
                          : 'repeat(auto-fill, minmax(min(350px, 100%), 1fr))',
                        gap: '8px',
                        alignItems: 'start'
                      }}>
                        {(filterQuery ? matchingSubGroups : subGroupNames).map((grpName) => {
                          const groupKey = `${devName}_${grpName}`;
                          const grpRegs = subGroups[grpName];
                          const filteredGrpRegs = filterQuery ? grpRegs.filter(filterRegister) : grpRegs;
                          const grpGhostRegs = grpRegs.filter(r => r.isUndeclared);
                          const grpExpanded = isGroupExpanded(groupKey);

                          const allTraced = grpRegs.length > 0 && grpRegs.every(r => !hiddenTraceKeys[`${r.deviceName}_${r.name}`]);

                          const toggleGroupTraces = (e) => {
                            e.stopPropagation();
                            if (setTraceKeysVisibility) {
                              const updates = {};
                              grpRegs.forEach(r => {
                                updates[`${r.deviceName}_${r.name}`] = allTraced;
                              });
                              setTraceKeysVisibility(updates);
                            }
                          };

                          return (
                            <div key={groupKey} style={{
                              border: grpGhostRegs.length > 0 ? '1px solid #da3633' : '1px solid #21262d',
                              borderRadius: '5px',
                              overflow: 'hidden',
                              background: '#161b22',
                              display: 'flex',
                              flexDirection: 'column'
                            }}>
                              {/* Subgroup Header */}
                              <div
                                onClick={() => toggleGroupExpand(groupKey)}
                                style={{
                                  display: 'flex',
                                  alignItems: 'center',
                                  justifyContent: 'space-between',
                                  padding: '5px 8px',
                                  background: grpGhostRegs.length > 0 ? 'rgba(218, 54, 51, 0.16)' : '#1c2128',
                                  cursor: 'pointer',
                                  userSelect: 'none',
                                  borderBottom: grpExpanded ? '1px solid #21262d' : 'none'
                                }}
                              >
                                <div style={{ display: 'flex', alignItems: 'center', gap: '6px', overflow: 'hidden', minWidth: 0 }}>
                                  {grpExpanded ? <ChevronDown size={12} style={{ color: '#8b949e', flexShrink: 0 }} /> : <ChevronRight size={12} style={{ color: '#8b949e', flexShrink: 0 }} />}
                                  <Layers size={12} style={{ color: '#58a6ff', flexShrink: 0 }} />
                                  <span style={{ 
                                    fontSize: '0.72rem', 
                                    fontWeight: 600, 
                                    color: grpGhostRegs.length > 0 ? '#ff7b72' : '#c9d1d9',
                                    whiteSpace: 'nowrap',
                                    overflow: 'hidden',
                                    textOverflow: 'ellipsis'
                                  }} title={grpName}>
                                    {grpName}
                                  </span>
                                  {grpGhostRegs.length > 0 && (
                                    <span style={{ fontSize: '0.6rem', color: '#ff7b72', flexShrink: 0 }}>
                                      ⚠️ {grpGhostRegs.length}
                                    </span>
                                  )}
                                </div>
                                <div style={{ display: 'flex', alignItems: 'center', gap: '6px', flexShrink: 0 }}>
                                  <button
                                    type="button"
                                    onClick={toggleGroupTraces}
                                    style={{
                                      fontSize: '0.6rem',
                                      background: allTraced ? 'rgba(63, 185, 80, 0.15)' : 'transparent',
                                      border: allTraced ? '1px solid #3fb950' : '1px solid #30363d',
                                      borderRadius: '4px',
                                      color: allTraced ? '#3fb950' : '#8b949e',
                                      padding: '1px 5px',
                                      cursor: 'pointer'
                                    }}
                                    title={allTraced ? "Untrace all in this group" : "Trace all in this group"}
                                  >
                                    {allTraced ? '✓ Traced' : 'Trace'}
                                  </button>
                                  <span style={{
                                    fontSize: '0.62rem',
                                    color: '#8b949e',
                                    background: '#0d1117',
                                    padding: '1px 5px',
                                    borderRadius: '8px'
                                  }}>
                                    {filteredGrpRegs.length !== grpRegs.length 
                                      ? `${filteredGrpRegs.length}/${grpRegs.length}` 
                                      : grpRegs.length}
                                  </span>
                                </div>
                              </div>

                              {/* Subgroup Table */}
                              {grpExpanded && renderRegisterTable(filteredGrpRegs)}
                            </div>
                          );
                        })}
                      </div>
                    )}
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
