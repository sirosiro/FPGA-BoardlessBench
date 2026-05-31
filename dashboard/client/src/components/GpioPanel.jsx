import { ToggleRight } from 'lucide-react';
import { useDashboard } from './DashboardContext';

function GpioPanel() {
  const { gpioDevices, registers, handleGpioToggle } = useDashboard();

  return (
    <div className="gpio-pane" style={{ height: '100%', display: 'flex', flexDirection: 'column', overflow: 'hidden' }}>
      <div className="panel-header"><ToggleRight size={16} /> GPIO / Pin Array (118ch)</div>
      <div className="gpio-viewport" style={{ flex: 1, overflowY: 'auto', padding: '1rem' }}>
        {gpioDevices.map((dev, i) => {
          const devDataRegs = registers.filter(r => r.deviceName === dev.name && r.name.startsWith('DATA'));
          
          if (devDataRegs.length === 0) return null;

          return devDataRegs.map((dataReg, chIndex) => {
            // Find matching TRI register (e.g., DATA -> TRI, DATA2 -> TRI2)
            const suffix = dataReg.name.replace('DATA', '');
            const triReg = registers.find(r => r.deviceName === dev.name && r.name === `TRI${suffix}`);
            
            const dataVal = dataReg?.decimal || 0;
            const triVal = triReg?.decimal || 0;
            const labelName = devDataRegs.length > 1 ? `${dev.name} (${dataReg.name})` : dev.name;
            
            return (
              <div key={`gpio-${i}-ch${chIndex}`} className="gpio-dev-group">
                <div className="gpio-dev-label">{labelName}</div>
                <div className="gpio-grid">
                  {Array.from({ length: 32 }).map((_, bitIndex) => {
                    const isInput = (triVal & (1 << bitIndex)) !== 0;
                    const isOn = (dataVal & (1 << bitIndex)) !== 0;
                    return (
                      <div 
                        key={bitIndex} 
                        className={`gpio-bit ${isInput ? 'input' : 'output'} ${isOn ? 'on' : 'off'}`}
                        onClick={() => isInput && handleGpioToggle(dev.name, bitIndex, isOn, dataReg.name)}
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
          });
        })}
      </div>
    </div>
  );
}

export default GpioPanel;
