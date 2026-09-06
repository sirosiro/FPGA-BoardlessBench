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

            // =========================================================================
            // [CRITICAL SECTION] GPIO 方向・データレジスタ判定ロジック (Regression Guard)
            // =========================================================================
            // @intent:responsibility
            //   DTS/マニフェストから取得したレジスタ定義に基づき、方向レジスタ(dirReg)、
            //   出力レジスタ(dataOutReg)、入力レジスタ(dataInReg)をSoC非依存に正確に分離・特定する。
            // @intent:historical-context (過去3回のエンバグの教訓)
            //   1. 最初期: NXPのレジスタ名 'PDIR'/'PDOR' をハードコードしていたため汎用性に欠けていた。
            //   2. 汎用化リファクタ時: r.name.includes('IN') に変更したが、'PDIR' には連続した 'IN' が
            //      含まれずマッチ失敗。dataInReg が常に dataOutReg(PDOR) にフォールバックする潜在バグが発生。
            //   3. 直前の修正: サーバー側の全SHM一括上書きバグをアトミックな4バイト局所書き込みへ改善した際、
            //      クライアントが誤って指定していた PDOR(出力) にのみ書き込まれ、FWがポーリングする PDIR(入力) に
            //      値が渡らない問題として顕在化した。
            // @intent:invariant
            //   - 単一レジスタ構成 (Zynq等): DATA @ 0x00 のみ存在。dataOutReg と dataInReg は同一レジスタを参照。
            //   - 分離レジスタ構成 (i.MX95, STM32等): 出力(PDOR/ODR/DOUT) と 入力(PDIR/IDR/DIN) が物理的に分離。
            //   - Single Source of Truth: DTSの論理名 (DATA_IN / DATA_OUT / INV_TRI) を最優先とし、
            //     フォールバックとして業界標準の命名規則 (IDR/ODR, DIN/DOUT, *IN*/*OUT*) を評価すること。
            //   - 入力トグル操作時 (handleGpioToggle): 必ず dataInReg.name を引数として渡すこと。
            // =========================================================================
            const dirReg = devRegs.find(r => 
              Boolean(r.direction_mode) || 
              (r.name || '').toUpperCase().includes('TRI') || 
              (r.logical_name || '').toUpperCase().includes('TRI')
            );

            const dataRegs = devRegs.filter(r => (r.logical_name || r.name).toUpperCase().includes('DATA') || r.name.toUpperCase().includes('DR'));
            let dataOutReg = dataRegs.find(r => 
              (r.logical_name || '').toUpperCase().includes('OUT') || 
              (r.name || '').toUpperCase().includes('OUT') || 
              (r.name || '').toUpperCase().includes('ODR') ||
              (r.name || '').toUpperCase().includes('DOUT')
            ) || dataRegs[0] || devRegs[0];

            let dataInReg = dataRegs.find(r => 
              (r.logical_name || '').toUpperCase().includes('IN') || 
              (r.name || '').toUpperCase().includes('IN') || 
              (r.name || '').toUpperCase().includes('IDR') || 
              (r.name || '').toUpperCase().includes('DIN')
            ) || dataOutReg;

            const dirVal = dirReg?.decimal || 0;
            const dataOutVal = dataOutReg?.decimal || 0;
            const dataInVal = dataInReg?.decimal || 0;

            const labelName = isHub75 ? `${dev.name} (HUB75E Pins)` : dev.name;
            const totalPins = dev.pin_count || dev.pins || 16;

            return (
              <div key={`gpio-${i}`} className="gpio-dev-group" style={{ marginBottom: '1rem' }}>
                <div className="gpio-dev-label" style={{ fontWeight: 600, fontSize: '0.85rem', color: '#58a6ff', marginBottom: '0.5rem' }}>{labelName}</div>
                <div className="gpio-grid">
                  {Array.from({ length: totalPins }).map((_, bitIndex) => {
                    let isInput = false;
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
                        onClick={() => handleGpioToggle(dev.name, bitIndex, isOn, dataInReg?.name || 'PDIR')}
                        style={{ cursor: 'pointer' }}
                        title={`${labelName} ${pinLabel} (Bit ${bitIndex})`}
                      >
                        <div className="gpio-indicator" style={{ pointerEvents: 'none' }}></div>
                        <span className="gpio-label" style={{ pointerEvents: 'none' }}>{pinLabel}</span>
                      </div>
                    );
                  })}
                </div>
              </div>
            );
          })
        )}
      </div>
    </div>
  );
}

export default GpioPanel;
