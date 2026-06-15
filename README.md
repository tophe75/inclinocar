# InclinoCar

Rooftop tent leveling assistant using ESP32-C3 and MPU-6050. Displays real-time pitch and roll on an OLED screen and connects to the InclinoCar Android app via Bluetooth.

## Web Installer

Flash firmware directly in your browser — no drivers or software needed.

> ⚠️ Flash the device **before** assembling it into the case — the original case has no access hole for the BOOT button once assembled.

👉 **[tophe75.github.io/inclinocar](https://tophe75.github.io/inclinocar/)**

Requires Chrome or Edge on desktop.

---

## Latest Releases

🔧 **Firmware:**
- ESP32-C3 Super Mini ![ESP32-C3](https://img.shields.io/github/v/release/tophe75/inclinocar?filter=fw-esp32c3-*&label=ESP32-C3&color=blue&logo=espressif)
- M5StickC Plus ![M5StickC](https://img.shields.io/github/v/release/tophe75/inclinocar?filter=fw-m5stick-*&label=M5StickC%20Plus&color=blue&logo=espressif)

🤖 **Android:** ![Android](https://img.shields.io/github/v/release/tophe75/inclinocar?filter=android-*&label=Android&color=green&logo=android)

🍎 **iOS:** ![iOS](https://img.shields.io/github/v/release/tophe75/inclinocar?filter=ios-*&label=iOS&color=black&logo=apple)

---

## Hardware

### What You Need

- [ESP32-C3 Super Mini](https://www.amazon.com/DWEII-ESP32-C3-Development-Supermini-Bluetooth/dp/B0G5XS345R)
- [SSD1306 OLED display 128×64 (I2C)](https://www.amazon.com/UCTRONICS-SSD1306-Self-Luminous-Display-Raspberry/dp/B072Q2X2LL)
- [MPU-6050 GY-521 accelerometer/gyroscope](https://www.amazon.com/EPLZON-MPU-6050-Accelerometer-Gyroscope-Converter/dp/B09TVYVC6X)
- [Momentary push button](https://www.amazon.com/Gebildet-Momentary-Button-Switch-Railway/dp/B07YDHP3HS)
- [Jumper wires (26 AWG Silicone)](https://www.amazon.com/Fermerry-Stranded-Colors-Flexible-electrical/dp/B089D3T1JD)

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
GND       →       AD0        (sets I2C address to 0x68)

ESP32-C3          Button
─────────         ──────
GPIO5     →       Terminal 1
GND       →       Terminal 2
(internal pull-up — no resistor needed)
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

### Boot Screen

On power-up the device shows the device nickname, firmware version, MAC address and PIN for 5 seconds:

```
  InclinoCore
  v0.0.31
  E8:3D:C1:9E:43:38
  PIN: 9208
  Cal loaded
```

### Display

```
  InclinoCore        PIN:9208
  ──────────────────────────
  P  +2.3°
  R  -1.1°
  ──────────────────────────
  Adjust...            ← or ** LEVEL ** when within 1°
```

`BT` appears in the top right when the app is connected.

### Button

| Action | Function |
|--------|----------|
| Short press | Cycle display brightness (25% → 50% → 75% → 100% → 25%) |
| Hold 1 second | Calibrate (keep device still on flat ground) |

Default brightness on first boot is 25%.

### Calibration

Hold the button for 1 second with the vehicle on flat level ground. The device takes 50 readings and saves the offsets to flash — they survive reboots and firmware updates.

### Bluetooth Pairing

1. Open the InclinoCar app
2. Tap the **⋮** menu → **Scan for InclinoCore**
3. Select your device from the list (identified by MAC address)
4. Enter the 4-digit PIN shown on the device display
5. The app remembers your device and auto-connects next time
=======
Short press the buttom to cycle through 25%, 50%, 75% and 100% (default start value is 25%).

The PIN is unique to each device and never changes.

### Bluetooth Data

The device sends JSON over Nordic UART Service (NUS) every 100ms:

```json
{"p":1.2,"r":0.3,"n":"InclinoCore"}
```

`p` = pitch (degrees), `r` = roll (degrees), `n` = device nickname

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Display blank after flash | Check SDA=GPIO6, SCL=GPIO7, VCC=3.3V |
| IMU not found | Check MPU-6050 wiring, AD0 must be GND |
| Values drifting | Hold button 1s to calibrate |
| App can't find device | Menu → Scan for InclinoCore, check device is powered on |
| Wrong PIN | Check the device display — PIN is shown on boot screen and top-right when not connected |
| Web installer fails | Hard refresh (Ctrl+Shift+R) or use incognito window |
| NVS errors in serial log | Flash with Erase device selected in web installer |

---

## Android App

Download the latest `InclinoCar-installer.apk` from [GitHub Releases](https://github.com/tophe75/inclinocar/releases).

**Install instructions:**
1. Download the APK on your Android phone
2. Settings → Security → Install unknown apps → allow your browser
3. Open the APK and tap Install

**Features:**
- Auto-connects to last known device on startup
- PIN-based pairing for identifying your device among multiple units
- Bubble level with car silhouette and directional arrows
- Pitch and roll readout with colour coding (green < 1°, amber < 3°, red ≥ 3°)
- Calibrate remotely from the app
- Set a custom nickname for your device
- Known devices list with ability to remove saved devices
- Screen-on mode to keep display active while leveling

---

## 3D Printed Parts

STL files for cases and mounting brackets are in the `3d-print/` folder. Print in PETG or ABS — avoid PLA as it can warp in a hot car.

---

## Project Structure

```
firmware/           PlatformIO project (ESP32-C3 core unit)
app/                Flutter app (Android + iOS)
  app/lib/          Dart source code
  app/android/      Android build files and icons
  app/ios/          iOS build files and icons
  app/assets/       App assets (master icon, Play Store icon)
docs/               GitHub Pages web installer
3d-print/           STL files for cases and mounting brackets
scripts/            Build utilities (version injection)
.github/workflows/  CI/CD pipelines
```

## Building Locally

**Firmware** — requires [VS Code](https://code.visualstudio.com/) and [PlatformIO](https://platformio.org/install/ide?install=vscode).
Open the `firmware/` folder in VS Code and click **Upload**.

**App** — requires [Flutter](https://flutter.dev/docs/get-started/install) SDK.
```bash
cd app
flutter pub get
flutter build apk --release
```
