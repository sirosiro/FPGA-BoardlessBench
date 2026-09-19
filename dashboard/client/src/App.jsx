import { useRef, useState, useEffect } from 'react';
import { Box, Plus, ChevronDown } from 'lucide-react';
import { DockviewReact } from 'dockview-react';
import { DashboardProvider, useDashboard } from './components/DashboardContext';
import MemoryErrorModal from './components/MemoryErrorModal';
import PopoutWindow from './components/PopoutWindow';
import DockHeaderActions from './components/DockHeaderActions';
import {
  getDockviewComponentsMap,
  getGroupedPanesForMenu,
  getPaneDefinition
} from './panes/paneRegistry';
import './App.css';

// Dynamic Dockview components mapping from DPPA registry
const components = getDockviewComponentsMap();

function DashboardInner() {
  const { connected, manifest } = useDashboard();
  const apiRef = useRef(null);
  const [saveStatus, setSaveStatus] = useState('Save Layout');
  const [isAddPaneOpen, setIsAddPaneOpen] = useState(false);
  const [poppedOutPanels, setPoppedOutPanels] = useState([]);
  const dropdownRef = useRef(null);

  // URL query parameter resolution
  const searchParams = typeof window !== 'undefined' ? new URLSearchParams(window.location.search) : new URLSearchParams();
  const paneParam = searchParams.get('pane');
  const screenParam = searchParams.get('screen') || 'main';

  // Close dropdown on outside click
  useEffect(() => {
    const handleClickOutside = (event) => {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target)) {
        setIsAddPaneOpen(false);
      }
    };
    document.addEventListener('mousedown', handleClickOutside);
    return () => document.removeEventListener('mousedown', handleClickOutside);
  }, []);

  // Multi-Screen: Popout event listener (triggered by DockHeaderActions or custom events)
  useEffect(() => {
    const handlePopoutEvent = (e) => {
      const panelInfo = e.detail;
      if (!panelInfo || !apiRef.current) return;

      const panel = apiRef.current.getPanel(panelInfo.id);
      if (panel) {
        apiRef.current.removePanel(panel);
      }
      setPoppedOutPanels(prev => [
        ...prev.filter(p => p.id !== panelInfo.id),
        panelInfo
      ]);
    };

    window.addEventListener('fbb:popout', handlePopoutEvent);
    return () => window.removeEventListener('fbb:popout', handlePopoutEvent);
  }, []);

  // Handler to re-dock a popped-out panel back into main Dockview
  const handleReDock = (panelInfo) => {
    setPoppedOutPanels(prev => prev.filter(p => p.id !== panelInfo.id));
    if (apiRef.current) {
      const existing = apiRef.current.getPanel(panelInfo.id);
      if (!existing) {
        apiRef.current.addPanel({
          id: panelInfo.id,
          component: panelInfo.component || panelInfo.id.split('_')[0],
          title: panelInfo.title,
          params: panelInfo.params || {}
        });
      }
    }
  };

  // @intent:rationale マウント時にバックエンドから保存済みのレイアウト（?screen=<id> 指定時は fbb_layout_${screen}.json、未指定時は fbb_layout.json）をフェッチし、存在する場合は Dockview API にロードして復元します。
  const onReady = async (event) => {
    const api = event.api;
    apiRef.current = api;

    const layoutUrl = screenParam && screenParam !== 'main' && screenParam !== 'default'
      ? `/api/layout?screen=${encodeURIComponent(screenParam)}`
      : '/api/layout';

    try {
      const response = await fetch(layoutUrl);
      if (response.ok) {
        const layoutData = await response.json();
        if (layoutData && Object.keys(layoutData).length > 0) {
          // Remap stale UART device names in saved layout to actual manifest UARTs
          const validUartNames = (manifest?.uarts || []).map(u => u.name);
          if (layoutData.panels) {
            Object.values(layoutData.panels).forEach(panel => {
              if (panel.contentComponent === 'uartTerminal' && panel.params?.deviceName) {
                if (validUartNames.length > 0 && !validUartNames.includes(panel.params.deviceName)) {
                  panel.params.deviceName = validUartNames[0];
                  panel.title = `UART: ${validUartNames[0]}`;
                }
              }
              if (panel.contentComponent === 'oledDisplay' || panel.contentComponent === 'seg7Display') {
                panel.contentComponent = 'genericPeripheralPane';
              }
            });
          }
          api.fromJSON(layoutData);
          return;
        }
      }
    } catch (e) {
      console.warn(`[Dashboard] No saved layout found for screen '${screenParam}', using default layout.`, e);
    }

    initLayout(api);
  };

  const initLayout = (api) => {
    api.clear();
    const rawUarts = manifest?.uarts || [];
    const uarts = rawUarts.map((uart, index) => ({
      ...uart,
      name: uart.name || `vfpga_uart_${index + 1}`
    }));

    // 1. Create a separate panel for each UART device
    let mainUartPanelId = 'uartTerminal_default';
    if (uarts.length > 0) {
      mainUartPanelId = `uartTerminal_${uarts[0].name}`;
      uarts.forEach((uart, index) => {
        const panelId = `uartTerminal_${uart.name}`;
        const isFirst = index === 0;

        api.addPanel({
          id: panelId,
          component: 'uartTerminal',
          title: `UART: ${uart.name}`,
          params: { deviceName: uart.name, _componentName: 'uartTerminal' },
          ...(isFirst ? {} : {
            position: {
              referencePanel: mainUartPanelId,
              direction: 'within'
            }
          })
        });
      });
    } else {
      api.addPanel({
        id: 'uartTerminal_default',
        component: 'uartTerminal',
        title: 'UART Console',
        params: { deviceName: 'default', _componentName: 'uartTerminal' }
      });
    }

    const referenceId = uarts.length > 0 ? `uartTerminal_${uarts[0].name}` : 'uartTerminal_default';

    // 2. Add registerMonitor to the left of the main UART panel
    const regPanel = api.addPanel({
      id: 'registerMonitor',
      component: 'registerMonitor',
      title: 'Registers',
      params: { _componentName: 'registerMonitor' },
      position: {
        referencePanel: referenceId,
        direction: 'left',
      },
    });

    // 3. Add gpioPanel below registerMonitor
    const gpioPanel = api.addPanel({
      id: 'gpioPanel',
      component: 'gpioPanel',
      title: 'GPIO / Pin Array',
      params: { _componentName: 'gpioPanel' },
      position: {
        referencePanel: 'registerMonitor',
        direction: 'below',
      },
    });

    // 3c. Add generic peripheral panes for all PPA slaves
    const i2cSlaves = manifest?.devices?.flatMap(d => d.i2c_slaves || []) || [];
    const spiSlavesInit = manifest?.devices?.flatMap(d => d.spi_slaves || []) || [];
    const directDevicesInit = manifest?.devices?.filter(d => d.ui_widget || d.compatible?.includes('hub75')) || [];
    const allSlavesInit = [...i2cSlaves, ...spiSlavesInit, ...directDevicesInit];
    allSlavesInit.forEach((s, idx) => {
      const pTitle = s.ui_widget?.title || s.name || 'Generic Peripheral';
      const pId = `generic_peripheral_${s.name || idx}_${idx}`;
      api.addPanel({
        id: pId,
        component: 'genericPeripheralPane',
        title: pTitle,
        params: {
          pluginId: s.compatible,
          manifest: s,
          _componentName: 'genericPeripheralPane'
        },
        position: {
          referencePanel: 'gpioPanel',
          direction: 'within',
        },
      });
    });

    // 3d. Add sdCard within gpioPanel group (as a tab)
    api.addPanel({
      id: 'sdCard',
      component: 'sdCard',
      title: 'Virtual SD Card',
      params: { _componentName: 'sdCard' },
      position: {
        referencePanel: 'gpioPanel',
        direction: 'within',
      },
    });

    // 3e. Add dtsVisualizer within gpioPanel group (as a tab)
    api.addPanel({
      id: 'dtsVisualizer',
      component: 'dtsVisualizer',
      title: 'DTS Visualizer & AI',
      params: { _componentName: 'dtsVisualizer' },
      position: {
        referencePanel: 'gpioPanel',
        direction: 'within',
      },
    });

    // 4. Add registerTracer below gpioPanel
    const tracerPanel = api.addPanel({
      id: 'registerTracer',
      component: 'registerTracer',
      title: 'Tracer',
      params: { _componentName: 'registerTracer' },
      position: {
        referencePanel: 'gpioPanel',
        direction: 'below',
      },
    });

    // 5. Add hdmiOutput below the main UART panel
    const hdmiPanel = api.addPanel({
      id: 'hdmiOutput',
      component: 'hdmiOutput',
      title: 'HDMI Output Preview',
      params: { _componentName: 'hdmiOutput' },
      position: {
        referencePanel: referenceId,
        direction: 'below',
      },
    });

    // Programmatic adjustment of sizes to match default ratios
    if (regPanel?.api) {
      regPanel.api.setSize({ width: 400 });
      regPanel.api.setSize({ height: 250 });
      regPanel.api.setConstraints({ minimumWidth: 200, minimumHeight: 100 });
    }
    if (gpioPanel?.api) {
      gpioPanel.api.setConstraints({ minimumWidth: 200, minimumHeight: 100 });
    }
    if (tracerPanel?.api) {
      tracerPanel.api.setSize({ height: 350 });
      tracerPanel.api.setConstraints({ minimumWidth: 200, minimumHeight: 100 });
    }
    
    const mainPanel = api.getPanel(referenceId);
    if (mainPanel?.api && tracerPanel?.api) {
      api.setGroupRatio(mainPanel.api.group, 0.6);
      mainPanel.api.setSize({ height: 400 });
      mainPanel.api.setConstraints({ minimumWidth: 300, minimumHeight: 150 });
    }
    
    if (hdmiPanel?.api) {
      hdmiPanel.api.setConstraints({ minimumWidth: 300, minimumHeight: 150 });
    }
  };

  const handleResetLayout = () => {
    if (apiRef.current) {
      initLayout(apiRef.current);
    }
  };

  // 単体ペインの追加・アクティブ化ハンドラー
  const handleAddPane = (id, component, title, params = {}) => {
    setIsAddPaneOpen(false);
    if (!apiRef.current) return;
    const existing = apiRef.current.getPanel(id);
    if (existing) {
      existing.api.setActive();
    } else {
      apiRef.current.addPanel({
        id,
        component,
        title,
        params: { ...params, _componentName: component },
      });
    }
  };

  // @intent:rationale 現在の Dockview のペイン配置情報をシリアライズし、バックエンド経由で保存します。?screen=<id> 指定時は画面固有のファイルへ保存されます。
  const handleSaveLayout = async () => {
    if (!apiRef.current) return;
    const layoutData = apiRef.current.toJSON();
    const layoutUrl = screenParam && screenParam !== 'main' && screenParam !== 'default'
      ? `/api/layout?screen=${encodeURIComponent(screenParam)}`
      : '/api/layout';

    try {
      const response = await fetch(layoutUrl, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
        },
        body: JSON.stringify(layoutData),
      });

      if (response.ok) {
        setSaveStatus('Saved!');
        setTimeout(() => setSaveStatus('Save Layout'), 2000);
      } else {
        setSaveStatus('Save Failed');
        setTimeout(() => setSaveStatus('Save Layout'), 2000);
      }
    } catch (e) {
      console.error('[Dashboard] Failed to save layout:', e);
      setSaveStatus('Save Failed');
      setTimeout(() => setSaveStatus('Save Layout'), 2000);
    }
  };

  // Standalone Pane View (triggered by ?pane=<id>)
  if (paneParam) {
    const paneDef = getPaneDefinition(paneParam);
    if (paneDef) {
      const Component = paneDef.component;
      const params = Object.fromEntries(searchParams.entries());
      return (
        <div style={{ width: '100vw', height: '100vh', background: '#0d1117', color: '#c9d1d9', overflow: 'hidden', display: 'flex', flexDirection: 'column' }}>
          <header style={{ height: '36px', background: '#161b22', borderBottom: '1px solid #30363d', display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 12px', fontSize: '0.8rem', userSelect: 'none' }}>
            <span style={{ fontWeight: 600, color: '#58a6ff' }}>F-BB Standalone: {paneDef.title}</span>
            <a href={window.location.pathname} style={{ color: '#8b949e', textDecoration: 'none', fontSize: '0.75rem' }}>&larr; Back to Full Dashboard</a>
          </header>
          <div style={{ flex: 1, overflow: 'hidden', position: 'relative' }}>
            <Component params={params} />
          </div>
          <MemoryErrorModal />
        </div>
      );
    }
  }

  // Dynamically resolve grouped pane items from DPPA registry
  const standardPaneCategories = getGroupedPanesForMenu(manifest);

  return (
    <div className="dashboard-container" style={{ display: 'flex', flexDirection: 'column', height: '100vh', width: '100vw', overflow: 'hidden' }}>
      <header className="main-header" style={{ flex: '0 0 60px', padding: '0 2rem', background: '#161b22', borderBottom: '1px solid #30363d', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div className="brand" style={{ display: 'flex', alignItems: 'center' }}>
          <span className="logo-text" style={{ fontWeight: 800, fontSize: '1.2rem', color: '#58a6ff' }}>FPGA-BoardlessBench (F-BB)</span>
          <span className="version-tag" style={{ marginLeft: '0.5rem', fontSize: '0.7rem', color: '#8b949e', border: '1px solid #30363d', padding: '2px 6px', borderRadius: '4px' }}>v3.0 Premium</span>
          {screenParam && screenParam !== 'main' && screenParam !== 'default' && (
            <span
              className="screen-tag"
              style={{
                marginLeft: '0.5rem',
                fontSize: '0.72rem',
                fontWeight: 700,
                color: '#58a6ff',
                backgroundColor: 'rgba(56, 139, 253, 0.15)',
                border: '1px solid rgba(56, 139, 253, 0.4)',
                padding: '2px 8px',
                borderRadius: '4px',
                letterSpacing: '0.5px'
              }}
            >
              SCREEN: {screenParam.toUpperCase()}
            </span>
          )}
        </div>
        <div className="system-meta" style={{ display: 'flex', alignItems: 'center', gap: '1.2rem' }}>

          {/* Add Pane Dropdown Menu (Registry Driven) */}
          <div ref={dropdownRef} style={{ position: 'relative' }}>
            <button
              className="add-pane-btn"
              onClick={() => setIsAddPaneOpen(!isAddPaneOpen)}
              style={{
                backgroundColor: '#1f6feb',
                color: '#ffffff',
                border: '1px solid #388bfd',
                padding: '6px 12px',
                fontSize: '0.75rem',
                borderRadius: '6px',
                cursor: 'pointer',
                fontWeight: '600',
                display: 'flex',
                alignItems: 'center',
                gap: '6px',
                transition: 'background 0.2s'
              }}
              onMouseEnter={(e) => e.currentTarget.style.backgroundColor = '#388bfd'}
              onMouseLeave={(e) => e.currentTarget.style.backgroundColor = '#1f6feb'}
            >
              <Plus size={14} /> Add Pane <ChevronDown size={12} style={{ transform: isAddPaneOpen ? 'rotate(180deg)' : 'none', transition: 'transform 0.2s' }} />
            </button>

            {isAddPaneOpen && (
              <div 
                className="add-pane-dropdown"
                style={{
                  position: 'absolute',
                  top: 'calc(100% + 6px)',
                  right: 0,
                  width: '260px',
                  background: '#161b22',
                  border: '1px solid #30363d',
                  borderRadius: '8px',
                  boxShadow: '0 8px 24px rgba(0,0,0,0.6)',
                  padding: '8px 0',
                  zIndex: 1000,
                  userSelect: 'none',
                  maxHeight: '80vh',
                  overflowY: 'auto'
                }}
              >
                {standardPaneCategories.map((cat, idx) => (
                  <div key={idx} style={{ marginBottom: '6px' }}>
                    <div style={{ padding: '4px 12px', fontSize: '0.68rem', fontWeight: 700, color: '#8b949e', textTransform: 'uppercase', letterSpacing: '0.5px' }}>
                      {cat.category}
                    </div>
                    {cat.items.map(item => {
                      const ItemIcon = item.icon || Box;
                      return (
                        <div
                          key={item.id}
                          onClick={() => handleAddPane(item.id, item.component, item.title, item.params || {})}
                          style={{
                            padding: '6px 14px',
                            fontSize: '0.8rem',
                            color: '#c9d1d9',
                            display: 'flex',
                            alignItems: 'center',
                            gap: '10px',
                            cursor: 'pointer',
                            transition: 'background 0.15s, color 0.15s'
                          }}
                          onMouseEnter={(e) => {
                            e.currentTarget.style.backgroundColor = '#21262d';
                            e.currentTarget.style.color = '#58a6ff';
                          }}
                          onMouseLeave={(e) => {
                            e.currentTarget.style.backgroundColor = 'transparent';
                            e.currentTarget.style.color = '#c9d1d9';
                          }}
                        >
                          <ItemIcon size={14} style={{ color: '#8b949e' }} />
                          {item.title}
                        </div>
                      );
                    })}
                  </div>
                ))}
              </div>
            )}
          </div>

          <button
            className="save-layout-btn"
            onClick={handleSaveLayout}
            style={{
              backgroundColor: '#238636',
              color: '#ffffff',
              border: '1px solid #2ea44f',
              padding: '6px 12px',
              fontSize: '0.75rem',
              borderRadius: '6px',
              cursor: 'pointer',
              fontWeight: '600',
              transition: 'background 0.2s, color 0.2s'
            }}
            onMouseEnter={(e) => e.currentTarget.style.backgroundColor = '#2ea44f'}
            onMouseLeave={(e) => e.currentTarget.style.backgroundColor = '#238636'}
          >
            {saveStatus}
          </button>
          <button
            className="reset-layout-btn"
            onClick={handleResetLayout}
            style={{
              backgroundColor: '#21262d',
              color: '#c9d1d9',
              border: '1px solid #30363d',
              padding: '6px 12px',
              fontSize: '0.75rem',
              borderRadius: '6px',
              cursor: 'pointer',
              fontWeight: '600',
              transition: 'background 0.2s'
            }}
            onMouseEnter={(e) => e.currentTarget.style.backgroundColor = '#30363d'}
            onMouseLeave={(e) => e.currentTarget.style.backgroundColor = '#21262d'}
          >
            Reset Layout
          </button>
          <div className="meta-item" style={{ display: 'flex', alignItems: 'center', gap: '0.4rem', fontSize: '0.85rem', color: '#c9d1d9' }}>
            <Box size={14} /> {manifest?.model || manifest?.board || 'Loading...'}
          </div>
          <div className={`conn-status ${connected ? 'online' : 'offline'}`} style={{ fontSize: '0.85rem', fontWeight: 600 }}>
            {connected ? '● LIVE' : '○ DISCONNECTED'}
          </div>
        </div>
      </header>

      <main className="content-layout dockview-theme-dark" style={{ flex: 1, overflow: 'hidden', position: 'relative' }}>
        <DockviewReact
          components={components}
          rightHeaderActionsComponent={DockHeaderActions}
          onReady={onReady}
        />
      </main>

      {/* Multi-Screen: Popped out child windows via React Portal */}
      {poppedOutPanels.map(p => {
        const componentId = p.component || p.id.split('_')[0];
        const paneDef = getPaneDefinition(componentId);
        const Component = paneDef?.component;
        if (!Component) return null;
        return (
          <PopoutWindow
            key={p.id}
            title={p.title}
            win={p.win}
            onClose={() => handleReDock(p)}
          >
            <Component params={p.params} />
          </PopoutWindow>
        );
      })}

      <MemoryErrorModal />
    </div>
  );
}

function App() {
  return (
    <DashboardProvider>
      <DashboardInner />
    </DashboardProvider>
  );
}

export default App;
