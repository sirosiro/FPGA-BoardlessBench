import { useRef } from 'react';
import { Box } from 'lucide-react';
import { DockviewReact } from 'dockview-react';
import { DashboardProvider, useDashboard } from './components/DashboardContext';
import RegisterMonitor from './components/RegisterMonitor';
import GpioPanel from './components/GpioPanel';
import UartTerminal from './components/UartTerminal';
import RegisterTracer from './components/RegisterTracer';
import HdmiOutput from './components/HdmiOutput';
import './App.css';


// Components mapping for Dockview
const components = {
  registerMonitor: () => <RegisterMonitor />,
  gpioPanel: () => <GpioPanel />,
  registerTracer: () => <RegisterTracer />,
  uartTerminal: () => <UartTerminal />,
  hdmiOutput: () => <HdmiOutput />,
};


function DashboardInner() {
  const { connected, manifest } = useDashboard();
  const apiRef = useRef(null);

  // Initialize the default layout:
  // - Register Monitor, GPIO Panel, and Register Tracer in a vertical stack on the left (width 30%)
  // - UART Terminal on the right (width 70%)
  const onReady = (event) => {
    apiRef.current = event.api;
    initLayout(event.api);
  };

  const initLayout = (api) => {
    api.clear();

    // 1. Add uartTerminal on the right (takes whole space initially)
    const uartPanel = api.addPanel({
      id: 'uartTerminal',
      component: 'uartTerminal',
      title: 'UART Console',
    });

    // 2. Add registerMonitor to the left of uartTerminal
    const regPanel = api.addPanel({
      id: 'registerMonitor',
      component: 'registerMonitor',
      title: 'Registers',
      position: {
        referencePanel: 'uartTerminal',
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

    // 5. Add hdmiOutput below uartTerminal
    const hdmiPanel = api.addPanel({
      id: 'hdmiOutput',
      component: 'hdmiOutput',
      title: 'HDMI Output Preview',
      position: {
        referencePanel: 'uartTerminal',
        direction: 'below',
      },
    });

    // Programmatic adjustment of sizes to match default ratios
    // Set widths
    regPanel.api.setSize({ width: 400 });
    // Set heights on the left column panels
    regPanel.api.setSize({ height: 250 });
    tracerPanel.api.setSize({ height: 350 });
    uartPanel.api.setSize({ height: 400 });

    // Set panel constraints to prevent breaking layout
    regPanel.api.setConstraints({ minimumWidth: 200, minimumHeight: 100 });
    gpioPanel.api.setConstraints({ minimumWidth: 200, minimumHeight: 100 });
    tracerPanel.api.setConstraints({ minimumWidth: 200, minimumHeight: 100 });
    uartPanel.api.setConstraints({ minimumWidth: 300, minimumHeight: 150 });
    hdmiPanel.api.setConstraints({ minimumWidth: 300, minimumHeight: 150 });
  };


  const handleResetLayout = () => {
    if (apiRef.current) {
      initLayout(apiRef.current);
    }
  };

  return (
    <div className="dashboard-container" style={{ display: 'flex', flexDirection: 'column', height: '100vh', width: '100vw', overflow: 'hidden' }}>
      <header className="main-header" style={{ flex: '0 0 60px', padding: '0 2rem', background: '#161b22', borderBottom: '1px solid #30363d', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div className="brand" style={{ display: 'flex', alignItems: 'center' }}>
          <span className="logo-text" style={{ fontWeight: 800, fontSize: '1.2rem', color: '#58a6ff' }}>FPGA-BoardlessBench (F-BB)</span>
          <span className="version-tag" style={{ marginLeft: '0.5rem', fontSize: '0.7rem', color: '#8b949e', border: '1px solid #30363d', padding: '2px 6px', borderRadius: '4px' }}>v3.0 Premium</span>
        </div>
        <div className="system-meta" style={{ display: 'flex', alignItems: 'center', gap: '1.5rem' }}>
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
