# AccelDevice Desktop (macOS)

Desktop app that receives **BLE advertising-only** predictions from the nRF52832 + BMA400 device (company ID 0x0059, 1-byte prediction) and displays them in the same dashboard as the web version. No GATT; uses native BLE scanning via `@abandonware/noble`.

## Requirements

- macOS (Bluetooth required)
- Node.js 18+ (for Electron and native modules)

## Run

From this directory (`desktop-app/`):

```bash
npm install
npm start
```

On first run, macOS may prompt for Bluetooth access; allow it so the app can scan for advertisements.

## Usage

1. Start the nRF device with the advertising-only firmware (manufacturer data 0x0059 + prediction byte).
2. Launch the app with `npm start`.
3. The dashboard loads and scanning starts automatically. When the device is in range, predictions appear in the hero, activity grid, and event log.

The "Start scanning" / "Stop scanning" buttons are hidden in the desktop app; BLE scanning is controlled by the main process.

## Browser vs desktop

- **Chrome (browser):** Use `ble_dashboard.html` in a browser; on desktop Chrome, `requestLEScan` often does not receive advertisements. The same page works on Chrome for Android.
- **This app:** Uses native BLE (noble) so it receives advertising packets on macOS and forwards predictions to the dashboard UI.
