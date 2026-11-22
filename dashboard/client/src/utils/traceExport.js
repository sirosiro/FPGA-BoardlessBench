// Utility helpers for formatting and exporting Register State Tracer history
// in AI-ready Markdown, lossless JSON, and CSV formats.

export const toInt32 = (val) => {
  if (val == null) return 0;
  const n = typeof val === 'number' ? val : Number(val);
  if (Number.isNaN(n)) return 0;
  return (n & 0x80000000) ? n - 0x100000000 : n;
};

export function formatHex32(val, padWidth = 8) {
  const u = Number(val) >>> 0;
  return `0x${u.toString(16).toUpperCase().padStart(padWidth, '0')}`;
}

export function formatExportValue(val, valueFormat = 'both', padWidth = 8) {
  if (val === null || val === undefined || Number.isNaN(Number(val))) return '';
  if (valueFormat === 'hex') {
    return formatHex32(val, padWidth);
  }
  if (valueFormat === 'signed') {
    return String(toInt32(val));
  }
  return `${formatHex32(val, padWidth)} (${toInt32(val)})`;
}

function escapeCsvCell(raw) {
  const s = raw == null ? '' : String(raw);
  if (s.includes(',') || s.includes('"') || s.includes('\n')) {
    return `"${s.replace(/"/g, '""')}"`;
  }
  return s;
}

function buildUniqueColumnLabels(signals) {
  const nameCounts = {};
  signals.forEach((sig) => {
    const base = sig.name || sig.key;
    nameCounts[base] = (nameCounts[base] || 0) + 1;
  });
  return signals.map((sig) => {
    const base = sig.name || sig.key;
    if (nameCounts[base] > 1 && sig.deviceName) {
      return `${sig.deviceName}.${base}`;
    }
    return base;
  });
}

function computeSignalStats(samples, sigKey, padWidth) {
  let minUnsigned = null;
  let maxUnsigned = null;
  let minSigned = null;
  let maxSigned = null;
  let sumSigned = 0;
  let count = 0;
  let changes = 0;
  let prevRaw = undefined;
  let firstVal = null;
  let lastVal = null;
  const uniqueSet = new Set();

  for (let i = 0; i < samples.length; i++) {
    const sn = samples[i];
    const raw = sn[sigKey];
    if (raw === null || raw === undefined || Number.isNaN(Number(raw))) continue;
    const u = Number(raw) >>> 0;
    const s = toInt32(u);

    if (firstVal === null) firstVal = { index: sn.index, u, s };
    lastVal = { index: sn.index, u, s };

    if (minUnsigned === null || u < minUnsigned.u) minUnsigned = { index: sn.index, u, s };
    if (maxUnsigned === null || u > maxUnsigned.u) maxUnsigned = { index: sn.index, u, s };
    if (minSigned === null || s < minSigned.s) minSigned = { index: sn.index, u, s };
    if (maxSigned === null || s > maxSigned.s) maxSigned = { index: sn.index, u, s };

    sumSigned += s;
    count++;
    if (uniqueSet.size <= 12) uniqueSet.add(u);
    if (prevRaw !== undefined && prevRaw !== u) changes++;
    prevRaw = u;
  }

  return {
    count,
    changes,
    uniqueCount: uniqueSet.size,
    uniqueSampleHex: Array.from(uniqueSet).slice(0, 6).map(v => formatHex32(v, padWidth)),
    minUnsigned,
    maxUnsigned,
    minSigned,
    maxSigned,
    meanSigned: count > 0 ? sumSigned / count : 0,
    firstVal,
    lastVal,
    padWidth
  };
}

function determineHexPadWidth(samples, sigKey) {
  for (let i = 0; i < samples.length; i++) {
    const raw = samples[i][sigKey];
    if (raw != null && (Number(raw) >>> 0) > 0xFFFF) {
      return 8;
    }
  }
  return 4;
}

function extractScenarioName(manifest) {
  if (!manifest?.scenario_dir) return 'active_scenario';
  const parts = String(manifest.scenario_dir).replace(/\/+$/, '').split('/').filter(Boolean);
  if (parts.length === 0) return 'active_scenario';
  const last = parts[parts.length - 1];
  if (last === 'scenario' && parts.length >= 2) {
    return `${parts[parts.length - 2]}/${last}`;
  }
  return last;
}

function pickKeyTelemetryEvents(samples, timeTravelIndex) {
  const out = [];
  let prevSig = null;
  for (let i = 0; i < samples.length; i++) {
    const sn = samples[i];
    if (!sn.telemetry || typeof sn.telemetry !== 'object') continue;
    const tlmJson = JSON.stringify(sn.telemetry);
    const isEdge = i === 0 || i === samples.length - 1 || sn.index === timeTravelIndex;
    if (isEdge || tlmJson !== prevSig) {
      out.push({ index: sn.index, time: sn.time, telemetry: sn.telemetry });
      prevSig = tlmJson;
    }
  }
  if (out.length <= 16) return out;
  const sampled = [out[0]];
  const step = (out.length - 1) / 14;
  for (let k = 1; k < 14; k++) {
    sampled.push(out[Math.round(k * step)]);
  }
  sampled.push(out[out.length - 1]);
  return sampled;
}

export function buildTraceExportPayload({
  format = 'md',
  samples = [],
  totalBufferedCount = 0,
  signals = [],
  regMetaMap = {},
  manifest = null,
  timeTravelState = null,
  valueFormat = 'both',
  scopeSignals = 'visible',
  scopeRange = 'span',
  selectedGroup = 'ALL',
  tracerMode = 'zero_center',
  timebaseSpan = 'all'
}) {
  const enrichedSignals = signals.map((sig) => {
    const meta = regMetaMap[sig.key] || {};
    return {
      key: sig.key,
      name: sig.name || meta.name || sig.key,
      logicalName: meta.logical_name || meta.name || sig.name || sig.key,
      deviceName: meta.deviceName || '',
      group: sig.group || meta.group || 'General',
      offset: sig.offset || meta.offset || '',
      directionMode: meta.direction_mode || null,
      padWidth: determineHexPadWidth(samples, sig.key)
    };
  });

  const colLabels = buildUniqueColumnLabels(enrichedSignals);
  const scenarioName = extractScenarioName(manifest);
  const timeTravelIndex = !timeTravelState?.isLive && timeTravelState?.snapshot
    ? timeTravelState.snapshot.index
    : null;
  const exportedAt = new Date().toISOString();

  if (format === 'csv') {
    const header = ['index', 'time', ...colLabels].map(escapeCsvCell).join(',');
    const rows = samples.map((sn) => [
      sn.index ?? '',
      sn.time ?? '',
      ...enrichedSignals.map((sig) => formatExportValue(sn[sig.key], valueFormat, sig.padWidth))
    ].map(escapeCsvCell).join(','));
    const content = [header, ...rows].join('\n') + '\n';
    return { format: 'csv', fileName: 'fbb_trace.csv', content, rowCount: samples.length, colCount: enrichedSignals.length };
  }

  if (format === 'json') {
    const payload = {
      exportedAt,
      scenario: scenarioName,
      target: manifest?.model || manifest?.compatible || null,
      tracerContext: {
        selectedGroup,
        tracerMode,
        timebaseSpan,
        scopeSignals,
        scopeRange,
        valueFormat,
        timeTravelIndex,
        totalBufferedCount,
        exportedCount: samples.length
      },
      signals: enrichedSignals.map((sig, i) => ({
        column: colLabels[i],
        key: sig.key,
        device: sig.deviceName,
        name: sig.name,
        logicalName: sig.logicalName,
        group: sig.group,
        offset: sig.offset,
        directionMode: sig.directionMode
      })),
      samples: samples.map((sn) => {
        const values = {};
        enrichedSignals.forEach((sig, i) => {
          const raw = sn[sig.key];
          values[colLabels[i]] = raw == null ? null : (Number(raw) >>> 0);
        });
        const entry = { index: sn.index, time: sn.time, values };
        if (sn.telemetry !== undefined && sn.telemetry !== null) {
          entry.telemetry = sn.telemetry;
        }
        return entry;
      })
    };
    const content = JSON.stringify(payload, null, 2) + '\n';
    return { format: 'json', fileName: 'fbb_trace.json', content, rowCount: samples.length, colCount: enrichedSignals.length };
  }

  // Default: AI-Ready Markdown with embedded CSV table
  const lines = [];
  lines.push('# F-BB Hardware Register Trace Export');
  lines.push('');
  lines.push(`- **Exported At**: ${exportedAt}`);
  lines.push(`- **Scenario**: ${scenarioName}`);
  if (manifest?.model || manifest?.compatible) {
    lines.push(`- **Target / Board**: ${manifest.model || manifest.compatible}`);
  }
  lines.push(`- **Frames**: ${samples.length} exported (of ${totalBufferedCount} buffered) | **Range**: ${scopeRange === 'span' ? `Current Span (${timebaseSpan})` : 'All Buffered'}`);
  lines.push(`- **Signals**: ${enrichedSignals.length} (${scopeSignals === 'visible' ? `Visible in group "${selectedGroup || 'ALL'}"` : 'All registers'}) | **Value Format**: ${valueFormat}`);
  if (timeTravelIndex !== null) {
    lines.push(`- **Time-Travel Cursor**: Frame #${timeTravelIndex} (${timeTravelState.snapshot?.time || ''})`);
  }

  lines.push('');
  lines.push('## 1. Signal Metadata (Board Manifest / DTS)');
  if (enrichedSignals.length === 0) {
    lines.push('_No signals selected._');
  } else {
    lines.push('| Column | Device | Group | Offset | Logical Name / Mode |');
    lines.push('| --- | --- | --- | --- | --- |');
    enrichedSignals.forEach((sig, i) => {
      const extra = [sig.logicalName !== sig.name ? sig.logicalName : null, sig.directionMode].filter(Boolean).join(' / ');
      lines.push(`| ${colLabels[i]} | ${sig.deviceName || '-'} | ${sig.group} | ${sig.offset || '-'} | ${extra || '-'} |`);
    });
  }

  lines.push('');
  lines.push('## 2. Window Statistics');
  enrichedSignals.forEach((sig, i) => {
    const st = computeSignalStats(samples, sig.key, sig.padWidth);
    const label = colLabels[i];
    if (st.count === 0) {
      lines.push(`- **${label}**: no samples`);
      return;
    }
    if (valueFormat === 'hex') {
      const uniqInfo = st.uniqueCount <= 8 ? `unique=[${st.uniqueSampleHex.join(', ')}]` : `${st.uniqueCount}+ distinct values`;
      lines.push(`- **${label}**: start=${formatHex32(st.firstVal.u, sig.padWidth)}, end=${formatHex32(st.lastVal.u, sig.padWidth)}, min=${formatHex32(st.minUnsigned.u, sig.padWidth)} (#${st.minUnsigned.index}), max=${formatHex32(st.maxUnsigned.u, sig.padWidth)} (#${st.maxUnsigned.index}), changes=${st.changes}, ${uniqInfo}`);
    } else {
      const minStr = formatExportValue(st.minSigned.u, valueFormat, sig.padWidth);
      const maxStr = formatExportValue(st.maxSigned.u, valueFormat, sig.padWidth);
      lines.push(`- **${label}**: start=${formatExportValue(st.firstVal.u, valueFormat, sig.padWidth)} -> end=${formatExportValue(st.lastVal.u, valueFormat, sig.padWidth)}, min=${minStr} (#${st.minSigned.index}), max=${maxStr} (#${st.maxSigned.index}), mean=${st.meanSigned.toFixed(2)}, changes=${st.changes}`);
    }
  });

  lines.push('');
  lines.push('## 3. Time-Series Samples (CSV)');
  lines.push('```csv');
  lines.push(['index', 'time', ...colLabels].map(escapeCsvCell).join(','));
  samples.forEach((sn) => {
    const row = [
      sn.index ?? '',
      sn.time ?? '',
      ...enrichedSignals.map((sig) => formatExportValue(sn[sig.key], valueFormat, sig.padWidth))
    ];
    lines.push(row.map(escapeCsvCell).join(','));
  });
  lines.push('```');

  const telemetryEvents = pickKeyTelemetryEvents(samples, timeTravelIndex);
  if (telemetryEvents.length > 0) {
    lines.push('');
    lines.push(`## 4. Attached Scenario Telemetry Snapshots (${telemetryEvents.length} distinct points)`);
    lines.push('```json');
    lines.push(JSON.stringify(telemetryEvents, null, 2));
    lines.push('```');
  }

  const content = lines.join('\n') + '\n';
  return { format: 'md', fileName: 'fbb_trace.md', content, rowCount: samples.length, colCount: enrichedSignals.length };
}
