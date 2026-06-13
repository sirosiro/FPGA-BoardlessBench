import { ToggleRight } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function GpioPanel() {
  const { gpioDevices, registers, handleGpioToggle } = useDashboard();

  return (
    <div className="gpio-pane" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      <div className="panel-header"><ToggleRight size={16} /> GPIO / Pin Array (118ch)</div>
      <div className="gpio-viewport" style={{ flex: 1, overflowY: 'auto', padding: '1rem' }}>
        {gpioDevices.map((dev, i) => {
          // Find registers for this device
          const devRegs = registers.filter(r => r.deviceName === dev.name);
          if (devRegs.length === 0) return null;

          // Find direction register (TRI or GDIR)
          const dirReg = devRegs.find(r => 
            (r.logical_name || r.name).startsWith('TRI') || 
            (r.logical_name || r.name).startsWith('GDIR')
          );

          // Find DATA registers (Data Out vs Data In)
          const dataRegs = devRegs.filter(r => (r.logical_name || r.name).startsWith('DATA'));
          if (dataRegs.length === 0) return null;

          // If there are multiple DATA registers, try to distinguish by physical name (PDOR vs PDIR)
          let dataOutReg = dataRegs.find(r => r.name.includes('PDOR') || r.name.includes('OUT') || r.name === 'DR') || dataRegs[0];
          let dataInReg = dataRegs.find(r => r.name.includes('PDIR') || r.name.includes('IN')) || dataRegs[0];

          // If we couldn't distinguish, fallback to the same register
          if (!dataOutReg) dataOutReg = dataRegs[0];
          if (!dataInReg) dataInReg = dataRegs[0];

          const dirVal = dirReg?.decimal || 0;
          const dataOutVal = dataOutReg?.decimal || 0;
          const dataInVal = dataInReg?.decimal || 0;

          const isGdir = dirReg && (dirReg.logical_name || dirReg.name).startsWith('GDIR');
          const labelName = dev.name;

          return (
            <div key={`gpio-${i}`} className="gpio-dev-group">
              <div className="gpio-dev-label">{labelName}</div>
              <div className="gpio-grid">
                {Array.from({ length: 32 }).map((_, bitIndex) => {
                  const isInput = isGdir 
                    ? (dirVal & (1 << bitIndex)) === 0 
                    : (dirVal & (1 << bitIndex)) !== 0;

                  // Read from dataInReg if input, dataOutReg if output
                  const isOn = isInput 
                    ? (dataInVal & (1 << bitIndex)) !== 0
                    : (dataOutVal & (1 << bitIndex)) !== 0;

                  return (
                    <div 
                      key={bitIndex} 
                      className={`gpio-bit ${isInput ? 'input' : 'output'} ${isOn ? 'on' : 'off'}`}
                      onClick={() => isInput && handleGpioToggle(dev.name, bitIndex, isOn, dataInReg.name)}
                      title={`${labelName} Bit ${bitIndex} (${isInput ? 'Input' : 'Output'})`}
                    >
                      <div className="gpio-indicator"></div>
                      <span className="gpio-label">B{bitIndex}</span>
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
