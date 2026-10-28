import { useMemo, useState } from 'react';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer,
  ReferenceLine
} from 'recharts';
import { useDashboard } from './DashboardContext';

// 32-bit 符号なし整数 (uint32) を 2の補数符号付き整数 (int32) に変換
const toInt32 = (val) => {
  if (val == null) return 0;
  const n = typeof val === 'number' ? val : Number(val);
  if (isNaN(n)) return 0;
  return (n & 0x80000000) ? n - 0x100000000 : n;
};

const RegisterTracer = () => {
  const { traceHistory: historyData, handleClearTrace: onClear, hiddenTraceKeys: hiddenKeys, toggleTraceKey } = useDashboard();
  
  // 表示モード: 'zero_center' (ゼロ中央固定) | 'signed' (符号付き自動レンジ) | 'unsigned' (従来の非負)
  const [mode, setMode] = useState('zero_center');

  // 履歴データからレジスタ名のリストを抽出
  const registerKeys = useMemo(() => {
    if (!historyData || historyData.length === 0) return [];
    return Object.keys(historyData[0]).filter(key => key !== 'index' && key !== 'time');
  }, [historyData]);

  // モードに応じたデータの正規化 (0-100%に変換)
  const normalizedData = useMemo(() => {
    if (!historyData || historyData.length === 0) return [];
    
    const stats = {};
    registerKeys.forEach(key => {
      if (mode === 'unsigned') {
        const values = historyData
          .map(d => (typeof d[key] === 'number' ? d[key] : (d[key] != null ? Number(d[key]) : NaN)))
          .filter(v => !isNaN(v));
        const min = values.length > 0 ? Math.min(...values) : 0;
        const max = values.length > 0 ? Math.max(...values) : 0;
        stats[key] = { min, max, range: max - min };
      } else if (mode === 'signed') {
        const values = historyData
          .map(d => (d[key] != null ? toInt32(d[key]) : NaN))
          .filter(v => !isNaN(v));
        const min = values.length > 0 ? Math.min(...values) : 0;
        const max = values.length > 0 ? Math.max(...values) : 0;
        stats[key] = { min, max, range: max - min };
      } else { // zero_center
        const values = historyData
          .map(d => (d[key] != null ? toInt32(d[key]) : NaN))
          .filter(v => !isNaN(v));
        const absMax = values.length > 0 ? Math.max(...values.map(Math.abs)) : 0;
        stats[key] = { absMax };
      }
    });

    return historyData.map(d => {
      const normalizedEntry = { ...d };
      registerKeys.forEach(key => {
        if (mode === 'unsigned') {
          const { min, range } = stats[key];
          const rawVal = d[key] != null ? Number(d[key]) : min;
          const val = isNaN(rawVal) ? min : rawVal;
          normalizedEntry[`${key}_norm`] = range === 0 ? 50 : ((val - min) / range) * 100;
        } else if (mode === 'signed') {
          const { min, range } = stats[key];
          const val = toInt32(d[key]);
          normalizedEntry[`${key}_norm`] = range === 0 ? 50 : ((val - min) / range) * 100;
        } else { // zero_center
          const { absMax } = stats[key];
          const val = toInt32(d[key]);
          normalizedEntry[`${key}_norm`] = absMax === 0 ? 50 : 50 + (val / absMax) * 46;
        }
      });
      return normalizedEntry;
    });
  }, [historyData, registerKeys, mode]);

  const getColor = (index) => {
    const colors = [
      '#58a6ff', '#3fb950', '#d29922', '#f85149', '#bc8cff', 
      '#1f6feb', '#238636', '#9e6a03', '#da3633', '#8957e5'
    ];
    return colors[index % colors.length];
  };

  // 凡例クリック時のハンドラ
  const handleLegendClick = (e) => {
    const { dataKey } = e;
    const key = dataKey.replace('_norm', '');
    toggleTraceKey(key);
  };

  return (
    <div style={{ 
      padding: '12px', 
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      boxSizing: 'border-box'
    }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '10px', flexWrap: 'wrap', gap: '8px' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          <h3 style={{ margin: 0, fontSize: '0.9rem', color: '#e6edf3' }}>Register State Tracer</h3>
          <span style={{ 
            fontSize: '0.7rem', 
            backgroundColor: '#21262d', 
            padding: '2px 6px', 
            borderRadius: '10px',
            color: '#8b949e',
            border: '1px solid #30363d'
          }}>
            {historyData?.length || 0} snapshots
          </span>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          {/* Segmented Mode Selector */}
          <div style={{
            display: 'flex',
            alignItems: 'center',
            backgroundColor: '#161b22',
            padding: '2px',
            borderRadius: '6px',
            border: '1px solid #30363d'
          }}>
            <button
              onClick={() => setMode('zero_center')}
              style={{
                backgroundColor: mode === 'zero_center' ? '#238636' : 'transparent',
                color: mode === 'zero_center' ? '#ffffff' : '#8b949e',
                border: 'none',
                padding: '3px 8px',
                fontSize: '0.7rem',
                borderRadius: '4px',
                cursor: 'pointer',
                fontWeight: mode === 'zero_center' ? '600' : '400',
                transition: 'all 0.15s ease'
              }}
              title="Zero-Center Mode: 0x00 / 0xFFFF centered at 50% baseline (ideal for signed commands & physical variables)"
            >
              Zero-Center
            </button>
            <button
              onClick={() => setMode('signed')}
              style={{
                backgroundColor: mode === 'signed' ? '#1f6feb' : 'transparent',
                color: mode === 'signed' ? '#ffffff' : '#8b949e',
                border: 'none',
                padding: '3px 8px',
                fontSize: '0.7rem',
                borderRadius: '4px',
                cursor: 'pointer',
                fontWeight: mode === 'signed' ? '600' : '400',
                transition: 'all 0.15s ease'
              }}
              title="Signed Mode: 2's complement auto-range (min to max)"
            >
              Signed
            </button>
            <button
              onClick={() => setMode('unsigned')}
              style={{
                backgroundColor: mode === 'unsigned' ? '#30363d' : 'transparent',
                color: mode === 'unsigned' ? '#ffffff' : '#8b949e',
                border: 'none',
                padding: '3px 8px',
                fontSize: '0.7rem',
                borderRadius: '4px',
                cursor: 'pointer',
                fontWeight: mode === 'unsigned' ? '600' : '400',
                transition: 'all 0.15s ease'
              }}
              title="Unsigned Mode: Standard 0 ~ 4.29G range (for memory addresses & bitmasks)"
            >
              Unsigned
            </button>
          </div>
          <button 
            onClick={onClear}
            style={{
              backgroundColor: '#21262d',
              color: '#f85149',
              border: '1px solid #30363d',
              padding: '4px 10px',
              fontSize: '0.7rem',
              borderRadius: '4px',
              cursor: 'pointer',
              fontWeight: '600'
            }}
          >
            Clear
          </button>
        </div>
      </div>

      <div style={{ flex: 1, minHeight: '300px', position: 'relative' }}>
        {historyData && historyData.length > 0 ? (
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={normalizedData} margin={{ top: 5, right: 5, left: -20, bottom: 0 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#21262d" vertical={false} />
              {mode === 'zero_center' && (
                <ReferenceLine 
                  y={50} 
                  stroke="#388bfd" 
                  strokeDasharray="4 4" 
                  strokeOpacity={0.45} 
                  label={{ value: '0 / 0xFFFF (Zero)', position: 'insideTopLeft', fill: '#8b949e', fontSize: 9 }}
                />
              )}
              <XAxis 
                dataKey="index" 
                stroke="#484f58" 
                tick={{ fontSize: 9 }}
                hide={historyData.length < 2}
              />
              <YAxis 
                domain={[0, 100]} 
                hide={true}
              />
              <Tooltip 
                contentStyle={{ 
                  backgroundColor: '#161b22', 
                  border: '1px solid #30363d', 
                  borderRadius: '6px',
                  fontSize: '0.8rem',
                  color: '#e6edf3'
                }}
                itemStyle={{ fontSize: '11px', padding: '2px 0' }}
                labelStyle={{ color: '#8b949e', marginBottom: '4px' }}
                formatter={(value, name, props) => {
                  const key = name.split(' (')[0];
                  const originalKey = registerKeys.find(k => k.replace(/_/g, ': ') === key) || key;
                  const originalValue = props.payload[originalKey];
                  if (originalValue == null) return ['', key];
                  const hexStr = `0x${(Number(originalValue) >>> 0).toString(16).padStart(8, '0')}`;
                  if (mode === 'unsigned') {
                    return [hexStr, key];
                  } else {
                    const signedVal = toInt32(originalValue);
                    const signStr = signedVal >= 0 ? `+${signedVal.toLocaleString()}` : signedVal.toLocaleString();
                    return [`${hexStr} (${signStr})`, key];
                  }
                }}
              />
              <Legend 
                verticalAlign="top" 
                height={36}
                iconType="circle"
                wrapperStyle={{ fontSize: '0.7rem', paddingTop: '0', cursor: 'pointer' }}
                onClick={handleLegendClick}
              />
              {registerKeys
                .map((key, i) => ({ key, originalIndex: i }))
                .filter(({ key }) => !hiddenKeys[key])
                .map(({ key, originalIndex }) => (
                  <Line
                    key={key}
                    type="stepAfter"
                    dataKey={`${key}_norm`}
                    stroke={getColor(originalIndex)}
                    dot={historyData.length <= 1 ? { r: 3 } : false}
                    activeDot={{ r: 3 }}
                    name={key.replace(/_/g, ': ')}
                    strokeWidth={1.5}
                    isAnimationActive={false}
                  />
                ))
              }
            </LineChart>
          </ResponsiveContainer>
        ) : (
          <div style={{ 
            display: 'flex', 
            flexDirection: 'column',
            justifyContent: 'center', 
            alignItems: 'center', 
            height: '100%', 
            color: '#484f58',
            fontSize: '0.8rem',
            border: '1px dashed #30363d',
            borderRadius: '6px'
          }}>
            <span>Waiting for register changes...</span>
            <span style={{ fontSize: '0.7rem', marginTop: '4px' }}>(e.g. Type 'mode seq' in shell)</span>
          </div>
        )}
      </div>
      <div style={{ fontSize: '0.65rem', color: '#8b949e', marginTop: '8px', display: 'flex', justifyContent: 'space-between', flexWrap: 'wrap', gap: '4px' }}>
        <span>* Click legend to toggle visibility.</span>
        <span>
          Current Mode: <strong style={{ color: mode === 'zero_center' ? '#3fb950' : mode === 'signed' ? '#58a6ff' : '#d29922' }}>
            {mode === 'zero_center' ? 'Zero-Center (0/0xFFFF at 50% baseline)' : mode === 'signed' ? 'Signed 32-bit (Auto Min/Max)' : 'Unsigned 32-bit (0-100%)'}
          </strong>
        </span>
      </div>
    </div>
  );
};

export default RegisterTracer;
