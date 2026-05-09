const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const fs = require('fs');
const path = require('path');
const cors = require('cors');

const app = express();
app.use(cors());
const server = http.createServer(app);
const io = new Server(server, {
    cors: { origin: "*" }
});

const MANIFEST_PATH = path.join(__dirname, 'data/board_manifest.json');
let manifest = {};
let shmBuffer = null;

// マニフェストの読み込み
function loadManifest() {
    try {
        if (fs.existsSync(MANIFEST_PATH)) {
            const data = fs.readFileSync(MANIFEST_PATH, 'utf8');
            manifest = JSON.parse(data);
            console.log(`[Backend] Manifest loaded: ${manifest.board}`);
            return true;
        }
    } catch (e) {
        console.error(`[Backend] Failed to load manifest: ${e.message}`);
    }
    return false;
}

// 共有メモリの読み取り
function updateShm() {
    if (!manifest.shm_path) return;
    try {
        if (fs.existsSync(manifest.shm_path)) {
            const stats = fs.statSync(manifest.shm_path);
            if (stats.size > 0) {
                shmBuffer = fs.readFileSync(manifest.shm_path);
                broadcastRegisters();
            }
        }
    } catch (e) {
        // 静かに無視（ファイルがまだ作成されていない場合など）
    }
}

// レジスタ情報のブロードキャスト
function broadcastRegisters() {
    if (!shmBuffer || !manifest.devices) return;
    
    // SHMベースアドレス: 全UIO/GPIOデバイスの最小ベースアドレス
    const uioGpioDevs = manifest.devices.filter(d => d.type === 'uio' || d.type === 'gpio');
    if (uioGpioDevs.length === 0) return;
    const shmBaseAddr = Math.min(...uioGpioDevs.map(d => d.base_addr || 0));
    
    const regData = [];
    uioGpioDevs.forEach(dev => {
        const devBaseAddr = dev.base_addr || 0;
        dev.registers.forEach(reg => {
            const regOffset = parseInt(reg.offset, 0);
            const physAddr = devBaseAddr + regOffset;
            const shmOffset = physAddr - shmBaseAddr;
            if (shmOffset >= 0 && shmOffset + 4 <= shmBuffer.length) {
                const value = shmBuffer.readUInt32LE(shmOffset);
                regData.push({
                    name: reg.name,
                    offset: reg.offset,
                    value: `0x${value.toString(16).padStart(8, '0')}`,
                    decimal: value,
                    deviceName: dev.name
                });
            }
        });
    });
    io.emit('registers', regData);
}

// 定期実行
setInterval(() => {
    loadManifest();
    updateShm();
}, 200); // 200ms間隔で更新

const net = require('net');

const UART_MAP_PATH = path.join(__dirname, 'data/uart_map.json');
let uartConnections = {}; // name -> net.Socket
let uartLogs = {}; // name -> string (last 1000 chars)

// UARTマクロの定義（拡張可能）
const UART_MACROS = [
    { pattern: /login:/i, response: 'root\n', delay: 500 },
    { pattern: /password:/i, response: 'vfpga\n', delay: 500 }
];

// UART接続の同期
function syncUartConnections() {
    try {
        if (fs.existsSync(UART_MAP_PATH)) {
            const mapping = JSON.parse(fs.readFileSync(UART_MAP_PATH, 'utf8'));
            for (const [name, port] of Object.entries(mapping)) {
                if (!uartConnections[name]) {
                    uartConnections[name] = 'connecting'; // 予約
                    connectToUart(name, port);
                }
            }
        }
    } catch (e) {}
}

function connectToUart(name, port) {
    console.log(`[Backend] Connecting to UART bridge: ${name} on port ${port}`);
    const client = new net.Socket();
    
    client.connect(port, '127.0.0.1', () => {
        console.log(`[Backend] UART ${name} connected`);
        uartConnections[name] = client;
        uartLogs[name] = "";
    });

    client.on('data', (data) => {
        const text = data.toString('utf8');
        uartLogs[name] = (uartLogs[name] + text).slice(-5000);
        io.emit('uart-data', { name, text });
        
        // マクロチェック
        UART_MACROS.forEach(macro => {
            if (macro.pattern.test(text)) {
                console.log(`[Macro] Pattern detected in ${name}: ${macro.pattern}. Responding in ${macro.delay}ms`);
                setTimeout(() => {
                    if (uartConnections[name]) uartConnections[name].write(macro.response);
                }, macro.delay);
            }
        });
    });

    client.on('close', () => {
        console.log(`[Backend] UART ${name} disconnected`);
        delete uartConnections[name];
    });

    client.on('error', (err) => {
        console.error(`[Backend] UART ${name} error: ${err.message}`);
    });
}

// Socket.io通信の設定
io.on('connection', (socket) => {
    console.log('[Backend] Frontend client connected');
    
    // 接続時に既存のログを送信
    socket.emit('uart-init', uartLogs);

    socket.on('uart-send', ({ name, text }) => {
        if (uartConnections[name]) {
            uartConnections[name].write(text);
        }
    });

    // GPIO Injection Handler
    socket.on('gpio-inject', ({ deviceName, bitIndex, value, dataRegName = 'DATA' }) => {
        if (!shmBuffer || !manifest.devices) return;
        const dev = manifest.devices.find(d => d.name === deviceName);
        if (!dev) return;

        // Find specific DATA register offset
        const dataReg = dev.registers.find(r => r.name === dataRegName);
        if (!dataReg) return;

        const uioGpioDevs = manifest.devices.filter(d => d.type === 'uio' || d.type === 'gpio');
        const shmBaseAddr = Math.min(...uioGpioDevs.map(d => d.base_addr || 0));
        const physAddr = (dev.base_addr || 0) + parseInt(dataReg.offset, 0);
        const shmOffset = physAddr - shmBaseAddr;

        if (shmOffset >= 0 && shmOffset + 4 <= shmBuffer.length) {
            let currentVal = shmBuffer.readUInt32LE(shmOffset);
            if (value) {
                currentVal |= (1 << bitIndex);
            } else {
                currentVal &= ~(1 << bitIndex);
            }
            shmBuffer.writeUInt32LE(currentVal, shmOffset);
            
            // Write back to file immediately
            try {
                fs.writeFileSync(manifest.shm_path, shmBuffer);
            } catch (e) {
                console.error(`[Backend] Failed to write SHM: ${e.message}`);
            }
        }
    });
});

// 定期実行の更新
setInterval(() => {
    loadManifest();
    updateShm();
    syncUartConnections();
}, 200);

// API
app.get('/api/manifest', (req, res) => res.json(manifest));
app.get('/api/uart/logs', (req, res) => res.json(uartLogs));

app.use(express.static(path.join(__dirname, 'client/dist')));

const PORT = process.env.PORT || 8080;
server.listen(PORT, () => {
    console.log(`[Backend] Dashboard Server running on http://localhost:${PORT}`);
});
