# Solar-Powered Batteryless Activity Recognition Wristband

A wrist-worn wearable that harvests energy from ambient light, performs on-device activity recognition using a tiny machine learning model, and transmits classification results over Bluetooth Low Energy—all without a rechargeable battery.

## Overview

This project explores whether a batteryless, solar-powered wearable can perform meaningful activity recognition entirely on harvested energy. Instead of streaming raw sensor data to the cloud or relying on a rechargeable battery, the device:

1. **Harvests** energy from a small solar panel into an energy-storage capacitor
2. **Senses** three-axis accelerometer data at 25 Hz from a Bosch BMA400
3. **Classifies** each 1-second window locally using an Edge Impulse quantized 1D CNN
4. **Transmits** only the activity label (plus latest accel sample) via BLE to a client app

The core design challenge is delivering reliable, timely classifications while operating solely on harvested energy and within the strict memory, compute, and communication constraints of a low-power microcontroller.

## What We're Building

- **Hardware**: Wrist-mountable PCB with photovoltaic panel, power-management IC, supercapacitor, nRF52832 microcontroller, BMA400 accelerometer, and BLE radio
- **Firmware**: Zephyr RTOS-based embedded software that coordinates sensing, 25-sample windowing, ML inference, and BLE notifications; energy-aware scheduling (voltage gatekeeper, deep sleep) for batteryless operation
- **ML Pipeline**: Edge Impulse project with a trained INT8-quantized Conv1D model (75 inputs, 7 activity classes: downstairs, jump, running, sitting, standing, upstairs, walking) that runs on-device
- **Client**: Web Bluetooth dashboard (current) for real-time visualization; Android application (planned deliverable)

## Current Implementation Status

We are currently flashing this firmware onto a **custom PCB** with the nRF52832 microcontroller and BMA400 accelerometer integrated on board. The PCB is **powered by a solar harvesting subsystem** (photovoltaic panel and energy-storage capacitor). The core **Sense → Infer → Transmit** pipeline is working; our focus is on **reducing power consumption** so that ML inference and BLE transmission can run reliably on solar power alone.

| Component | Status |
|-----------|--------|
| Custom PCB (nRF52832 + BMA400) | ✅ Complete |
| Solar-powered operation | ✅ In progress |
| BMA400 accelerometer (SPI, FIFO, interrupt-driven) | ✅ Complete |
| 25-sample sliding window, Edge Impulse feature format | ✅ Complete |
| On-device ML inference (ei-v3 1D CNN, INT8) | ✅ Complete |
| BLE GATT notifications (label + X,Y,Z + profiling) | ✅ Complete |
| Web Bluetooth dashboard | ✅ Complete |
| Power optimization (gatekeeper, deep sleep, duty cycling) | 🔄 Active focus |
| Android application | 📋 Planned |

## Power Optimization Focus

Because the device is powered solely by solar harvesting, we are actively exploring ways to **reduce power consumption** so that ML predictions can run continuously on harvested energy. This includes:

- **Voltage gatekeeper**: Enforcing a go/no-go threshold (e.g., 3.8 V) before allowing high-power operations (inference, BLE)
- **Deep sleep routines**: Disabling UART, I2C, and unused peripherals to cut idle current (e.g., from ~3.4 mA to ~18 µA)
- **Duty cycling**: Running inference and BLE only when sufficient energy is available
- **Checkpointing**: Saving state to non-volatile memory so the system can resume after brownouts
- **Model and inference tuning**: Keeping the ML model small and inference latency low to minimize energy per prediction

## Project Structure

```
├── src/
│   ├── main.c           # Main firmware: BMA400 thread, ML integration, BLE
│   ├── bma400.c         # BMA400 driver (SPI, FIFO)
│   └── ei_glue_v3.cpp   # Edge Impulse model wrapper
├── include/
│   ├── glueV3.h         # C API for classifier
│   ├── bma400.h         # BMA400 driver API
│   └── bma400_defs.h
├── ei-v3/               # Edge Impulse model (TFLite, model metadata)
├── ble_dashboard.html   # Web Bluetooth dashboard for live testing
├── boards/              # Zephyr board overlays (nRF52 DK)
├── prj.conf             # Zephyr / Nordic config
└── Updated System Design Report.md   # Full system design documentation
```

## Requirements

- **Nordic nRF Connect SDK** (v3.x) with Zephyr RTOS
- **Target hardware**: Custom PCB with nRF52832 and BMA400, powered by solar harvesting (or nRF52 DK for development)
- **BMA400** accelerometer connected via SPI (see `boards/nrf52dk_nrf52832.overlay`)

## Build & Flash

```bash
# From the project root, using West (nRF Connect SDK)
west build -b nrf52dk_nrf52832
west flash
```

Or use the Nordic VS Code extension / nRF Connect for Desktop.

## Web Dashboard

Open `ble_dashboard.html` in a Chromium-based browser (Web Bluetooth support required). Connect to the device by name **"AccelDevice"** to view:

- Current activity classification
- Per-class activity counts
- Live accelerometer waveform (raw LSB and m/s²)
- ML profiling: inference latency, arena memory, model size
- Event log

## Key Specifications

- **Sampling**: 25 Hz, ±4G range, 25-sample (1 s) windows
- **Model**: 75 inputs (25×3 axes), 7 classes, INT8 quantization, ~14 KB TFLite arena
- **BLE Payload**: 17 bytes (label + X,Y,Z + inference latency + arena + model size)
- **Latency Target**: < 2 s end-to-end (window + inference + BLE)
- **Accuracy Target**: ≥ 70% overall, per-class F1 ≈ 0.6+

## Team

ECE 464K / ECE 464D project by Ronak Jain, Nikhil Kabra, Andres Wearden, Karemeldin Mohamed, and Matthew O.

## Documentation

For full system design, specifications, risk reduction, test plan, and project management details, see **[Updated System Design Report.md](Updated%20System%20Design%20Report.md)**.

## License

See project and Nordic Semiconductor license terms.
