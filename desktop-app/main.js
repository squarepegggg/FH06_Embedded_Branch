const { app, BrowserWindow } = require('electron');
const path = require('path');
const noble = require('@abandonware/noble');

const MANUFACTURER_ID_LE = [0x59, 0x00]; // 0x0059 little-endian
const PRED_MIN = 0;
const PRED_MAX = 6;
const ADV_PAYLOAD_MIN_LEN = 3;   // pred only (legacy)
const ADV_PAYLOAD_FULL_LEN = 11; // company(2) + pred(1) + x(2) + y(2) + z(2) + voltage_mv(2)

let mainWindow = null;
let scanning = false;

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 560,
    height: 900,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  const dashboardPath = path.resolve(__dirname, '..', 'ble_dashboard.html');
  mainWindow.loadFile(dashboardPath);

  mainWindow.once('ready-to-show', () => {
    mainWindow.show();
    startScanning();
  });

  mainWindow.on('closed', () => {
    mainWindow = null;
    stopScanning();
  });
}

function startScanning() {
  if (scanning) return;
  if (noble.state !== 'poweredOn') return;
  scanning = true;
  noble.startScanning([], true);
}

function stopScanning() {
  if (!scanning) return;
  scanning = false;
  noble.stopScanning();
}

function parsePayloadFromManufacturerData(manufacturerData) {
  if (!manufacturerData || !Buffer.isBuffer(manufacturerData)) return null;
  if (manufacturerData.length < ADV_PAYLOAD_MIN_LEN) return null;
  if (manufacturerData[0] !== MANUFACTURER_ID_LE[0] || manufacturerData[1] !== MANUFACTURER_ID_LE[1]) return null;
  const pred = manufacturerData[2];
  if (pred < PRED_MIN || pred > PRED_MAX) return null;
  if (manufacturerData.length >= ADV_PAYLOAD_FULL_LEN) {
    return {
      pred,
      x: manufacturerData.readInt16LE(3),
      y: manufacturerData.readInt16LE(5),
      z: manufacturerData.readInt16LE(7),
      voltageMv: manufacturerData.readUInt16LE(9),
    };
  }
  return { pred };
}

noble.on('stateChange', (state) => {
  if (state === 'poweredOn' && mainWindow && scanning) {
    noble.startScanning([], true);
  } else if (state !== 'poweredOn') {
    noble.stopScanning();
  }
});

noble.on('discover', (peripheral) => {
  const payload = parsePayloadFromManufacturerData(peripheral.advertisement?.manufacturerData);
  if (payload === null) return;
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send('prediction', payload);
  }
});

app.whenReady().then(createWindow);

app.on('window-all-closed', () => {
  stopScanning();
  app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) {
    createWindow();
  }
});
