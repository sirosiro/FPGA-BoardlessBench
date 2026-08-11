import { ToggleRight } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function GpioPanel() {
  const { gpioDevices, registers, manifest, handleGpioToggle } = useDashboard();
  const isGpioDev = (d) => {
    if (!d) return false;
    if (d.type === 'gpio') return true;
    const name = (d.name || '').toLowerCase();
    const compat = (d.compatible || '').toLowerCase();
    if (name.includes('memory') || name.includes('bram') || name.includes('dma') || compat.includes('bram') || compat.includes('dma')) {
      return false;
    }
    return (
      compat.includes('gpio') || 
      compat.includes('matrix') || 
      compat.includes('hub75') || 
      name.includes('gpio') || 
      name.includes('pin') || 
      name.includes('matrix') || 
      name.includes('hub75')
    );
  };

  const rawDevs = gpioDevices.length > 0 ? gpioDevices : (manifest?.devices || []);
  const allGpioDevs = rawDevs.filter(isGpioDev);

  const HUB75_PIN_LABELS = ['R1', 'G1', 'B1', 'R2', 'G2', 'B2', 'CLK', 'LAT', 'OE', 'A', 'B', 'C', 'D', 'E', 'GND', 'NC'];

  return (
    <div className="gpio-pane" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      <div className="panel-header"><ToggleRight size={16} /> GPIO / Pin Array</div>
      <div className="gpio-viewport" style={{ flex: 1, overflowY: 'auto', padding: '1rem' }}>
        {allGpioDevs.length === 0 ? (
          <div style={{ padding: '1rem', color: '#8b949e', fontSize: '0.85rem' }}>No GPIO / Pin Array devices configured.</div>
        ) : (
          allGpioDevs.map((dev, i) => {
            const devRegs = registers.filter(r => r.deviceName === dev.name);
            const isHub75 = dev.compatible?.includes('hub75') || dev.name?.includes('matrix');

            const dirReg = devRegs.find(r => 
              Boolean(r.direction_mode) || 
              (r.name || '').toUpperCase().includes('TRI') || 
              (r.logical_name || '').toUpperCase().includes('TRI')
            );

            const dataRegs = devRegs.filter(r => (r.logical_name || r.name).toUpperCase().includes('DATA') || r.name.toUpperCase().includes('DR'));
            let dataOutReg = dataRegs[0] || devRegs[0];
            let dataInReg = dataRegs.find(r => r.name.toUpperCase().includes('IN')) || dataOutReg;

            const dirVal = dirReg?.decimal || 0;
            const dataOutVal = dataOutReg?.decimal || 0;
            const dataInVal = dataInReg?.decimal || 0;

            const labelName = isHub75 ? `${dev.name} (HUB75E Pins)` : dev.name;
            const totalPins = dev.pin_count || dev.pins || 16;

            return (
              <div key={`gpio-${i}`} className="gpio-dev-group" style={{ marginBottom: '1rem' }}>
                <div className="gpio-dev-label" style={{ fontWeight: 600, fontSize: '0.85rem', color: '#58a6ff', marginBottom: '0.5rem' }}>{labelName}</div>
                <div className="gpio-grid">
                  {Array.from({ length: totalPins }).map((_, bitIndex) => {
                    let isInput = false;
                    if (dirReg) {
                      const isActiveLow = dirReg.direction_mode === 'active_low_input' || (dirReg.logical_name || '').toUpperCase().includes('INV');
                      isInput = isActiveLow ? (dirVal & (1 << bitIndex)) === 0 : (dirVal & (1 << bitIndex)) !== 0;
                    } else {
                      isInput = !isHub75;
                    }

                    const isOn = isInput 
                      ? (dataInVal & (1 << bitIndex)) !== 0
                      : (dataOutVal & (1 << bitIndex)) !== 0 || isHub75;

                    const pinLabel = isHub75 ? (HUB75_PIN_LABELS[bitIndex] || `B${bitIndex}`) : `B${bitIndex}`;

                    return (
                      <div 
                        key={bitIndex} 
                        className={`gpio-bit ${isInput ? 'input' : 'output'} ${isOn ? 'on' : 'off'}`}
                        onClick={() => handleGpioToggle(dev.name, bitIndex, isOn, dataInReg?.name || 'PDIR')}
                        style={{ cursor: 'pointer' }}
                        title={`${labelName} ${pinLabel} (Bit ${bitIndex})`}
                      >
                        <div className="gpio-indicator" style={{ pointerEvents: 'none' }}></div>
                        <span className="gpio-label" style={{ pointerEvents: 'none' }}>{pinLabel}</span>
                      </div>
                    );
                  })}
                </div>
              </div>
            );
          })
        )}
      </div>
    </div>
  );
}

export default GpioPanel;
