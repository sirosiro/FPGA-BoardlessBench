import { ToggleRight } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function GpioPanel() {
  const { gpioDevices, registers, handleGpioToggle } = useDashboard();

  return (
    <div className="gpio-pane" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      <div className="panel-header"><ToggleRight size={16} /> GPIO / Pin Array</div>
      <div className="gpio-viewport" style={{ flex: 1, overflowY: 'auto', padding: '1rem' }}>
        {gpioDevices.map((dev, i) => {
          const devRegs = registers.filter(r => r.deviceName === dev.name);
          if (devRegs.length === 0) return null;

          // Find direction register:
          // 1. Primary: Driven strictly by DTS/Manifest direction_mode metadata (Single Source of Truth)
          // 2. Fallback: Zynq PS / XPS GPIO standard ('TRI' / 'GPIO_TRI')
          const dirReg = devRegs.find(r => 
            Boolean(r.direction_mode) || 
            (r.name || '').toUpperCase().includes('TRI') || 
            (r.logical_name || '').toUpperCase().includes('TRI')
          );

          // Find DATA registers (Zynq / Standard DATA)
          const dataRegs = devRegs.filter(r => (r.logical_name || r.name).toUpperCase().includes('DATA') || r.name.toUpperCase().includes('DR'));
          let dataOutReg = dataRegs[0] || devRegs[0];
          let dataInReg = dataRegs.find(r => r.name.toUpperCase().includes('IN')) || dataOutReg;

          const dirVal = dirReg?.decimal || 0;
          const dataOutVal = dataOutReg?.decimal || 0;
          const dataInVal = dataInReg?.decimal || 0;

          const labelName = dev.name;
          const totalPins = dev.pin_count || dev.pins || 16; // Default to 16 pins for SoC like i.MX95

          return (
            <div key={`gpio-${i}`} className="gpio-dev-group">
              <div className="gpio-dev-label">{labelName}</div>
              <div className="gpio-grid">
                {Array.from({ length: totalPins }).map((_, bitIndex) => {
                  let isInput = false;
                  if (dirReg) {
                    const isActiveLow = dirReg.direction_mode === 'active_low_input' || (dirReg.logical_name || '').toUpperCase().includes('INV');
                    if (isActiveLow) {
                      isInput = (dirVal & (1 << bitIndex)) === 0;
                    } else {
                      isInput = (dirVal & (1 << bitIndex)) !== 0;
                    }
                  } else {
                    isInput = true;
                  }

                  // Read from dataInReg if input, dataOutReg if output
                  const isOn = isInput 
                    ? (dataInVal & (1 << bitIndex)) !== 0
                    : (dataOutVal & (1 << bitIndex)) !== 0;

                  return (
                    <div 
                      key={bitIndex} 
                      className={`gpio-bit ${isInput ? 'input' : 'output'} ${isOn ? 'on' : 'off'}`}
                      onClick={() => handleGpioToggle(dev.name, bitIndex, isOn, dataInReg?.name || 'PDIR')}
                      style={{ cursor: 'pointer' }}
                      title={`${labelName} Bit ${bitIndex} (${isInput ? 'Input' : 'Output'})`}
                    >
                      <div className="gpio-indicator" style={{ pointerEvents: 'none' }}></div>
                      <span className="gpio-label" style={{ pointerEvents: 'none' }}>B{bitIndex}</span>
                    </div>
                  );
                })}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}

export default GpioPanel;
