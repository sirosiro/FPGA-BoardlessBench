import { useRef, useState, useEffect } from 'react';
import { Box, Plus, ChevronDown, Monitor, Cpu, Activity, Terminal, Tv, HardDrive, Layers, FileCode } from 'lucide-react';
import { DockviewReact } from 'dockview-react';
import { DashboardProvider, useDashboard } from './components/DashboardContext';
import RegisterMonitor from './components/RegisterMonitor';
import GpioPanel from './components/GpioPanel';
import UartTerminal from './components/UartTerminal';
import RegisterTracer from './components/RegisterTracer';
import HdmiOutput from './components/HdmiOutput';
import SdCardPanel from './components/SdCardPanel';
import DtsVisualizer from './components/DTSVisualizer';
import GenericPeripheralPane from './components/GenericPeripheralPane';
import MemoryErrorModal from './components/MemoryErrorModal';
import './App.css';


// Components mapping for Dockview
const components = {
  registerMonitor: (props) => <RegisterMonitor {...props} />,
  gpioPanel: (props) => <GpioPanel {...props} />,
  registerTracer: (props) => <RegisterTracer {...props} />,
  uartTerminal: (props) => <UartTerminal {...props} />,
  hdmiOutput: (props) => <HdmiOutput {...props} />,
  spiAdcPanel: (props) => <GenericPeripheralPane {...props} />,
  oledDisplay: (props) => <GenericPeripheralPane {...props} />,
  seg7Display: (props) => <GenericPeripheralPane {...props} />,
  sdCard: (props) => <SdCardPanel {...props} />,
  dtsVisualizer: (props) => <DtsVisualizer {...props} />,
  genericPeripheralPane: (props) => <GenericPeripheralPane {...props} />,
};


function DashboardInner() {
  const { connected, manifest } = useDashboard();
  const apiRef = useRef(null);
  const [saveStatus, setSaveStatus] = useState('Save Layout');
  const [isAddPaneOpen, setIsAddPaneOpen] = useState(false);
  const dropdownRef = useRef(null);

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

  // @intent:rationale マウント時にバックエンドから保存済みのレイアウト（fbb_layout.json）をフェッチし、存在する場合は Dockview API にロードして復元します。存在しない場合はデフォルトレイアウトを適用します。
  const onReady = async (event) => {
    const api = event.api;
    apiRef.current = api;

    try {
      const response = await fetch('/api/layout');
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
      console.warn('[Dashboard] No saved layout found or failed to load, using default layout.', e);
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
          params: { deviceName: uart.name },
          ...(isFirst ? {} : {
            position: {
              referencePanel: mainUartPanelId,
              direction: 'within'
            }
          })
        });
      });
    } else {
      // Fallback if no UARTs are defined yet
      api.addPanel({
        id: 'uartTerminal_default',
        component: 'uartTerminal',
        title: 'UART Console',
        params: { deviceName: 'default' }
      });
    }

    const referenceId = uarts.length > 0 ? `uartTerminal_${uarts[0].name}` : 'uartTerminal_default';

    // 2. Add registerMonitor to the left of the main UART panel
    const regPanel = api.addPanel({
      id: 'registerMonitor',
      component: 'registerMonitor',
      title: 'Registers',
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
          manifest: s
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
      position: {
        referencePanel: referenceId,
        direction: 'below',
      },
    });

    // Programmatic adjustment of sizes to match default ratios with safety checks
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
    
    // Programmatic layout update via API lookup instead of object handles
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
        params,
      });
    }
  };

  // @intent:rationale 現在の Dockview のペイン配置情報をシリアライズし、バックエンド経由でアクティブなシナリオフォルダ配下の fbb_layout.json に保存します。保存結果はボタン表記を通じて非侵襲的に通知されます。
  const handleSaveLayout = async () => {
    if (!apiRef.current) return;
    const layoutData = apiRef.current.toJSON();
    try {
      const response = await fetch('/api/layout', {
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

  // DTSに定義されているアドイン・ペリフェラルの動的抽出
  const i2cSlaves = manifest?.devices?.flatMap(d => d.i2c_slaves || []) || [];
  const spiSlaves = manifest?.devices?.flatMap(d => d.spi_slaves || []) || [];
  const directDevices = manifest?.devices?.filter(d => d.ui_widget || d.compatible?.includes('hub75')) || [];
  const allSlaves = [...i2cSlaves, ...spiSlaves, ...directDevices];

  const hasOled = i2cSlaves.some(s => s.compatible?.includes('ssd1306'));
  const hasSeg7 = i2cSlaves.some(s => s.compatible?.includes('ht16k33'));
  const hasSpiAdc = spiSlaves.some(s => s.compatible?.includes('mcp3208'));
  const hasSdCard = Boolean(manifest?.sd_card_path) || manifest?.devices?.some(d => d.name?.includes('sd') || d.compatible?.includes('sd'));
  const hasHdmi = Boolean(manifest?.hdmi_output_path) || manifest?.devices?.some(d => d.name?.includes('hdmi') || d.compatible?.includes('hdmi'));

  const peripheralItems = [];

  if (hasOled) {
    const oledTitle = i2cSlaves.find(s => s.compatible?.includes('ssd1306'))?.ui_widget?.title || 'SSD1306 OLED Display (128x64)';
    peripheralItems.push({
      id: 'oledDisplay',
      component: 'oledDisplay',
      title: oledTitle,
      icon: Monitor,
      params: { type: 'oled' }
    });
  }

  if (hasSeg7) {
    const seg7Title = i2cSlaves.find(s => s.compatible?.includes('ht16k33'))?.ui_widget?.title || 'Adafruit 4-Digit 7-Segment LED (Red)';
    peripheralItems.push({
      id: 'seg7Display',
      component: 'oledDisplay',
      title: seg7Title,
      icon: Monitor,
      params: { type: 'seg7', pluginId: 'adafruit_ht16k33' }
    });
  }

  if (hasSpiAdc) {
    peripheralItems.push({
      id: 'spiAdcPanel',
      component: 'spiAdcPanel',
      title: 'SPI ADC (12-bit)',
      icon: Activity
    });
  }

  // Dynamic PPA 3.0/4.0 custom peripheral items for dropdown menu
  allSlaves.forEach((s, idx) => {
    const isLegacyOled = s.compatible?.includes('ssd1306');
    const isLegacySeg7 = s.compatible?.includes('ht16k33');
    const isLegacySpiAdc = s.compatible?.includes('mcp3208');
    if (!isLegacyOled && !isLegacySeg7 && !isLegacySpiAdc) {
      const pTitle = s.ui_widget?.title || s.name || 'Generic Peripheral';
      const pId = `generic_peripheral_${s.name || idx}_${idx}`;
      peripheralItems.push({
        id: pId,
        component: 'genericPeripheralPane',
        title: pTitle,
        icon: Monitor,
        params: { pluginId: s.compatible, manifest: s }
      });
    }
  });

  if (hasSdCard) {
    peripheralItems.push({
      id: 'sdCard',
      component: 'sdCard',
      title: 'Virtual SD Card',
      icon: HardDrive
    });
  }

  if (hasHdmi) {
    peripheralItems.push({
      id: 'hdmiOutput',
      component: 'hdmiOutput',
      title: 'HDMI Output Preview',
      icon: Tv
    });
  }

  // もしDTSにディスプレイ/センサー等のアドインペリフェラルが含まれない場合、
  // デフォルトの「Virtual Peripheral View」(スタンドバイ画面) を1つだけ表示
  if (peripheralItems.length === 0) {
    peripheralItems.push({
      id: 'oledDisplay',
      component: 'oledDisplay',
      title: 'Virtual Peripheral View',
      icon: Monitor,
      params: { type: 'standby' }
    });
  }

  // 利用可能な標準ペインの一覧
  const standardPaneCategories = [
    {
      category: 'Observability & Control',
      items: [
        { id: 'registerMonitor', component: 'registerMonitor', title: 'Registers', icon: Cpu },
        { id: 'gpioPanel', component: 'gpioPanel', title: 'GPIO / Pin Array', icon: Activity },
        { id: 'registerTracer', component: 'registerTracer', title: 'Tracer', icon: Layers },
        { id: 'dtsVisualizer', component: 'dtsVisualizer', title: 'DTS Visualizer & AI', icon: FileCode },
      ]
    },
    {
      category: 'Peripherals & I/O',
      items: peripheralItems
    }
  ];

  // UART Consoles (動的生成)
  const rawUarts = manifest?.uarts || [];
  const uartItems = rawUarts.length > 0
    ? rawUarts.map(u => ({
        id: `uartTerminal_${u.name}`,
        component: 'uartTerminal',
        title: `UART: ${u.name}`,
        params: { deviceName: u.name },
        icon: Terminal
      }))
    : [{ id: 'uartTerminal_default', component: 'uartTerminal', title: 'UART Console', params: { deviceName: 'default' }, icon: Terminal }];

  return (
    <div className="dashboard-container" style={{ display: 'flex', flexDirection: 'column', height: '100vh', width: '100vw', overflow: 'hidden' }}>
      <header className="main-header" style={{ flex: '0 0 60px', padding: '0 2rem', background: '#161b22', borderBottom: '1px solid #30363d', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div className="brand" style={{ display: 'flex', alignItems: 'center' }}>
          <span className="logo-text" style={{ fontWeight: 800, fontSize: '1.2rem', color: '#58a6ff' }}>FPGA-BoardlessBench (F-BB)</span>
          <span className="version-tag" style={{ marginLeft: '0.5rem', fontSize: '0.7rem', color: '#8b949e', border: '1px solid #30363d', padding: '2px 6px', borderRadius: '4px' }}>v3.0 Premium</span>
        </div>
        <div className="system-meta" style={{ display: 'flex', alignItems: 'center', gap: '1.2rem' }}>

          {/* Add Pane Dropdown Menu */}
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
                  userSelect: 'none'
                }}
              >
                {standardPaneCategories.map((cat, idx) => (
                  <div key={idx} style={{ marginBottom: '6px' }}>
                    <div style={{ padding: '4px 12px', fontSize: '0.68rem', fontWeight: 700, color: '#8b949e', textTransform: 'uppercase', letterSpacing: '0.5px' }}>
                      {cat.category}
                    </div>
                    {cat.items.map(item => {
                      const ItemIcon = item.icon;
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

                {/* UART Group */}
                <div style={{ borderTop: '1px solid #21262d', paddingTop: '6px', marginTop: '4px' }}>
                  <div style={{ padding: '4px 12px', fontSize: '0.68rem', fontWeight: 700, color: '#8b949e', textTransform: 'uppercase', letterSpacing: '0.5px' }}>
                    Serial Terminals
                  </div>
                  {uartItems.map(item => (
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
                      <Terminal size={14} style={{ color: '#8b949e' }} />
                      {item.title}
                    </div>
                  ))}
                </div>
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
          onReady={onReady}
        />
      </main>
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
