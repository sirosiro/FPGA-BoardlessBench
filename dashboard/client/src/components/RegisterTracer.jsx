import { useMemo, useState, useEffect, useRef, memo } from 'react';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  ReferenceLine
} from 'recharts';
import { Layers, CheckSquare, Square, Download, Save, Copy, Check, X, FileText } from 'lucide-react';
import { useDashboard } from './DashboardContext';
import { toInt32, buildTraceExportPayload } from '../utils/traceExport';

// オシロスコープ用パレット（見やすくコントラストの高い24色）
const TRACE_COLORS = [
  '#00f2fe', '#4facfe', '#00e676', '#ffea00', '#ff007f', '#7928ca',
  '#ff9100', '#00b0ff', '#f50057', '#76ff03', '#d500f9', '#ffd600',
  '#1de9b6', '#ff1744', '#651fff', '#3d5afe', '#00e5ff', '#ff3d00',
  '#c6ff00', '#f50057', '#00c853', '#aa00ff', '#2979ff', '#ff6d00'
];

const RegisterTracer = () => {
  const { 
    traceHistory: historyData, 
    handleClearTrace: onClear, 
    hiddenTraceKeys: hiddenKeys, 
    toggleTraceKey,
    setTraceKeysVisibility,
    effectiveRegisters,
    manifest,
    timeTravelState,
    isTimeTravelActive,
    scrubTo,
    returnToLive,
    tracerState,
    updateTracerState
  } = useDashboard();
  
  const rootRef = useRef(null);
  const [exportOpen, setExportOpen] = useState(false);
  const [exportFormat, setExportFormat] = useState('md');
  const [exportSignalsScope, setExportSignalsScope] = useState('visible');
  const [exportRangeScope, setExportRangeScope] = useState('span');
  const [exportValueFormat, setExportValueFormat] = useState('both');
  const [exportStatus, setExportStatus] = useState(null);
  const [exportBusy, setExportBusy] = useState(false);
  const [copiedPath, setCopiedPath] = useState(false);

  // 表示モード: 'zero_center' (ゼロ中央固定) | 'signed' (符号付き自動レンジ) | 'unsigned' (従来の非負)
  const mode = tracerState?.mode || 'zero_center';
  const setMode = (m) => updateTracerState && updateTracerState({ mode: m });

  // オシロスコープ時間軸スパン: 50 | 100 | 250 | 'all'
  const timebaseSpan = tracerState?.timebaseSpan || 'all';
  const setTimebaseSpan = (s) => updateTracerState && updateTracerState({ timebaseSpan: s });

  // ドラッグ操作判定
  const [isMouseDown, setIsMouseDown] = useState(false);

  // 選択中のレジスタグループフィルター
  const selectedGroup = tracerState?.selectedGroup;
  const setSelectedGroup = (g) => updateTracerState && updateTracerState({ selectedGroup: g });

  // レジスタメタデータマップ (キー: `${deviceName}_${regName}`)
  const regMetaMap = useMemo(() => {
    const map = {};
    (effectiveRegisters || []).forEach(reg => {
      const key = `${reg.deviceName}_${reg.name}`;
      map[key] = reg;
    });
    return map;
  }, [effectiveRegisters]);

  // 履歴データからレジスタ名のリストを抽出
  const registerKeys = useMemo(() => {
    if (!historyData || historyData.length === 0) return [];
    return Object.keys(historyData[0]).filter(key => key !== 'index' && key !== 'time' && key !== 'telemetry');
  }, [historyData]);

  // グループ分類
  const { groupNames, groupMap } = useMemo(() => {
    const gMap = {};
    registerKeys.forEach((key, idx) => {
      const meta = regMetaMap[key];
      const grp = meta?.group || 'General';
      if (!gMap[grp]) gMap[grp] = [];
      gMap[grp].push({
        key,
        originalIndex: idx,
        name: meta?.name || key.replace(/^[^_]+_/, ''),
        group: grp,
        offset: meta?.offset || ''
      });
    });

    const gNames = Object.keys(gMap);
    return { groupNames: gNames, groupMap: gMap };
  }, [registerKeys, regMetaMap]);

  // 初期グループ選択: グループが複数ある場合は先頭のグループ（制御系）をデフォルト選択（未保存時）
  useEffect(() => {
    if (groupNames.length > 0 && !selectedGroup) {
      const defaultGroup = groupNames.includes('Main Registers') ? 'Main Registers' : (groupNames.length > 1 ? groupNames[0] : 'ALL');
      if (updateTracerState) {
        updateTracerState({ selectedGroup: defaultGroup });
      }
    }
  }, [groupNames, selectedGroup]);

  // 文字列バッファ（波形表示に適さないもの）を自動識別
  const isStringBufferKey = (key) => {
    const meta = regMetaMap[key];
    const grp = (meta?.group || '').toLowerCase();
    const name = (meta?.name || key).toUpperCase();
    return grp.includes('string') || grp.includes('buffer') || name.startsWith('GRADE_STR') || name.startsWith('EVENT_STR') || name === 'SHM_MAGIC';
  };

  // 現在の選択グループに属するレジスタ
  const activeGroupItems = useMemo(() => {
    if (!selectedGroup || selectedGroup === 'ALL') {
      return registerKeys.map((key, idx) => ({
        key,
        originalIndex: idx,
        name: regMetaMap[key]?.name || key.replace(/^[^_]+_/, ''),
        group: regMetaMap[key]?.group || 'General',
        offset: regMetaMap[key]?.offset || ''
      }));
    }
    return groupMap[selectedGroup] || [];
  }, [selectedGroup, registerKeys, groupMap, regMetaMap]);

  // ESCキーでLIVEへ復帰 & 左右矢印キー(◄/►)でコマ送り/コマ戻し
  useEffect(() => {
    const handleKeyDown = (e) => {
      const tag = e.target?.tagName?.toLowerCase();
      if (tag === 'input' || tag === 'textarea' || e.target?.isContentEditable) {
        return;
      }

      if (e.key === 'Escape') {
        if (exportOpen) {
          setExportOpen(false);
          return;
        }
        returnToLive();
        return;
      }

      if (exportOpen) return;

      if (!historyData || historyData.length === 0) return;

      const step = e.shiftKey ? 10 : 1;

      if (e.key === 'ArrowLeft') {
        e.preventDefault();
        let currIdx = -1;
        if (timeTravelState?.snapshot) {
          currIdx = historyData.findIndex(d => d.index === timeTravelState.snapshot.index);
        }
        if (currIdx === -1) {
          currIdx = historyData.length - 1;
        }
        const targetIdx = Math.max(0, currIdx - step);
        if (historyData[targetIdx]) {
          scrubTo(historyData[targetIdx]);
        }
      } else if (e.key === 'ArrowRight') {
        if (!timeTravelState?.snapshot) {
          return;
        }
        e.preventDefault();
        const currIdx = historyData.findIndex(d => d.index === timeTravelState.snapshot.index);
        if (currIdx !== -1) {
          if (currIdx >= historyData.length - 1) {
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
  }, [historyData, timeTravelState, scrubTo, returnToLive, exportOpen]);

  // オシロスコープ・スパンによるデータ切り出し
  const slicedHistoryData = useMemo(() => {
    if (!historyData || historyData.length === 0) return [];
    if (timebaseSpan === 'all' || historyData.length <= timebaseSpan) return historyData;
    
    let centerIdx = -1;
    if (timeTravelState?.snapshot) {
      centerIdx = historyData.findIndex(d => d.index === timeTravelState.snapshot.index);
    }
    if (centerIdx === -1) {
      centerIdx = historyData.length - 1;
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

  // 波形プロット対象キー（選択グループ内 かつ 非非表示）
  const plottedKeys = useMemo(() => {
    return activeGroupItems.filter(({ key }) => {
      if (hiddenKeys[key]) return false;
      // 文字列バッファは初期状態で非表示扱い（未設定時）
      if (hiddenKeys[key] === undefined && isStringBufferKey(key)) return false;
      return true;
    });
  }, [activeGroupItems, hiddenKeys]);

  // モードに応じたデータの正規化 (0-100%に変換) - 表示対象キー (plottedKeys) のみ計算して大幅に高速化
  const normalizedData = useMemo(() => {
    if (!slicedHistoryData || slicedHistoryData.length === 0 || plottedKeys.length === 0) return [];
    
    const targetKeys = plottedKeys.map(p => p.key);
    const stats = {};
    targetKeys.forEach(key => {
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
      } else if (mode === 'zero_center') {
        const values = slicedHistoryData
          .map(d => {
            if (d[key] == null) return NaN;
            return toInt32(d[key]);
          })
          .filter(v => !isNaN(v));
        const maxAbs = values.length > 0 ? Math.max(...values.map(v => Math.abs(v))) : 1;
        stats[key] = { maxAbs: Math.max(maxAbs, 1) };
      }
    });

    return slicedHistoryData.map(item => {
      const normItem = { index: item.index, time: item.time };
      targetKeys.forEach(key => {
        const val = item[key];
        normItem[key] = val; // Tooltip formatter requires raw value
        if (val == null) {
          normItem[`${key}_norm`] = null;
          return;
        }

        if (mode === 'unsigned') {
          const { min, range } = stats[key] || { min: 0, range: 0 };
          const num = typeof val === 'number' ? val : Number(val);
          normItem[`${key}_norm`] = range === 0 ? 50 : ((num - min) / range) * 100;
        } else if (mode === 'signed') {
          const { min, range } = stats[key] || { min: 0, range: 0 };
          const sVal = toInt32(val);
          normItem[`${key}_norm`] = range === 0 ? 50 : ((sVal - min) / range) * 100;
        } else if (mode === 'zero_center') {
          const { maxAbs } = stats[key] || { maxAbs: 1 };
          const sVal = toInt32(val);
          const ratio = Math.max(-1, Math.min(1, sVal / maxAbs));
          normItem[`${key}_norm`] = 50 + ratio * 45;
        }
      });
      return normItem;
    });
  }, [slicedHistoryData, plottedKeys, mode]);

  const getColor = (index) => TRACE_COLORS[index % TRACE_COLORS.length];

  // クリック・ドラッグでスクラブ
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

  // グループ内一括表示/非表示切り替え
  const handleGroupVisibility = (show) => {
    if (!setTraceKeysVisibility) return;
    const updates = {};
    activeGroupItems.forEach(({ key }) => {
      updates[key] = !show; // hidden = true if hide
    });
    setTraceKeysVisibility(updates);
  };

  // 最新スナップショット（凡例値表示用）
  const latestSnapshot = useMemo(() => {
    if (timeTravelState?.snapshot) return timeTravelState.snapshot;
    if (historyData && historyData.length > 0) return historyData[historyData.length - 1];
    return null;
  }, [timeTravelState, historyData]);

  // All register items (for Export All Registers option)
  const allRegisterItems = useMemo(() => {
    return registerKeys.map((key, idx) => ({
      key,
      originalIndex: idx,
      name: regMetaMap[key]?.name || key.replace(/^[^_]+_/, ''),
      group: regMetaMap[key]?.group || 'General',
      offset: regMetaMap[key]?.offset || ''
    }));
  }, [registerKeys, regMetaMap]);

  const exportPayload = useMemo(() => {
    if (!exportOpen) return null;
    const targetSamples = exportRangeScope === 'span' ? slicedHistoryData : (historyData || []);
    const targetSignals = exportSignalsScope === 'visible' ? plottedKeys : allRegisterItems;
    return buildTraceExportPayload({
      format: exportFormat,
      samples: targetSamples,
      totalBufferedCount: historyData?.length || 0,
      signals: targetSignals,
      regMetaMap,
      manifest,
      timeTravelState,
      valueFormat: exportValueFormat,
      scopeSignals: exportSignalsScope,
      scopeRange: exportRangeScope,
      selectedGroup: selectedGroup || 'ALL',
      tracerMode: mode,
      timebaseSpan
    });
  }, [
    exportOpen,
    exportFormat,
    exportSignalsScope,
    exportRangeScope,
    exportValueFormat,
    slicedHistoryData,
    historyData,
    plottedKeys,
    allRegisterItems,
    regMetaMap,
    manifest,
    timeTravelState,
    selectedGroup,
    mode,
    timebaseSpan
  ]);

  const openExportModal = () => {
    setExportValueFormat(mode === 'unsigned' ? 'hex' : 'both');
    setExportStatus(null);
    setCopiedPath(false);
    setExportOpen(true);
  };

  const copyTextSafe = async (text) => {
    const doc = rootRef.current?.ownerDocument || document;
    const win = doc?.defaultView || window;
    try {
      if (win?.navigator?.clipboard?.writeText) {
        await win.navigator.clipboard.writeText(text);
        return true;
      }
    } catch {
      // Fallback below for non-HTTPS or pop-out contexts
    }
    try {
      const ta = doc.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      doc.body.appendChild(ta);
      ta.focus();
      ta.select();
      const ok = doc.execCommand('copy');
      doc.body.removeChild(ta);
      return ok;
    } catch {
      return false;
    }
  };

  const handleSaveToWorkspace = async () => {
    if (!exportPayload || exportBusy) return;
    setExportBusy(true);
    setExportStatus(null);
    setCopiedPath(false);
    try {
      const res = await fetch('/api/trace-export', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ format: exportPayload.format, content: exportPayload.content })
      });
      const data = await res.json();
      if (!res.ok || !data.success) {
        throw new Error(data.error || `HTTP ${res.status}`);
      }
      setExportStatus({
        type: 'success',
        message: `Saved ${(data.bytesWritten / 1024).toFixed(1)} KB to workspace root:`,
        path: data.path
      });
    } catch (err) {
      setExportStatus({ type: 'error', message: `Save failed: ${err.message}` });
    } finally {
      setExportBusy(false);
    }
  };

  const handleDownloadExport = () => {
    if (!exportPayload) return;
    const doc = rootRef.current?.ownerDocument || document;
    const win = doc?.defaultView || window;
    const mime = exportPayload.format === 'json'
      ? 'application/json;charset=utf-8'
      : exportPayload.format === 'csv'
        ? 'text/csv;charset=utf-8'
        : 'text/markdown;charset=utf-8';
    const blob = new win.Blob([exportPayload.content], { type: mime });
    const url = win.URL.createObjectURL(blob);
    const a = doc.createElement('a');
    a.href = url;
    a.download = exportPayload.fileName;
    doc.body.appendChild(a);
    a.click();
    doc.body.removeChild(a);
    setTimeout(() => win.URL.revokeObjectURL(url), 1000);
    setExportStatus({ type: 'success', message: `Downloaded ${exportPayload.fileName}` });
  };

  const handleCopyExport = async () => {
    if (!exportPayload) return;
    const ok = await copyTextSafe(exportPayload.content);
    setExportStatus(
      ok
        ? { type: 'success', message: `Copied ${exportPayload.rowCount} frames (${exportPayload.format.toUpperCase()}) to clipboard.` }
        : { type: 'error', message: 'Clipboard copy failed in this browser context.' }
    );
  };

  return (
    <div ref={rootRef} style={{ 
      padding: '12px', 
      display: 'flex',
      flexDirection: 'column',
      height: '100%',
      boxSizing: 'border-box',
      position: 'relative'
    }}>
      {/* Top Header Bar */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '8px', flexWrap: 'wrap', gap: '8px' }}>
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
            {historyData?.length || 0} pts
          </span>

          {/* Time-Travel Status Badge */}
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
                ⏪ TIME-TRAVEL: #{timeTravelState.snapshot.index}
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
                  boxShadow: '0 0 6px rgba(35, 134, 54, 0.4)'
                }}
                title="Return to LIVE real-time telemetry (Shortcut: ESC)"
              >
                ● Return to LIVE (ESC)
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

        {/* Right Controls: Span & Mode */}
        <div style={{ display: 'flex', alignItems: 'center', gap: '8px' }}>
          {/* Span Selector */}
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
              >
                {s === 'all' ? 'All' : `${s}pt`}
              </button>
            ))}
          </div>

          {/* Mode Selector */}
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
                fontWeight: mode === 'zero_center' ? '600' : '400'
              }}
              title="Zero-Center Mode: 0 centered at 50% baseline (ideal for physical signed commands)"
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
                fontWeight: mode === 'signed' ? '600' : '400'
              }}
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
                fontWeight: mode === 'unsigned' ? '600' : '400'
              }}
            >
              Unsigned
            </button>
          </div>
          <button
            onClick={openExportModal}
            disabled={!historyData || historyData.length === 0}
            title="Export trace samples to workspace root (fbb_trace.*), download, or clipboard for AI analysis"
            style={{
              backgroundColor: (!historyData || historyData.length === 0) ? '#161b22' : '#1f6feb',
              color: (!historyData || historyData.length === 0) ? '#484f58' : '#ffffff',
              border: '1px solid #30363d',
              padding: '4px 10px',
              fontSize: '0.7rem',
              borderRadius: '4px',
              cursor: (!historyData || historyData.length === 0) ? 'not-allowed' : 'pointer',
              fontWeight: '600',
              display: 'flex',
              alignItems: 'center',
              gap: '4px'
            }}
          >
            <Download size={12} />
            <span>Export</span>
          </button>
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

      {/* Register Group Filter Tabs */}
      {groupNames.length > 1 && (
        <div style={{
          display: 'flex',
          gap: '4px',
          overflowX: 'auto',
          paddingBottom: '4px',
          marginBottom: '6px',
          scrollbarWidth: 'thin'
        }}>
          {['ALL', ...groupNames].map(grp => {
            const count = grp === 'ALL' ? registerKeys.length : (groupMap[grp]?.length || 0);
            const isSelected = (selectedGroup === grp) || (grp === 'ALL' && selectedGroup === 'ALL');
            return (
              <button
                key={grp}
                onClick={() => setSelectedGroup(grp)}
                style={{
                  backgroundColor: isSelected ? '#1f6feb' : '#161b22',
                  color: isSelected ? '#ffffff' : '#8b949e',
                  border: isSelected ? '1px solid #388bfd' : '1px solid #30363d',
                  padding: '3px 9px',
                  fontSize: '0.68rem',
                  borderRadius: '12px',
                  cursor: 'pointer',
                  fontWeight: isSelected ? 600 : 400,
                  whiteSpace: 'nowrap',
                  display: 'flex',
                  alignItems: 'center',
                  gap: '5px',
                  transition: 'all 0.15s ease'
                }}
              >
                <Layers size={11} style={{ opacity: isSelected ? 1 : 0.6 }} />
                <span>{grp}</span>
                <span style={{ 
                  fontSize: '0.6rem', 
                  backgroundColor: isSelected ? 'rgba(255,255,255,0.2)' : '#21262d', 
                  padding: '1px 4px', 
                  borderRadius: '8px' 
                }}>
                  {count}
                </span>
              </button>
            );
          })}
        </div>
      )}

      {/* Interactive Compact Legend & Scope Bar */}
      <div style={{
        backgroundColor: '#161b22',
        border: '1px solid #30363d',
        borderRadius: '6px',
        padding: '6px 10px',
        marginBottom: '8px'
      }}>
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '4px' }}>
          <div style={{ fontSize: '0.68rem', color: '#8b949e', display: 'flex', alignItems: 'center', gap: '6px' }}>
            <span style={{ fontWeight: 600, color: '#c9d1d9' }}>
              {selectedGroup === 'ALL' ? 'All Groups' : selectedGroup}
            </span>
            <span>({plottedKeys.length} active of {activeGroupItems.length} regs)</span>
          </div>
          <div style={{ display: 'flex', gap: '6px' }}>
            <button
              onClick={() => handleGroupVisibility(true)}
              style={{
                fontSize: '0.62rem',
                backgroundColor: 'transparent',
                border: '1px solid #30363d',
                color: '#3fb950',
                padding: '1px 6px',
                borderRadius: '4px',
                cursor: 'pointer',
                display: 'flex',
                alignItems: 'center',
                gap: '3px'
              }}
            >
              <CheckSquare size={10} /> Show All
            </button>
            <button
              onClick={() => handleGroupVisibility(false)}
              style={{
                fontSize: '0.62rem',
                backgroundColor: 'transparent',
                border: '1px solid #30363d',
                color: '#f85149',
                padding: '1px 6px',
                borderRadius: '4px',
                cursor: 'pointer',
                display: 'flex',
                alignItems: 'center',
                gap: '3px'
              }}
            >
              <Square size={10} /> Hide All
            </button>
          </div>
        </div>

        {/* Scrollable Legend Chips Container */}
        <div style={{
          display: 'flex',
          flexWrap: 'wrap',
          gap: '4px',
          maxHeight: '62px',
          overflowY: 'auto',
          paddingRight: '4px'
        }}>
          {activeGroupItems.map(({ key, originalIndex, name, offset }) => {
            const isHidden = hiddenKeys[key] ?? isStringBufferKey(key);
            const color = getColor(originalIndex);
            const val = latestSnapshot ? latestSnapshot[key] : null;
            const valStr = val != null ? `0x${(Number(val) >>> 0).toString(16).padStart(4, '0')}` : '';

            return (
              <div
                key={key}
                onClick={() => toggleTraceKey(key)}
                style={{
                  display: 'flex',
                  alignItems: 'center',
                  gap: '4px',
                  backgroundColor: isHidden ? '#21262d' : 'rgba(255, 255, 255, 0.05)',
                  border: `1px solid ${isHidden ? '#30363d' : color}`,
                  opacity: isHidden ? 0.45 : 1.0,
                  borderRadius: '4px',
                  padding: '2px 6px',
                  cursor: 'pointer',
                  userSelect: 'none',
                  fontSize: '0.65rem',
                  fontFamily: 'monospace',
                  transition: 'all 0.12s ease'
                }}
                title={`Click to ${isHidden ? 'show' : 'hide'} ${name} (Offset ${offset})`}
              >
                <div style={{
                  width: '7px',
                  height: '7px',
                  borderRadius: '50%',
                  backgroundColor: color,
                  boxShadow: isHidden ? 'none' : `0 0 4px ${color}`
                }} />
                <span style={{ color: isHidden ? '#8b949e' : '#e6edf3', fontWeight: isHidden ? 'normal' : 600 }}>
                  {name}
                </span>
                {valStr && (
                  <span style={{ color: '#8b949e', fontSize: '0.6rem' }}>
                    {valStr}
                  </span>
                )}
              </div>
            );
          })}
        </div>
      </div>

      {/* Waveform Chart Area */}
      <div 
        onMouseDown={() => setIsMouseDown(true)}
        onMouseUp={() => setIsMouseDown(false)}
        onMouseLeave={() => setIsMouseDown(false)}
        style={{ 
          flex: 1, 
          minHeight: '260px', 
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
              {plottedKeys.map(({ key, originalIndex, name }) => (
                <Line
                  key={key}
                  type="stepAfter"
                  dataKey={`${key}_norm`}
                  name={name}
                  stroke={getColor(originalIndex)}
                  dot={slicedHistoryData.length <= 1 ? { r: 3 } : false}
                  activeDot={{ r: 3 }}
                  strokeWidth={1.5}
                  isAnimationActive={false}
                />
              ))}
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

      {/* Bottom Status / Navigation Hints */}
      <div style={{ fontSize: '0.65rem', color: '#8b949e', marginTop: '8px', display: 'flex', justifyContent: 'space-between', flexWrap: 'wrap', gap: '4px' }}>
        <span>* Click/drag or use ◄/► arrow keys (Shift+◄/►: x10) to scrub frame-by-frame. Press ESC to return to LIVE. Click badge to toggle.</span>
        <span>
          Current Mode: <strong style={{ color: mode === 'zero_center' ? '#3fb950' : mode === 'signed' ? '#58a6ff' : '#d29922' }}>
            {mode === 'zero_center' ? 'Zero-Center (0 at 50% baseline)' : mode === 'signed' ? 'Signed 32-bit (Auto Min/Max)' : 'Unsigned 32-bit (0-100%)'}
          </strong>
        </span>
      </div>

      {/* Inline AI-Ready Trace Export Modal (Portal Pop-out Safe) */}
      {exportOpen && (
        <div
          onClick={() => setExportOpen(false)}
          style={{
            position: 'absolute',
            inset: 0,
            backgroundColor: 'rgba(1, 4, 9, 0.78)',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            zIndex: 50,
            padding: '16px'
          }}
        >
          <div
            onClick={(e) => e.stopPropagation()}
            style={{
              width: '100%',
              maxWidth: '580px',
              maxHeight: '92%',
              display: 'flex',
              flexDirection: 'column',
              backgroundColor: '#161b22',
              border: '1px solid #30363d',
              borderRadius: '8px',
              boxShadow: '0 12px 32px rgba(0, 0, 0, 0.65)',
              color: '#e6edf3',
              overflow: 'hidden'
            }}
          >
            {/* Modal Header */}
            <div style={{
              display: 'flex',
              justifyContent: 'space-between',
              alignItems: 'center',
              padding: '10px 14px',
              borderBottom: '1px solid #30363d',
              backgroundColor: '#0d1117'
            }}>
              <div style={{ display: 'flex', alignItems: 'center', gap: '8px', fontSize: '0.82rem', fontWeight: 600 }}>
                <FileText size={15} color="#58a6ff" />
                <span>Export Hardware Trace (AI-Ready)</span>
              </div>
              <button
                onClick={() => setExportOpen(false)}
                title="Close (ESC)"
                style={{
                  background: 'transparent',
                  border: 'none',
                  color: '#8b949e',
                  cursor: 'pointer',
                  padding: '2px',
                  display: 'flex'
                }}
              >
                <X size={16} />
              </button>
            </div>

            {/* Modal Options Body */}
            <div style={{ padding: '12px 14px', overflowY: 'auto', display: 'flex', flexDirection: 'column', gap: '10px', fontSize: '0.73rem' }}>
              {/* Format */}
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px', flexWrap: 'wrap' }}>
                <span style={{ color: '#8b949e', fontWeight: 600 }}>Format</span>
                <div style={{ display: 'flex', gap: '4px' }}>
                  {[
                    { id: 'md', label: 'AI Markdown (.md)' },
                    { id: 'json', label: 'JSON (.json)' },
                    { id: 'csv', label: 'CSV (.csv)' }
                  ].map((opt) => (
                    <button
                      key={opt.id}
                      onClick={() => { setExportFormat(opt.id); setExportStatus(null); }}
                      style={{
                        backgroundColor: exportFormat === opt.id ? '#1f6feb' : '#21262d',
                        color: exportFormat === opt.id ? '#ffffff' : '#c9d1d9',
                        border: exportFormat === opt.id ? '1px solid #388bfd' : '1px solid #30363d',
                        padding: '4px 9px',
                        borderRadius: '4px',
                        cursor: 'pointer',
                        fontSize: '0.7rem',
                        fontWeight: exportFormat === opt.id ? 600 : 400
                      }}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              </div>

              {/* Signals */}
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px', flexWrap: 'wrap' }}>
                <span style={{ color: '#8b949e', fontWeight: 600 }}>Signals</span>
                <div style={{ display: 'flex', gap: '4px' }}>
                  <button
                    onClick={() => { setExportSignalsScope('visible'); setExportStatus(null); }}
                    style={{
                      backgroundColor: exportSignalsScope === 'visible' ? '#238636' : '#21262d',
                      color: '#ffffff',
                      border: '1px solid #30363d',
                      padding: '4px 9px',
                      borderRadius: '4px',
                      cursor: 'pointer',
                      fontSize: '0.7rem'
                    }}
                  >
                    Visible Only ({plottedKeys.length})
                  </button>
                  <button
                    onClick={() => { setExportSignalsScope('all'); setExportStatus(null); }}
                    style={{
                      backgroundColor: exportSignalsScope === 'all' ? '#238636' : '#21262d',
                      color: '#ffffff',
                      border: '1px solid #30363d',
                      padding: '4px 9px',
                      borderRadius: '4px',
                      cursor: 'pointer',
                      fontSize: '0.7rem'
                    }}
                  >
                    All Registers ({allRegisterItems.length})
                  </button>
                </div>
              </div>

              {/* Range */}
              <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px', flexWrap: 'wrap' }}>
                <span style={{ color: '#8b949e', fontWeight: 600 }}>Range</span>
                <div style={{ display: 'flex', gap: '4px' }}>
                  <button
                    onClick={() => { setExportRangeScope('span'); setExportStatus(null); }}
                    style={{
                      backgroundColor: exportRangeScope === 'span' ? '#388bfd' : '#21262d',
                      color: '#ffffff',
                      border: '1px solid #30363d',
                      padding: '4px 9px',
                      borderRadius: '4px',
                      cursor: 'pointer',
                      fontSize: '0.7rem'
                    }}
                  >
                    Current Span ({slicedHistoryData.length} pts{isTimeTravelActive ? ` @ #${timeTravelState.snapshot?.index}` : ''})
                  </button>
                  <button
                    onClick={() => { setExportRangeScope('all'); setExportStatus(null); }}
                    style={{
                      backgroundColor: exportRangeScope === 'all' ? '#388bfd' : '#21262d',
                      color: '#ffffff',
                      border: '1px solid #30363d',
                      padding: '4px 9px',
                      borderRadius: '4px',
                      cursor: 'pointer',
                      fontSize: '0.7rem'
                    }}
                  >
                    All Buffered ({historyData?.length || 0} pts)
                  </button>
                </div>
              </div>

              {/* Value Encoding */}
              {exportFormat !== 'json' && (
                <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px', flexWrap: 'wrap' }}>
                  <span style={{ color: '#8b949e', fontWeight: 600 }}>Values</span>
                  <div style={{ display: 'flex', gap: '4px' }}>
                    {[
                      { id: 'hex', label: 'Hex (0x...)' },
                      { id: 'signed', label: 'Signed' },
                      { id: 'both', label: 'Hex + Signed' }
                    ].map((opt) => (
                      <button
                        key={opt.id}
                        onClick={() => { setExportValueFormat(opt.id); setExportStatus(null); }}
                        style={{
                          backgroundColor: exportValueFormat === opt.id ? '#30363d' : '#21262d',
                          color: exportValueFormat === opt.id ? '#ffffff' : '#8b949e',
                          border: exportValueFormat === opt.id ? '1px solid #58a6ff' : '1px solid #30363d',
                          padding: '4px 9px',
                          borderRadius: '4px',
                          cursor: 'pointer',
                          fontSize: '0.7rem'
                        }}
                      >
                        {opt.label}
                      </button>
                    ))}
                  </div>
                </div>
              )}

              {/* Preview Box */}
              {exportPayload && (
                <div style={{ display: 'flex', flexDirection: 'column', gap: '4px' }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', color: '#8b949e', fontSize: '0.67rem' }}>
                    <span>Preview ({exportPayload.fileName})</span>
                    <span>
                      {exportPayload.rowCount} rows × {exportPayload.colCount} signals (~{(exportPayload.content.length / 1024).toFixed(1)} KB)
                    </span>
                  </div>
                  <pre style={{
                    margin: 0,
                    padding: '8px',
                    backgroundColor: '#0d1117',
                    border: '1px solid #30363d',
                    borderRadius: '6px',
                    fontSize: '0.66rem',
                    fontFamily: 'monospace',
                    color: '#c9d1d9',
                    maxHeight: '150px',
                    overflow: 'auto',
                    whiteSpace: 'pre'
                  }}>
                    {exportPayload.content.split('\n').slice(0, 18).join('\n')}
                    {exportPayload.content.split('\n').length > 18 ? '\n...' : ''}
                  </pre>
                </div>
              )}

              {/* Status Banner */}
              {exportStatus && (
                <div style={{
                  padding: '8px 10px',
                  borderRadius: '6px',
                  backgroundColor: exportStatus.type === 'error' ? 'rgba(248, 81, 73, 0.12)' : 'rgba(63, 185, 80, 0.12)',
                  border: `1px solid ${exportStatus.type === 'error' ? '#f85149' : '#238636'}`,
                  color: exportStatus.type === 'error' ? '#f85149' : '#3fb950',
                  fontSize: '0.71rem',
                  display: 'flex',
                  flexDirection: 'column',
                  gap: '4px'
                }}>
                  <span>{exportStatus.message}</span>
                  {exportStatus.path && (
                    <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '8px' }}>
                      <code style={{ color: '#e6edf3', fontSize: '0.68rem', wordBreak: 'break-all' }}>{exportStatus.path}</code>
                      <button
                        onClick={async () => {
                          if (await copyTextSafe(exportStatus.path)) setCopiedPath(true);
                        }}
                        style={{
                          backgroundColor: '#21262d',
                          color: copiedPath ? '#3fb950' : '#c9d1d9',
                          border: '1px solid #30363d',
                          borderRadius: '4px',
                          padding: '2px 7px',
                          fontSize: '0.66rem',
                          cursor: 'pointer',
                          flexShrink: 0,
                          display: 'flex',
                          alignItems: 'center',
                          gap: '3px'
                        }}
                      >
                        {copiedPath ? <Check size={11} /> : <Copy size={11} />}
                        <span>{copiedPath ? 'Copied Path' : 'Copy Path'}</span>
                      </button>
                    </div>
                  )}
                </div>
              )}
            </div>

            {/* Modal Action Footer */}
            <div style={{
              display: 'flex',
              justifyContent: 'flex-end',
              alignItems: 'center',
              gap: '8px',
              padding: '10px 14px',
              borderTop: '1px solid #30363d',
              backgroundColor: '#0d1117',
              flexWrap: 'wrap'
            }}>
              <button
                onClick={handleCopyExport}
                disabled={!exportPayload || exportPayload.colCount === 0}
                style={{
                  backgroundColor: '#21262d',
                  color: '#c9d1d9',
                  border: '1px solid #30363d',
                  padding: '5px 11px',
                  borderRadius: '5px',
                  fontSize: '0.72rem',
                  cursor: (!exportPayload || exportPayload.colCount === 0) ? 'not-allowed' : 'pointer',
                  display: 'flex',
                  alignItems: 'center',
                  gap: '5px',
                  fontWeight: 600
                }}
              >
                <Copy size={13} />
                <span>Copy</span>
              </button>
              <button
                onClick={handleDownloadExport}
                disabled={!exportPayload || exportPayload.colCount === 0}
                style={{
                  backgroundColor: '#21262d',
                  color: '#58a6ff',
                  border: '1px solid #30363d',
                  padding: '5px 11px',
                  borderRadius: '5px',
                  fontSize: '0.72rem',
                  cursor: (!exportPayload || exportPayload.colCount === 0) ? 'not-allowed' : 'pointer',
                  display: 'flex',
                  alignItems: 'center',
                  gap: '5px',
                  fontWeight: 600
                }}
              >
                <Download size={13} />
                <span>Download</span>
              </button>
              <button
                onClick={handleSaveToWorkspace}
                disabled={exportBusy || !exportPayload || exportPayload.colCount === 0}
                style={{
                  backgroundColor: (exportBusy || !exportPayload || exportPayload.colCount === 0) ? '#30363d' : '#238636',
                  color: '#ffffff',
                  border: 'none',
                  padding: '5px 12px',
                  borderRadius: '5px',
                  fontSize: '0.72rem',
                  cursor: (exportBusy || !exportPayload || exportPayload.colCount === 0) ? 'not-allowed' : 'pointer',
                  display: 'flex',
                  alignItems: 'center',
                  gap: '5px',
                  fontWeight: 600
                }}
              >
                <Save size={13} />
                <span>{exportBusy ? 'Saving...' : `Save to F-BB Root (${exportPayload?.fileName || 'fbb_trace.md'})`}</span>
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
};

export default memo(RegisterTracer);
