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
            const totalPins = dev.pin_count || dev.pins || 16;

            // =========================================================================
            // [CRITICAL SECTION] GPIO チャネル分離・方向・データレジスタ判定ロジック (Regression Guard)
            // =========================================================================
            // @intent:responsibility
            //   DTS/マニフェストから取得したレジスタ定義に基づき、単一チャネル、入出力分離型、
            //   およびデュアル/マルチチャネル構成（AXI GPIO等）をSoC非依存に正確に判別・グループ化する。
            // @intent:historical-context (過去4回のエンバグの教訓)
            //   1. 最初期: NXPのレジスタ名 'PDIR'/'PDOR' をハードコードしていたため汎用性に欠けていた。
            //   2. 汎用化リファクタ時: r.name.includes('IN') に変更したが、'PDIR' には連続した 'IN' が
            //      含まれずマッチ失敗。dataInReg が常に dataOutReg(PDOR) にフォールバックする潜在バグが発生。
            //   3. 直前の修正: サーバー側の全SHM一括上書きバグをアトミックな4バイト局所書き込みへ改善した際、
            //      クライアントが誤って指定していた PDOR(出力) にのみ書き込まれ、FWがポーリングする PDIR(入力) に
            //      値が渡らない問題として顕在化した。
            //   4. デュアルチャネル対応の復元: AXI GPIO (DATA/TRI, DATA2/TRI2) のように複数の独立した入出力チャネル
            //      を持つ構成において、第2チャネルが単一チャネルのPDOR/PDIRペアと誤認されて消失していた問題を解消。
            // @intent:invariant
            //   - 単一レジスタ構成 (Zynq等): DATA @ 0x00 のみ存在。dataOutReg と dataInReg は同一レジスタを参照。
            //   - 分離レジスタ構成 (i.MX95, STM32等): 出力(PDOR/ODR/DOUT) と 入力(PDIR/IDR/DIN) が物理的に分離。
            //   - デュアル/マルチチャネル構成 (AXI GPIO等): DATA/TRI (Ch1) と DATA2/TRI2 (Ch2) を独立チャネルとして展開。
            //   - Single Source of Truth: DTSの論理名 (DATA_IN / DATA_OUT / INV_TRI) を最優先とし、
            //     フォールバックとして業界標準の命名規則 (IDR/ODR, DIN/DOUT, *IN*/*OUT*) を評価すること。
            //   - 入力トグル操作時 (handleGpioToggle): 必ず dataInReg.name を引数として渡すこと。
            // =========================================================================

            // Group registers into channels based on channel identifiers (e.g. DATA vs DATA2, TRI vs TRI2)
            const channelMap = new Map();
            if (devRegs.length === 0) {
              channelMap.set('1', []);
            } else {
              devRegs.forEach(reg => {
                const rawName = reg.name || '';
                const match = rawName.match(/(?:GPIO|CH|DATA|TRI|DR)(\d+)/i) || rawName.match(/(\d+)$/);
                const chNum = match ? match[1] : '1';
                if (!channelMap.has(chNum)) {
                  channelMap.set(chNum, []);
                }
                channelMap.get(chNum).push(reg);
              });
            }

            const sortedChannelKeys = Array.from(channelMap.keys()).sort((a, b) => parseInt(a, 10) - parseInt(b, 10));
            const totalChannels = sortedChannelKeys.length;

            return sortedChannelKeys.map(chKey => {
              const chRegs = channelMap.get(chKey);

              const dirReg = chRegs.find(r => 
                Boolean(r.direction_mode) || 
                (r.name || '').toUpperCase().includes('TRI') || 
                (r.logical_name || '').toUpperCase().includes('TRI')
              );

              const dataRegs = chRegs.filter(r => 
                (r.logical_name || r.name).toUpperCase().includes('DATA') || 
                r.name.toUpperCase().includes('DR')
              );

              let dataOutReg = dataRegs.find(r => 
                (r.logical_name || '').toUpperCase().includes('OUT') || 
                (r.name || '').toUpperCase().includes('OUT') || 
                (r.name || '').toUpperCase().includes('ODR') ||
                (r.name || '').toUpperCase().includes('DOUT')
              ) || dataRegs[0] || chRegs[0];

              let dataInReg = dataRegs.find(r => 
                (r.logical_name || '').toUpperCase().includes('IN') || 
                (r.name || '').toUpperCase().includes('IN') || 
                (r.name || '').toUpperCase().includes('IDR') || 
                (r.name || '').toUpperCase().includes('DIN')
              ) || dataOutReg;

              const dirVal = dirReg?.decimal || 0;
              const dataOutVal = dataOutReg?.decimal || 0;
              const dataInVal = dataInReg?.decimal || 0;

              const baseLabel = isHub75 ? `${dev.name} (HUB75E Pins)` : dev.name;
              const labelName = totalChannels > 1
                ? `${baseLabel} (Channel ${chKey}: ${dataOutReg?.name || `CH${chKey}`})`
                : baseLabel;

              return (
                <div key={`gpio-${i}-ch-${chKey}`} className="gpio-dev-group" style={{ marginBottom: '1rem' }}>
                  <div className="gpio-dev-label" style={{ fontWeight: 600, fontSize: '0.85rem', color: '#58a6ff', marginBottom: '0.5rem' }}>{labelName}</div>
                  <div className="gpio-grid">
                    {Array.from({ length: totalPins }).map((_, bitIndex) => {
                      let isInput;
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
                          onClick={() => isInput && handleGpioToggle(dev.name, bitIndex, isOn, dataInReg?.name || 'DATA')}
                          style={{ cursor: isInput ? 'pointer' : 'default' }}
                          title={`${labelName} ${pinLabel} (Bit ${bitIndex}) - ${isInput ? 'Input (Click to toggle)' : 'Output (LED)'}`}
                        >
                          <div className="gpio-indicator" style={{ pointerEvents: 'none' }}></div>
                          <span className="gpio-label" style={{ pointerEvents: 'none' }}>{pinLabel}</span>
                        </div>
                      );
                    })}
                  </div>
                </div>
              );
            });
          })
        )}
      </div>
    </div>
  );
}

export default GpioPanel;
