const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const fs = require('fs');
const path = require('path');
const cors = require('cors');
const chokidar = require('chokidar');


const app = express();
app.use(cors());
app.use(express.json()); // @intent:rationale フロントエンドから送信されるレイアウト用のJSONデータを受信・解析するためにBody Parserを有効化します。
const server = http.createServer(app);
const io = new Server(server, {
    cors: { origin: "*" }
});

const MANIFEST_PATH = path.join(__dirname, 'data/board_manifest.json');
let manifest = {};
let shmBuffer = null;
let hdmiWatcher = null;

function setupHdmiWatcher() {
    if (hdmiWatcher) {
        hdmiWatcher.close();
    }
    const hdmiPath = manifest.hdmi_output_path || '/tmp/hdmi_output.bmp';

    hdmiWatcher = chokidar.watch(hdmiPath, {
        persistent: true,
        usePolling: true,
        interval: 30
    });

    const sendHdmiFrame = () => {
        fs.readFile(hdmiPath, (err, data) => {
            if (!err) {
                io.emit('hdmi-frame', data.toString('base64'));
            }
        });
    };

    hdmiWatcher.on('add', sendHdmiFrame);
    hdmiWatcher.on('change', sendHdmiFrame);
}


// Register State Tracer 状態
let traceHistory = [];
let lastRegState = null;
let traceIndex = 0;
const MAX_TRACE_HISTORY = 500;

// マニフェストの読み込み
function loadManifest() {
    try {
        if (fs.existsSync(MANIFEST_PATH)) {
            const data = fs.readFileSync(MANIFEST_PATH, 'utf8');
            const newManifest = JSON.parse(data);
            const pathChanged = newManifest.hdmi_output_path !== manifest.hdmi_output_path;
            manifest = newManifest;
            if (pathChanged) {
                setupHdmiWatcher();
            }
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
        console.error("[Backend] updateShm error:", e.message);
    }
}

// レジスタ情報のブロードキャスト
function broadcastRegisters() {
    if (!shmBuffer || !manifest.devices) return;
    
    const uioGpioDevs = manifest.devices.filter(d => d.type === 'uio' || d.type === 'gpio');
    if (uioGpioDevs.length === 0) {
        console.log("[Backend Debug] uioGpioDevs is empty!");
        return;
    }
    const shmBaseAddr = Math.min(...uioGpioDevs.map(d => d.base_addr || 0));
    
    const regData = [];
    const currentState = {}; 

    uioGpioDevs.forEach(dev => {
        const devBaseAddr = dev.base_addr || 0;
        dev.registers.forEach(reg => {
            const regOffset = parseInt(reg.offset, 0);
            const physAddr = devBaseAddr + regOffset;
            const shmOffset = physAddr - shmBaseAddr;
            if (shmOffset >= 0 && shmOffset + 4 <= shmBuffer.length) {
                const value = shmBuffer.readUInt32LE(shmOffset);
                const regKey = `${dev.name}_${reg.name}`;
                
                regData.push({
                    name: reg.name,
                    logical_name: reg.logical_name || reg.name,
                    offset: reg.offset,
                    value: `0x${value.toString(16).padStart(8, '0')}`,
                    decimal: value,
                    deviceName: dev.name
                });
                
                currentState[regKey] = value;
            }
        });
    });

    // 変化検知と履歴への記録
    let hasChanged = !lastRegState;
    if (lastRegState) {
        for (const key in currentState) {
            if (currentState[key] !== lastRegState[key]) {
                hasChanged = true;
                break;
            }
        }
    }

    if (hasChanged) {
        const snapshot = {
            index: traceIndex++,
            time: new Date().toLocaleTimeString('ja-JP', { hour12: false }) + '.' + String(new Date().getMilliseconds()).padStart(3, '0'),
            ...currentState
        };
        traceHistory.push(snapshot);
        if (traceHistory.length > MAX_TRACE_HISTORY) {
            traceHistory.shift();
        }
        io.emit('trace-history-update', snapshot);
        lastRegState = { ...currentState };
    }

    console.log("[Backend Debug] Broadcasting registers, count:", regData.length);
    io.emit('registers', regData);
}

const net = require('net');
const UART_MAP_PATH = path.join(__dirname, 'data/uart_map.json');
let uartConnections = {}; 
let uartLogs = {}; 

let externalUartServers = {};
let externalUartClients = {};

const UART_MACROS = [
    { pattern: /login:/i, response: 'root\n', delay: 500 },
    { pattern: /password:/i, response: 'vfpga\n', delay: 500 }
];

function cleanupExternalUart(name) {
    if (externalUartClients[name]) {
        externalUartClients[name].forEach(socket => {
            try { socket.destroy(); } catch (e) {}
        });
        delete externalUartClients[name];
    }
    if (externalUartServers[name]) {
        try { externalUartServers[name].close(); } catch (e) {}
        delete externalUartServers[name];
    }
}

function syncUartConnections() {
    try {
        if (fs.existsSync(UART_MAP_PATH)) {
            const mapping = JSON.parse(fs.readFileSync(UART_MAP_PATH, 'utf8'));
            
            // Cleanup mapping connections that are no longer active
            for (const name of Object.keys(uartConnections)) {
                if (!mapping[name]) {
                    if (uartConnections[name] && uartConnections[name] !== 'connecting') {
                        try { uartConnections[name].destroy(); } catch (e) {}
                    }
                    delete uartConnections[name];
                    cleanupExternalUart(name);
                }
            }

            for (const [name, port] of Object.entries(mapping)) {
                if (!uartConnections[name]) {
                    uartConnections[name] = 'connecting';
                    connectToUart(name, port);
                }
            }
        } else {
            // Cleanup everything if mapping file is removed
            for (const name of Object.keys(uartConnections)) {
                if (uartConnections[name] && uartConnections[name] !== 'connecting') {
                    try { uartConnections[name].destroy(); } catch (e) {}
                }
                delete uartConnections[name];
                cleanupExternalUart(name);
            }
        }
    } catch (e) {}
}

function connectToUart(name, port) {
    const client = new net.Socket();
    client.connect(port, '127.0.0.1', () => {
        uartConnections[name] = client;
        uartLogs[name] = "";

        // Start proxy server for external client on port = Python port + 1000
        const extPort = parseInt(port, 10) + 1000;
        if (!externalUartServers[name]) {
            externalUartClients[name] = new Set();
            const server = net.createServer((socket) => {
                externalUartClients[name].add(socket);
                
                // Immediately replay past logs for new external connections
                if (uartLogs[name]) {
                    socket.write(uartLogs[name]);
                }

                socket.on('data', (data) => {
                    // Pipe external client input to Python bridge
                    if (uartConnections[name] && uartConnections[name] !== 'connecting') {
                        try { uartConnections[name].write(data); } catch (e) {}
                    }
                    // Mirror to dashboard
                    const text = data.toString('utf8');
                    uartLogs[name] = (uartLogs[name] + text).slice(-5000);
                    io.emit('uart-data', { name, text });
                });

                socket.on('close', () => {
                    if (externalUartClients[name]) {
                        externalUartClients[name].delete(socket);
                    }
                });

                socket.on('error', (err) => {
                    if (externalUartClients[name]) {
                        externalUartClients[name].delete(socket);
                    }
                });
            });

            server.listen(extPort, '0.0.0.0', () => {
                console.log(`[Backend] External UART proxy for ${name} listening on port ${extPort}`);
            });
            externalUartServers[name] = server;
        }
    });

    client.on('data', (data) => {
        const text = data.toString('utf8');
        uartLogs[name] = (uartLogs[name] + text).slice(-5000);
        io.emit('uart-data', { name, text });

        // Forward python data to all connected external clients
        if (externalUartClients[name]) {
            externalUartClients[name].forEach(socket => {
                try { socket.write(data); } catch (e) {}
            });
        }

        UART_MACROS.forEach(macro => {
            if (macro.pattern.test(text)) {
                setTimeout(() => {
                    if (uartConnections[name] && uartConnections[name] !== 'connecting') {
                        uartConnections[name].write(macro.response);
                    }
                }, macro.delay);
            }
        });
    });

    client.on('close', () => { 
        delete uartConnections[name]; 
        cleanupExternalUart(name);
    });
    client.on('error', () => { 
        delete uartConnections[name]; 
        cleanupExternalUart(name);
    });
}

// Socket.io
io.on('connection', (socket) => {
    socket.emit('uart-init', uartLogs);
    socket.emit('trace-history-init', traceHistory);

    // Send initial HDMI frame if exists
    const hdmiPath = manifest.hdmi_output_path || '/tmp/hdmi_output.bmp';
    if (fs.existsSync(hdmiPath)) {
        fs.readFile(hdmiPath, (err, data) => {
            if (!err) {
                socket.emit('hdmi-frame', data.toString('base64'));
            }
        });
    }


    socket.on('trace-history-clear', () => {
        traceHistory = [];
        traceIndex = 0;
        lastRegState = null;
        io.emit('trace-history-init', []);
    });

    socket.on('uart-send', ({ name, text }) => {
        if (uartConnections[name] && uartConnections[name] !== 'connecting') {
            try { uartConnections[name].write(text); } catch (e) {}
        }
        // Mirror the dashboard input to all external connected terminals
        if (externalUartClients[name]) {
            externalUartClients[name].forEach(socket => {
                try { socket.write(text); } catch (e) {}
            });
        }
    });

    socket.on('gpio-inject', ({ deviceName, bitIndex, value, dataRegName = 'DATA' }) => {
        if (!shmBuffer || !manifest.devices) return;
        const dev = manifest.devices.find(d => d.name === deviceName);
        if (!dev) return;
        const dataReg = dev.registers.find(r => r.name === dataRegName);
        if (!dataReg) return;

        const uioGpioDevs = manifest.devices.filter(d => d.type === 'uio' || d.type === 'gpio');
        const shmBaseAddr = Math.min(...uioGpioDevs.map(d => d.base_addr || 0));
        const physAddr = (dev.base_addr || 0) + parseInt(dataReg.offset, 0);
        const shmOffset = physAddr - shmBaseAddr;

        if (shmOffset >= 0 && shmOffset + 4 <= shmBuffer.length) {
            let currentVal = shmBuffer.readUInt32LE(shmOffset);
            if (value) currentVal |= (1 << bitIndex);
            else currentVal &= ~(1 << bitIndex);
            shmBuffer.writeUInt32LE(currentVal, shmOffset);
            try {
                const fd = fs.openSync(manifest.shm_path, 'r+');
                fs.writeSync(fd, shmBuffer, 0, shmBuffer.length, 0);
                fs.closeSync(fd);
            } catch (e) {
                console.error(`[Backend] Failed to write SHM: ${e.message}`);
            }
        }
    });

    socket.on('spi-adc-inject', ({ channel, value }) => {
        const ADC_SHM_PATH = '/dev/shm/spi_adc';
        try {
            if (fs.existsSync(ADC_SHM_PATH)) {
                const fd = fs.openSync(ADC_SHM_PATH, 'r+');
                const buf = Buffer.alloc(2);
                buf.writeUInt16LE(value, 0);
                fs.writeSync(fd, buf, 0, 2, channel * 2);
                fs.closeSync(fd);
            }
        } catch (e) {
            console.error(`[Backend] Failed to write SPI ADC SHM: ${e.message}`);
        }
    });
});

let lastDisplayBuffer = null;
function updateDisplayShm() {
    const displayShmPath = '/dev/shm/fbb_display_0';
    try {
        if (fs.existsSync(displayShmPath)) {
            const buffer = fs.readFileSync(displayShmPath);
            if (!lastDisplayBuffer || !lastDisplayBuffer.equals(buffer)) {
                lastDisplayBuffer = buffer;
                io.emit('display-frame', buffer.toString('base64'));
            }
        }
    } catch (e) {
        // Ignore read sharing violations or temp unlinks
    }
}

setInterval(() => {
    loadManifest();
    updateShm();
    syncUartConnections();
}, 200);

setInterval(() => {
    updateDisplayShm();
}, 33); // ~30 FPS

// GET /api/layout - Load fbb_layout.json from the active scenario folder
// @intent:rationale 指定されたテストシナリオフォルダ配下の fbb_layout.json を読み込み、クライアントに返します。存在しない場合は 404 を返します。
app.get('/api/layout', (req, res) => {
    if (!manifest.scenario_dir || !manifest.project_root) {
        return res.status(404).json({ error: 'Scenario not loaded yet' });
    }
    const layoutPath = path.join(manifest.project_root, manifest.scenario_dir, 'fbb_layout.json');
    try {
        if (fs.existsSync(layoutPath)) {
            const data = fs.readFileSync(layoutPath, 'utf8');
            return res.json(JSON.parse(data));
        } else {
            return res.status(404).json({ message: 'No saved layout' });
        }
    } catch (e) {
        console.error(`[Backend] Failed to load layout: ${e.message}`);
        return res.status(500).json({ error: e.message });
    }
});

// POST /api/layout - Save fbb_layout.json to the active scenario folder
// @intent:rationale 現在のペイン配置（レイアウト）データを、指定されたテストシナリオフォルダ配下に fbb_layout.json として永続化保存します。
app.post('/api/layout', (req, res) => {
    if (!manifest.scenario_dir || !manifest.project_root) {
        return res.status(400).json({ error: 'Scenario not loaded yet' });
    }
    const layoutPath = path.join(manifest.project_root, manifest.scenario_dir, 'fbb_layout.json');
    try {
        fs.writeFileSync(layoutPath, JSON.stringify(req.body, null, 4), 'utf8');
        console.log(`[Backend] Layout saved successfully to ${layoutPath}`);
        return res.json({ success: true });
    } catch (e) {
        console.error(`[Backend] Failed to save layout: ${e.message}`);
        return res.status(500).json({ error: e.message });
    }
});

app.get('/api/manifest', (req, res) => res.json(manifest));
app.use(express.static(path.join(__dirname, 'client/dist')));

// サーバー起動時に最初のマニフェストロードを同期実行
loadManifest();

const PORT = process.env.PORT || 8080;
server.listen(PORT, () => {
    console.log(`[Backend] Dashboard Server running on http://localhost:${PORT}`);
});
