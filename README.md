# InclinoCar 🏕️

A rooftop tent leveling assistant using ESP32-C3, MPU-6050 IMU sensors, and a Flutter mobile app.

## Overview

InclinoCar helps you level your rooftop tent before sleeping. It measures the pitch and roll of your tent and guides you through the leveling process using ramp boards or wheel chocks — all from your phone or a small OLED display mounted on the unit.

---

## Hardware

### Core Unit (ESP32 #1) — Required
| Component | Model | Notes |
|-----------|-------|-------|
| Microcontroller | ESP32-C3 | BLE + ESP-NOW capable |
| IMU | GY-521 (MPU-6050) | 3-axis gyro + accelerometer, I2C |
| Display (optional) | SSD1306 128×64 OLED | I2C, 0.96 inch, blue |

### Satellite Unit (ESP32 #2) — Optional
| Component | Model | Notes |
|-----------|-------|-------|
| Microcontroller | ESP32-C3 | ESP-NOW transmitter |
| IMU | GY-521 (MPU-6050) | Same as core unit |

---

## Wiring

### Core Unit

```
MPU-6050 (GY-521)          ESP32-C3
─────────────────          ─────────
VCC              →         3.3V
GND              →         GND
SDA              →         GPIO6
SCL              →         GPIO7
AD0              →         GND     (I2C address: 0x68)
INT              →         GPIO4   (optional)

SSD1306 OLED               ESP32-C3
─────────────────          ─────────
VCC              →         3.3V
GND              →         GND
SDA              →         GPIO6   (shared I2C bus)
SCL              →         GPIO7   (shared I2C bus)
```

### Satellite Unit
```
MPU-6050 (GY-521)          ESP32-C3
─────────────────          ─────────
VCC              →         3.3V
GND              →         GND
SDA              →         GPIO6
SCL              →         GPIO7
AD0              →         GND
```

---

## Usage Modes

| Mode | Hardware needed | Description |
|------|----------------|-------------|
| **Minimal** | Core unit + phone | Tent pitch/roll streamed over BLE to Flutter app |
| **Standalone** | Core unit + OLED | No phone needed, angles shown on display |
| **Full system** | Core + Satellite + phone | Compare tent vs car body, optimal leveling guidance |

---

## Project Structure

```
inclinocar/
├── firmware/
│   ├── core/           # ESP32 #1 — IMU + BLE + OLED + optional ESP-NOW
│   └── satellite/      # ESP32 #2 — IMU + ESP-NOW transmitter only
├── app/                # Flutter app (Android + iOS)
├── docs/               # Architecture, BLE spec, wiring diagrams
└── README.md
```

---

## BLE Service

| UUID | Type | Description |
|------|------|-------------|
| `4fafc201-1fb5-459e-8fcc-c5c9c331914b` | Service | InclinoCar primary service |
| `beb5483e-36e1-4688-b7f5-ea07361b26a8` | Characteristic | Pitch (float, degrees) |
| `beb5483f-36e1-4688-b7f5-ea07361b26a8` | Characteristic | Roll (float, degrees) |
| `beb54840-36e1-4688-b7f5-ea07361b26a8` | Characteristic | Satellite pitch (float, optional) |
| `beb54841-36e1-4688-b7f5-ea07361b26a8` | Characteristic | Satellite roll (float, optional) |

---

## Getting Started

### Firmware
1. Install [VS Code](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
2. Open `firmware/core` or `firmware/satellite` as a PlatformIO project
3. Connect your ESP32-C3 via USB
4. Click **Upload** in PlatformIO

### App
1. Install [Flutter SDK](https://flutter.dev/docs/get-started/install)
2. `cd app && flutter pub get`
3. `flutter run`

---

## License

MIT
