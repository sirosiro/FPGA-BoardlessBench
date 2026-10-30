import { useMemo, useState, useEffect } from 'react';
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
  const { 
    traceHistory: historyData, 
    handleClearTrace: onClear, 
    hiddenTraceKeys: hiddenKeys, 
    toggleTraceKey,
    timeTravelState,
    isTimeTravelActive,
    scrubTo,
    returnToLive
  } = useDashboard();
  
  // 表示モード: 'zero_center' (ゼロ中央固定) | 'signed' (符号付き自動レンジ) | 'unsigned' (従来の非負)
  const [mode, setMode] = useState('zero_center');

  // オシロスコープ時間軸スパン: 50 | 100 | 250 | 'all'
  const [timebaseSpan, setTimebaseSpan] = useState('all');

  // ドラッグ操作判定
  const [isMouseDown, setIsMouseDown] = useState(false);

  // ESCキーでLIVEへ復帰 & 左右矢印キー(◄/►)でコマ送り/コマ戻し (Shift同時押しで10フレームジャンプ)
  useEffect(() => {
    const handleKeyDown = (e) => {
      // 入力フォームやターミナル等にフォーカスがある時はスキップ
      const tag = e.target?.tagName?.toLowerCase();
      if (tag === 'input' || tag === 'textarea' || e.target?.isContentEditable) {
        return;
      }

      if (e.key === 'Escape') {
        returnToLive();
        return;
      }

      if (!historyData || historyData.length === 0) return;

      const step = e.shiftKey ? 10 : 1;

      if (e.key === 'ArrowLeft') {
        e.preventDefault();
        let currIdx = -1;
        if (timeTravelState?.snapshot) {
          currIdx = historyData.findIndex(d => d.index === timeTravelState.snapshot.index);
        }
        if (currIdx === -1) {
          // LIVE状態から左キーを押した場合は最新から1つ前へ
          currIdx = historyData.length - 1;
        }
        const targetIdx = Math.max(0, currIdx - step);
        if (historyData[targetIdx]) {
          scrubTo(historyData[targetIdx]);
        }
      } else if (e.key === 'ArrowRight') {
        if (!timeTravelState?.snapshot) {
          return; // すでにLIVE中なら右へは進めない
        }
        e.preventDefault();
        const currIdx = historyData.findIndex(d => d.index === timeTravelState.snapshot.index);
        if (currIdx !== -1) {
          if (currIdx >= historyData.length - 1) {
            // 右端（最新）に到達したらLIVEへ復帰
            returnToLive();
          } else {
            const targetIdx = Math.min(historyData.length - 1, currIdx + step);
            if (historyData[targetIdx]) {
              scrubTo(historyData[targetIdx]);
            }
          }
        }
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [historyData, timeTravelState, scrubTo, returnToLive]);

  // 履歴データからレジスタ名のリストを抽出（全体から抽出して欠損防止）
  const registerKeys = useMemo(() => {
    if (!historyData || historyData.length === 0) return [];
    return Object.keys(historyData[0]).filter(key => key !== 'index' && key !== 'time' && key !== 'telemetry');
  }, [historyData]);

  // オシロスコープ・スパンによるデータ切り出し (選択カーソルまたは最新を中心)
  const slicedHistoryData = useMemo(() => {
    if (!historyData || historyData.length === 0) return [];
    if (timebaseSpan === 'all' || historyData.length <= timebaseSpan) return historyData;
    
    let centerIdx = -1;
    if (timeTravelState?.snapshot) {
      centerIdx = historyData.findIndex(d => d.index === timeTravelState.snapshot.index);
    }
    if (centerIdx === -1) {
      centerIdx = historyData.length - 1; // LIVE時は最新点
    }
    
    const half = Math.floor(timebaseSpan / 2);
    let start = centerIdx - half;
    let end = start + timebaseSpan;
    if (start < 0) {
      start = 0;
      end = Math.min(historyData.length, timebaseSpan);
    } else if (end > historyData.length) {
      end = historyData.length;
      start = Math.max(0, end - timebaseSpan);
    }
    return historyData.slice(start, end);
  }, [historyData, timebaseSpan, timeTravelState?.snapshot]);

  // モードに応じたデータの正規化 (0-100%に変換)
  const normalizedData = useMemo(() => {
    if (!slicedHistoryData || slicedHistoryData.length === 0) return [];
    
    const stats = {};
    registerKeys.forEach(key => {
      if (mode === 'unsigned') {
        const values = slicedHistoryData
          .map(d => (typeof d[key] === 'number' ? d[key] : (d[key] != null ? Number(d[key]) : NaN)))
          .filter(v => !isNaN(v));
        const min = values.length > 0 ? Math.min(...values) : 0;
        const max = values.length > 0 ? Math.max(...values) : 0;
        stats[key] = { min, max, range: max - min };
      } else if (mode === 'signed') {
        const values = slicedHistoryData
          .map(d => (d[key] != null ? toInt32(d[key]) : NaN))
          .filter(v => !isNaN(v));
        const min = values.length > 0 ? Math.min(...values) : 0;
        const max = values.length > 0 ? Math.max(...values) : 0;
        stats[key] = { min, max, range: max - min };
      } else { // zero_center
        const values = slicedHistoryData
          .map(d => (d[key] != null ? toInt32(d[key]) : NaN))
          .filter(v => !isNaN(v));
        const absMax = values.length > 0 ? Math.max(...values.map(Math.abs)) : 0;
        stats[key] = { absMax };
      }
    });

    return slicedHistoryData.map(d => {
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
  }, [slicedHistoryData, registerKeys, mode]);

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

  // チャートクリックまたはなぞりドラッグによるシーク
  const handleChartInteraction = (e) => {
    if (!e) return;
    let targetIndex = null;
    if (e.activePayload && e.activePayload.length > 0 && e.activePayload[0].payload) {
      targetIndex = e.activePayload[0].payload.index;
    } else if (e.activeLabel != null) {
      targetIndex = Number(e.activeLabel);
    } else if (e.activeTooltipIndex != null && slicedHistoryData[e.activeTooltipIndex]) {
      targetIndex = slicedHistoryData[e.activeTooltipIndex].index;
    }

    if (targetIndex != null) {
      const snap = historyData.find(d => d.index === targetIndex || d.index === Number(targetIndex));
      if (snap) {
        scrubTo(snap);
      }
    }
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

          {/* Time-Travel Status Badge & Return Button */}
          {isTimeTravelActive ? (
            <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
              <span style={{
                fontSize: '0.7rem',
                backgroundColor: 'rgba(0, 242, 254, 0.12)',
                border: '1px solid #00f2fe',
                color: '#00f2fe',
                padding: '2px 8px',
                borderRadius: '10px',
                fontWeight: 'bold',
                display: 'flex',
                alignItems: 'center',
                gap: '4px'
              }}>
                ⏪ TIME-TRAVEL: #{timeTravelState.snapshot.index} ({timeTravelState.snapshot.time})
              </span>
              <button
                onClick={returnToLive}
                style={{
                  backgroundColor: '#238636',
                  color: '#ffffff',
                  border: 'none',
                  padding: '3px 8px',
                  fontSize: '0.7rem',
                  borderRadius: '4px',
                  cursor: 'pointer',
                  fontWeight: '600',
                  display: 'flex',
                  alignItems: 'center',
                  gap: '4px',
                  boxShadow: '0 0 6px rgba(35, 134, 54, 0.4)'
                }}
                title="Return to LIVE real-time telemetry (Shortcut: ESC)"
              >
                ● LIVEへ復帰 (ESC)
              </button>
            </div>
          ) : (
            <span style={{
              fontSize: '0.7rem',
              backgroundColor: 'rgba(35, 134, 54, 0.15)',
              border: '1px solid #238636',
              color: '#3fb950',
              padding: '2px 8px',
              borderRadius: '10px',
              fontWeight: '600'
            }}>
              ● LIVE
            </span>
          )}
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          {/* Oscilloscope Timebase Span Selector */}
          <div style={{
            display: 'flex',
            alignItems: 'center',
            backgroundColor: '#161b22',
            padding: '2px',
            borderRadius: '6px',
            border: '1px solid #30363d',
            gap: '2px'
          }}>
            <span style={{ fontSize: '0.65rem', color: '#8b949e', padding: '0 4px', fontWeight: 600 }}>SPAN:</span>
            {[50, 100, 250, 'all'].map(s => (
              <button
                key={s}
                onClick={() => setTimebaseSpan(s)}
                style={{
                  backgroundColor: timebaseSpan === s ? '#388bfd' : 'transparent',
                  color: timebaseSpan === s ? '#ffffff' : '#8b949e',
                  border: 'none',
                  padding: '2px 6px',
                  fontSize: '0.65rem',
                  borderRadius: '4px',
                  cursor: 'pointer',
                  fontWeight: timebaseSpan === s ? '600' : '400',
                  transition: 'all 0.15s ease'
                }}
                title={s === 'all' ? 'Show all snapshots (max 500)' : `Zoom to ${s} snapshots centered on cursor`}
              >
                {s === 'all' ? 'All' : `${s}pt`}
              </button>
            ))}
          </div>

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

      <div 
        onMouseDown={() => setIsMouseDown(true)}
        onMouseUp={() => setIsMouseDown(false)}
        onMouseLeave={() => setIsMouseDown(false)}
        style={{ 
          flex: 1, 
          minHeight: '300px', 
          position: 'relative',
          cursor: isMouseDown ? 'col-resize' : 'crosshair'
        }}
      >
        {slicedHistoryData && slicedHistoryData.length > 0 ? (
          <ResponsiveContainer width="100%" height="100%">
            <LineChart 
              data={normalizedData} 
              margin={{ top: 5, right: 5, left: -20, bottom: 0 }}
              onClick={handleChartInteraction}
              onMouseDown={handleChartInteraction}
              onMouseMove={(e) => { if (isMouseDown) handleChartInteraction(e); }}
            >
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
              {/* Scrubber Needle Line in Time-Travel Mode */}
              {timeTravelState?.snapshot && (
                <ReferenceLine 
                  x={timeTravelState.snapshot.index} 
                  stroke="#00f2fe" 
                  strokeWidth={2}
                  strokeDasharray="3 3"
                  label={{ 
                    value: `⏪ #${timeTravelState.snapshot.index}`, 
                    position: 'insideTopRight', 
                    fill: '#00f2fe', 
                    fontSize: 10,
                    fontWeight: 'bold'
                  }} 
                />
              )}
              <XAxis 
                dataKey="index" 
                stroke="#484f58" 
                tick={{ fontSize: 9 }}
                hide={slicedHistoryData.length < 2}
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
                    dot={slicedHistoryData.length <= 1 ? { r: 3 } : false}
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
        <span>* Click/drag or use ◄/► arrow keys (Shift+◄/►: x10) to scrub frame-by-frame. Press ESC to return to LIVE. Click legend to toggle lines.</span>
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
