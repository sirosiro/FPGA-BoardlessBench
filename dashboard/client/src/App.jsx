import { useRef, useState } from 'react';
import { Box } from 'lucide-react';
import { DockviewReact } from 'dockview-react';
import { DashboardProvider, useDashboard } from './components/DashboardContext';
import RegisterMonitor from './components/RegisterMonitor';
import GpioPanel from './components/GpioPanel';
import UartTerminal from './components/UartTerminal';
import RegisterTracer from './components/RegisterTracer';
import HdmiOutput from './components/HdmiOutput';
import SpiAdcPanel from './components/SpiAdcPanel';
import OledDisplay from './components/OledDisplay';
import SdCardPanel from './components/SdCardPanel';
import MemoryErrorModal from './components/MemoryErrorModal';
import './App.css';


// Components mapping for Dockview
const components = {
  registerMonitor: () => <RegisterMonitor />,
  gpioPanel: () => <GpioPanel />,
  registerTracer: () => <RegisterTracer />,
  uartTerminal: (props) => <UartTerminal {...props} />,
  hdmiOutput: () => <HdmiOutput />,
  spiAdcPanel: () => <SpiAdcPanel />,
  oledDisplay: () => <OledDisplay />,
  sdCard: () => <SdCardPanel />,
};


function DashboardInner() {
  const { connected, manifest } = useDashboard();
  const apiRef = useRef(null);
  const [saveStatus, setSaveStatus] = useState('Save Layout');

  // @intent:rationale マウント時にバックエンドから保存済みのレイアウト（fbb_layout.json）をフェッチし、存在する場合は Dockview API にロードして復元します。存在しない場合はデフォルトレイアウトを適用します。
  const onReady = async (event) => {
    const api = event.api;
    apiRef.current = api;

    try {
      const response = await fetch('/api/layout');
      if (response.ok) {
        const layoutData = await response.json();
        if (layoutData && Object.keys(layoutData).length > 0) {
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
      name: `vfpga_uart_${index + 1}`
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

    // 3b. Add spiAdcPanel within gpioPanel group (as a tab)
    api.addPanel({
      id: 'spiAdcPanel',
      component: 'spiAdcPanel',
      title: 'SPI ADC (12-bit)',
      position: {
        referencePanel: 'gpioPanel',
        direction: 'within',
      },
    });

    // 3c. Add oledDisplay within gpioPanel group (as a tab)
    api.addPanel({
      id: 'oledDisplay',
      component: 'oledDisplay',
      title: 'Virtual OLED',
      position: {
        referencePanel: 'gpioPanel',
        direction: 'within',
      },
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
    if (mainPanel?.api) {
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
        setSaveStatus('Failed');
        setTimeout(() => setSaveStatus('Save Layout'), 2000);
      }
    } catch (e) {
      console.error('[Dashboard] Failed to save layout:', e);
      setSaveStatus('Error');
      setTimeout(() => setSaveStatus('Save Layout'), 2000);
    }
  };

  // Wait for manifest to load before mounting the Dockview interface to avoid race conditions.
  if (!manifest) {
    return (
      <div style={{ display: 'flex', justifyContent: 'center', alignItems: 'center', height: '100vh', background: '#0d1117', color: '#c9d1d9' }}>
        <div style={{ textAlign: 'center', fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif' }}>
          <div style={{ fontSize: '1.5rem', marginBottom: '1rem', color: '#58a6ff', fontWeight: 800 }}>FPGA-BoardlessBench (F-BB)</div>
          <div>Loading board manifest...</div>
        </div>
      </div>
    );
  }

  return (
    <div className="dashboard-container" style={{ display: 'flex', flexDirection: 'column', height: '100vh', width: '100vw', overflow: 'hidden' }}>
      <header className="main-header" style={{ flex: '0 0 60px', padding: '0 2rem', background: '#161b22', borderBottom: '1px solid #30363d', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div className="brand" style={{ display: 'flex', alignItems: 'center' }}>
          <span className="logo-text" style={{ fontWeight: 800, fontSize: '1.2rem', color: '#58a6ff' }}>FPGA-BoardlessBench (F-BB)</span>
          <span className="version-tag" style={{ marginLeft: '0.5rem', fontSize: '0.7rem', color: '#8b949e', border: '1px solid #30363d', padding: '2px 6px', borderRadius: '4px' }}>v3.0 Premium</span>
        </div>
        <div className="system-meta" style={{ display: 'flex', alignItems: 'center', gap: '1.5rem' }}>
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
