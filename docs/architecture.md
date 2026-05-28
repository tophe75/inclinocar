# InclinoCar — Architecture

## System Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     TENT / ROOF RACK                        │
│                                                             │
│   ┌──────────────────────────────────┐                      │
│   │        ESP32-C3 CORE UNIT        │                      │
│   │  ┌──────────┐  ┌──────────────┐ │                      │
│   │  │ MPU-6050 │  │  SSD1306     │ │   ◄── BLE ──►  📱    │
│   │  │ (Tent    │  │  OLED 128x64 │ │                Phone  │
│   │  │ angles)  │  │  (optional)  │ │                      │
│   │  └──────────┘  └──────────────┘ │                      │
│   └──────────────────────────────────┘                      │
│              │ ESP-NOW (optional)                           │
└──────────────┼──────────────────────────────────────────────┘
               │
┌──────────────┼──────────────────────────────────────────────┐
│   CAR BODY   │                                              │
│   ┌──────────▼──────────────────────┐                      │
│   │      ESP32-C3 SATELLITE UNIT    │                      │
│   │  ┌──────────┐                  │                      │
│   │  │ MPU-6050 │                  │                      │
│   │  │ (Car     │                  │                      │
│   │  │ angles)  │                  │                      │
│   │  └──────────┘                  │                      │
│   └─────────────────────────────────┘                      │
└─────────────────────────────────────────────────────────────┘
```

---

## Component Responsibilities

### ESP32-C3 Core Unit
- Reads tent IMU (MPU-6050) via I2C
- Applies complementary filter for stable angle estimation
- Serves BLE GATT server (InclinoCar service)
- Sends pitch/roll notifications to connected phone at 20 Hz
- Receives satellite data via ESP-NOW and forwards over BLE
- Drives SSD1306 OLED display (if connected)

### ESP32-C3 Satellite Unit (optional)
- Reads car body IMU (MPU-6050) via I2C
- Applies complementary filter
- Transmits pitch/roll to core unit via ESP-NOW at 20 Hz
- No display, no BLE

### Flutter App
- Scans for BLE device named "InclinoCar"
- Subscribes to GATT notifications
- Renders visual bubble level and angle readouts
- Shows leveling guidance (which corner to raise/lower)

---

## Sensor Fusion

The MPU-6050 raw data is processed with a **complementary filter**:

```
filteredPitch = α × (filteredPitch + gyro_x × dt) + (1 - α) × accel_pitch
filteredRoll  = α × (filteredRoll  + gyro_y × dt) + (1 - α) × accel_roll
```

Where `α = 0.96` — high-pass for gyro (short term), low-pass for accelerometer (long term drift correction).

This is intentionally simple and sufficient for static/slow-moving tent leveling. A Madgwick or Kalman filter can be substituted for higher precision.

---

## Usage Modes

### Mode 1: Minimal (Core only)
1. Power on core unit
2. Open app → tap "Connect to InclinoCar"
3. Read pitch/roll on phone
4. Adjust ramps/chocks until level

### Mode 2: Standalone (Core + OLED)
1. Power on core unit with OLED connected
2. Read angles directly on display
3. No phone required

### Mode 3: Full System (Core + Satellite + Phone)
1. Power on both units
2. Satellite auto-connects to core via ESP-NOW
3. App shows both tent angles and car body angles
4. Delta between tent and car shows actual leveling needed

---

## First-Time Setup

### ESP-NOW MAC Address Pairing
1. Flash satellite firmware to ESP32 #2
2. Open Serial Monitor at 115200 baud
3. Note the MAC address printed on boot: `Satellite MAC: XX:XX:XX:XX:XX:XX`
4. Open `firmware/core/include/config.h`
5. Update `CORE_MAC_ADDR` with satellite's MAC
6. Re-flash core firmware

### Enable OLED
In `firmware/core/platformio.ini`, set:
```ini
build_flags = -DUSE_OLED=1
```

### Enable ESP-NOW on Core
In `firmware/core/platformio.ini`, set:
```ini
build_flags = -DUSE_OLED=1 -DUSE_ESPNOW=1
```
