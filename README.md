# InclinoCar

Rooftop tent leveling assistant using ESP32-C3 and MPU-6050. Displays real-time pitch and roll on an OLED screen and sends data to the InclinoCar Android app via Bluetooth.

## Web Installer

Flash firmware directly in your browser — no drivers or software needed.

👉 **[tophe75.github.io/inclinocar](https://tophe75.github.io/inclinocar/)**

Requires Chrome or Edge on desktop.

---

## Hardware

### What You Need

- ESP32-C3 DevKitM-1
- SSD1306 OLED display 128×64 (I2C)
- MPU-6050 GY-521 accelerometer/gyroscope
- Momentary push button
- Jumper wires

### Wiring

All devices share the same I2C bus on GPIO6 and GPIO7.

```
ESP32-C3          SSD1306 OLED
─────────         ────────────
3.3V      →       VCC
GND       →       GND
GPIO6     →       SDA
GPIO7     →       SCL

ESP32-C3          MPU-6050 GY-521
─────────         ───────────────
3.3V      →       VCC
GND       →       GND
GPIO6     →       SDA
GPIO7     →       SCL
GND       →       AD0        (sets address to 0x68)

ESP32-C3          Button
─────────         ──────
GPIO5     →       Terminal 1
GND       →       Terminal 2
```

---

## Installation

1. Open the [Web Installer](https://tophe75.github.io/inclinocar/) in Chrome or Edge
2. Select the firmware version
3. Click **Install**
4. Select **Erase device** when prompted — required for first install
5. Device reboots automatically when done

---

## Usage

### Display

```
  InclinoCar vX.X.X
  ──────────────────
  P  +2.3°
  R  -1.1°
  ──────────────────
  Adjust...            ← or ** LEVEL ** when within 1°
```

`BT+` in the top right corner indicates a Bluetooth app connection.

### Calibration

Hold the button for 1 second with the vehicle on flat ground. The device averages 50 readings and saves the offsets to flash memory — they survive reboots.

### Bluetooth App

The device advertises as **InclinoCar** using the Nordic UART Service (NUS). It sends JSON data every 100ms:

```json
{"p":2.3,"r":-1.1}
```

Where `p` = pitch and `r` = roll in degrees.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Display blank | Check SDA=GPIO6, SCL=GPIO7, VCC=3.3V |
| IMU not found | Check MPU-6050 wiring, AD0 must be GND |
| Values drifting | Hold button to calibrate |
| BT- on display | Open InclinoCar app and connect |
| Web installer fails | Hard refresh (Ctrl+Shift+R) or use incognito window |

---

## Project Structure

```
firmware/          PlatformIO project (ESP32-C3)
app/               Flutter app (Android + iOS)
  app/assets/      App icon and assets
docs/              GitHub Pages web installer
3d-print/          STL files for cases and mounting brackets
scripts/           Build utilities
.github/workflows/ CI/CD
```

## Android App

Download the latest APK from [GitHub Releases](https://github.com/tophe75/inclinocar/releases) and install it on your Android phone.

**Enable unknown sources:**
Settings → Security → Install unknown apps → allow your browser

The app connects to the device via Bluetooth, shows a live bubble level indicator and pitch/roll readings, and lets you trigger calibration remotely.

## 3D Printed Parts

STL files for cases and mounting brackets are in the `3d-print/` folder. Print in PETG or ABS — avoid PLA as it can warp in a hot car.

## Building Locally

Requires [VS Code](https://code.visualstudio.com/) and [PlatformIO](https://platformio.org/install/ide?install=vscode).

Open the `firmware/` folder in VS Code, then click **Upload**.
