import { useMemo, useState } from 'react';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  Legend,
  ResponsiveContainer
} from 'recharts';
import { useDashboard } from './DashboardContext';

const RegisterTracer = () => {
  const { traceHistory: historyData, handleClearTrace: onClear, hiddenTraceKeys: hiddenKeys, toggleTraceKey } = useDashboard();

  // 履歴データからレジスタ名のリストを抽出
  const registerKeys = useMemo(() => {
    if (!historyData || historyData.length === 0) return [];
    return Object.keys(historyData[0]).filter(key => key !== 'index' && key !== 'time');
  }, [historyData]);

  // 各レジスタの変化を可視化するためにデータを正規化 (0-100%に変換)
  const normalizedData = useMemo(() => {
    if (!historyData || historyData.length === 0) return [];
    
    const stats = {};
    registerKeys.forEach(key => {
      const values = historyData.map(d => d[key]);
      const min = Math.min(...values);
      const max = Math.max(...values);
      stats[key] = { min, max, range: max - min };
    });

    return historyData.map(d => {
      const normalizedEntry = { ...d };
      registerKeys.forEach(key => {
        const { min, range } = stats[key];
        normalizedEntry[`${key}_norm`] = range === 0 ? 50 : ((d[key] - min) / range) * 100;
      });
      return normalizedEntry;
    });
  }, [historyData, registerKeys]);

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
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '10px' }}>
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

      <div style={{ flex: 1, minHeight: '300px', position: 'relative' }}>
        {historyData && historyData.length > 0 ? (
          <ResponsiveContainer width="100%" height="100%">
            <LineChart data={normalizedData} margin={{ top: 5, right: 5, left: -20, bottom: 0 }}>
              <CartesianGrid strokeDasharray="3 3" stroke="#21262d" vertical={false} />
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
                  return [`0x${originalValue?.toString(16).padStart(8, '0')}`, key];
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
                    dot={false}
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
      <div style={{ fontSize: '0.65rem', color: '#8b949e', marginTop: '8px', fontStyle: 'italic' }}>
        * Click legend to toggle visibility. Values are normalized (0-100%).
      </div>
    </div>
  );
};

export default RegisterTracer;
