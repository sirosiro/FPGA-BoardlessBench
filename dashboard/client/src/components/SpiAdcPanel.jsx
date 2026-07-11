import { useState, useEffect } from 'react';
import { useDashboard } from './DashboardContext';
import { Cpu } from 'lucide-react';

export default function SpiAdcPanel() {
  const { manifest, handleSpiAdcInject } = useDashboard();
  const [adcValues, setAdcValues] = useState(Array(8).fill(2048));

  // Find if there is an MCP3208 device in the manifest
  const hasAdc = manifest?.devices?.some(dev => 
    dev.type === 'spi' && 
    dev.spi_slaves?.some(slave => slave.compatible === 'microchip,mcp3208')
  );

  useEffect(() => {
    // Optionally fetch initial values or default to 2048
    setAdcValues(Array(8).fill(2048));
  }, [manifest]);

  if (!hasAdc) {
    return (
      <div style={{ padding: '1.5rem', color: '#8b949e', fontSize: '0.9rem', textAlign: 'center', background: '#0d1117', height: '100%', display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
        No virtual SPI ADC (MCP3208) detected in the current board manifest.
      </div>
    );
  }

  const handleSliderChange = (channel, value) => {
    const newVal = parseInt(value, 10);
    const updated = [...adcValues];
    updated[channel] = newVal;
    setAdcValues(updated);
    handleSpiAdcInject(channel, newVal);
  };

  const handleShortcut = (channel, val) => {
    const updated = [...adcValues];
    updated[channel] = val;
    setAdcValues(updated);
    handleSpiAdcInject(channel, val);
  };

  return (
    <div style={{
      padding: '1rem',
      background: '#0d1117',
      color: '#c9d1d9',
      height: '100%',
      overflowY: 'auto',
      fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif'
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '1.2rem', borderBottom: '1px solid #30363d', paddingBottom: '0.5rem' }}>
        <Cpu size={16} style={{ color: '#bc8cff' }} />
        <span style={{ fontWeight: '700', fontSize: '0.9rem', color: '#f0f6fc' }}>Virtual SPI ADC (MCP3208 12-bit)</span>
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem' }}>
        {adcValues.map((value, ch) => {
          const voltage = ((value / 4095) * 3.3).toFixed(2);
          return (
            <div key={ch} style={{
              background: '#161b22',
              border: '1px solid #30363d',
              borderRadius: '8px',
              padding: '0.8rem',
              display: 'flex',
              flexDirection: 'column',
              gap: '0.6rem',
              transition: 'border-color 0.2s',
            }}
            onMouseEnter={(e) => e.currentTarget.style.borderColor = '#bc8cff'}
            onMouseLeave={(e) => e.currentTarget.style.borderColor = '#30363d'}
            >
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', fontSize: '0.8rem', fontWeight: '600' }}>
                <span style={{ color: '#bc8cff' }}>CH{ch} <span style={{ color: '#8b949e', fontWeight: 'normal' }}>(Analog Input)</span></span>
                <div style={{ display: 'flex', gap: '0.8rem' }}>
                  <span style={{ color: '#c9d1d9' }}>{value} <span style={{ fontSize: '0.7rem', color: '#8b949e' }}>LSB</span></span>
                  <span style={{ color: '#58a6ff', fontWeight: 'bold' }}>{voltage} V</span>
                </div>
              </div>

              <div style={{ display: 'flex', alignItems: 'center', gap: '1rem' }}>
                <input
                  type="range"
                  min="0"
                  max="4095"
                  value={value}
                  onChange={(e) => handleSliderChange(ch, e.target.value)}
                  style={{
                    flex: 1,
                    height: '6px',
                    borderRadius: '3px',
                    background: '#21262d',
                    outline: 'none',
                    WebkitAppearance: 'none',
                    cursor: 'pointer',
                    accentColor: '#bc8cff'
                  }}
                />
              </div>

              <div style={{ display: 'flex', gap: '0.4rem', justifyContent: 'flex-end' }}>
                <button
                  onClick={() => handleShortcut(ch, 0)}
                  style={{
                    padding: '2px 8px',
                    fontSize: '0.7rem',
                    background: '#21262d',
                    border: '1px solid #30363d',
                    color: '#c9d1d9',
                    borderRadius: '4px',
                    cursor: 'pointer'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.background = '#30363d'}
                  onMouseLeave={(e) => e.currentTarget.style.background = '#21262d'}
                >
                  0V
                </button>
                <button
                  onClick={() => handleShortcut(ch, 2048)}
                  style={{
                    padding: '2px 8px',
                    fontSize: '0.7rem',
                    background: '#21262d',
                    border: '1px solid #30363d',
                    color: '#c9d1d9',
                    borderRadius: '4px',
                    cursor: 'pointer'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.background = '#30363d'}
                  onMouseLeave={(e) => e.currentTarget.style.background = '#21262d'}
                >
                  1.65V
                </button>
                <button
                  onClick={() => handleShortcut(ch, 4095)}
                  style={{
                    padding: '2px 8px',
                    fontSize: '0.7rem',
                    background: '#21262d',
                    border: '1px solid #30363d',
                    color: '#c9d1d9',
                    borderRadius: '4px',
                    cursor: 'pointer'
                  }}
                  onMouseEnter={(e) => e.currentTarget.style.background = '#30363d'}
                  onMouseLeave={(e) => e.currentTarget.style.background = '#21262d'}
                >
                  3.3V
                </button>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
