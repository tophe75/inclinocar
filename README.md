# InclinoCar 🏕️

A rooftop tent leveling assistant using ESP32-C3, MPU-6050 IMU, and a Flutter mobile app.

## Overview

InclinoCar helps you level your rooftop tent before sleeping. Mount the Core Unit on the tent or roof rack — it measures pitch and roll and guides you through placing ramps or wheel chocks. Read the angles on the built-in OLED, your phone, or a remote Satellite Display unit inside the car.

---

## Hardware

### Core Unit (ESP32 #1) — Required
| Component | Model | Notes |
|-----------|-------|-------|
| Microcontroller | ESP32-C3 | BLE + ESP-NOW |
| IMU | GY-521 (MPU-6050) | 3-axis gyro + accelerometer, I2C |
| Display | SSD1306 128×64 OLED | I2C, 0.96 inch |
| Button | Momentary push button | Calibration reset, GPIO3 → GND |

### Satellite Display Unit (ESP32 #2) — Optional
| Component | Model | Notes |
|-----------|-------|-------|
| Microcontroller | ESP32-C3 | ESP-NOW receiver |
| Display | SSD1306 128×64 OLED | I2C, 0.96 inch |

No IMU on the Satellite — it only receives data from the Core and displays it. Place it anywhere convenient in the car or tent.

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

SSD1306 OLED               ESP32-C3
─────────────────          ─────────
VCC              →         3.3V
GND              →         GND
SDA              →         GPIO6   (shared I2C bus)
SCL              →         GPIO7   (shared I2C bus)

Calibration Button         ESP32-C3
──────────────────         ─────────
Terminal 1       →         GPIO3
Terminal 2       →         GND
(internal pull-up enabled — no resistor needed)
```

### Satellite Display Unit
```
SSD1306 OLED               ESP32-C3
─────────────────          ─────────
VCC              →         3.3V
GND              →         GND
SDA              →         GPIO6
SCL              →         GPIO7
```

---

## Usage Modes

| Mode | Hardware | Description |
|------|----------|-------------|
| **Standalone** | Core unit | Angles on Core OLED, no phone needed |
| **Phone** | Core + phone | Full bubble level UI via BLE app |
| **Remote display** | Core + Satellite | Satellite OLED inside car/tent, Core on rack |

---

## Calibration

The Core Unit calibrates automatically on every power-on. Place it on the tent mount before turning on for best results.

To reset calibration at any time:
- **Physical button:** Hold the button on the Core Unit for 1 second
- **App:** Tap the **Recalibrate** button in the Flutter app

During calibration the unit must be kept still for ~2 seconds.

---

## Project Structure

```
inclinocar/
├── firmware/
│   ├── core/           # ESP32 #1 — IMU + BLE + OLED + ESP-NOW TX + cal button
│   └── satellite/      # ESP32 #2 — OLED display + ESP-NOW RX only
├── app/                # Flutter app (Android + iOS)
├── docs/               # GitHub Pages web installer + documentation
└── README.md
```

---

## BLE Service

| UUID | Type | Description |
|------|------|-------------|
| `4fafc201-1fb5-459e-8fcc-c5c9c331914b` | Service | InclinoCar primary service |
| `beb5483e-36e1-4688-b7f5-ea07361b26a8` | Characteristic | Pitch (float32, degrees, NOTIFY) |
| `beb5483f-36e1-4688-b7f5-ea07361b26a8` | Characteristic | Roll (float32, degrees, NOTIFY) |
| `beb54842-36e1-4688-b7f5-ea07361b26a8` | Characteristic | Calibrate (write 0x01 to reset) |
| `beb54843-36e1-4688-b7f5-ea07361b26a8` | Characteristic | Status (NOTIFY: 0x01 = cal done) |

---

## Getting Started

### Flash Firmware
Use the **[Web Installer](https://tophe75.github.io/inclinocar/)** — flash directly from Chrome or Edge, no drivers needed.

### App
```bash
cd app
flutter pub get
flutter run
```

---

## License
MIT
