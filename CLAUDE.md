# InclinoCar — Project Guide for Claude Code

Rooftop tent leveling assistant. An IMU-based device shows real-time pitch/roll on a
display and streams it over BLE to an Android app. Three hardware targets, one shared
Flutter app, a GitHub Pages web installer, and per-target CI.

## Repository layout

```
firmware/
  firmware-esp32c3/     ESP32-C3 Super Mini + external MPU-6050 + SSD1306 OLED
  firmware-m5stick/     M5StickC Plus (built-in MPU6886)
  firmware-m5sticks3/   M5StickS3 (built-in BMI270, ESP32-S3)
app/                    Flutter app (Android + iOS)
  lib/main.dart         All app logic
  pubspec.yaml
  android/ ios/ assets/
docs/                   GitHub Pages web installer
  index.html            Hardware selector + version dropdown
  manifest-esp32c3.json
  manifest-m5stick.json
  manifest-m5sticks3.json
  bins/{esp32c3,m5stick,m5sticks3}/   CI-committed firmware binaries
3d-print/               STL/3MF files for cases and brackets
scripts/set_version.py  Injects git tag into platformio.ini FW_VERSION
.github/workflows/
  build-esp32c3.yml     tag: fw-esp32c3-v*
  build-m5stick.yml     tag: fw-m5stick-v*
  build-m5sticks3.yml   tag: fw-m5sticks3-v*
  build-app-android.yml tag: android-v*
```

## Tag conventions (trigger CI)

| Tag pattern         | Builds                         |
|---------------------|--------------------------------|
| `fw-esp32c3-v*`     | ESP32-C3 firmware              |
| `fw-m5stick-v*`     | M5StickC Plus firmware         |
| `fw-m5sticks3-v*`   | M5StickS3 firmware             |
| `android-v*`        | Android APK                    |
| `ios-v*`            | iOS IPA                        |
| `app-v*`            | Both Android + iOS             |

CI strips the `fw-<target>-` prefix, runs `scripts/set_version.py` on that target's
`platformio.ini`, builds, copies bins to `docs/bins/<target>/`, updates the matching
manifest, force-commits to main, and cuts a GitHub Release.

## Release workflow (per firmware target)

```powershell
git add firmware/firmware-<target>/
git commit -m "fw-<target>-v0.0.X — <summary>"
git pull origin main --no-rebase   # CI force-commits bins to main; always pull first
git push origin main
git tag fw-<target>-v0.0.X
git push origin fw-<target>-v0.0.X
```

If retagging the same version: `git tag -d <tag>; git push origin --delete <tag>` first.
The `git pull --no-rebase` before pushing is mandatory — the CI bot pushes bins to main
and your local will be behind.

## Firmware architecture (shared across all three targets)

- **BLE**: NimBLE, Nordic UART Service. Advertises as `Core#<MAC>` (fixed, derived from
  MAC, never changes with nickname). App scans for the `Core#` prefix.
- **Nickname**: display/JSON label only, stored in NVS, set via `NICK:<name>`. Does NOT
  change the BLE advertising name.
- **PIN**: deterministic from MAC `(mac[4]*256+mac[5]) % 9000 + 1000`. Shown on boot
  screen and top-right when disconnected. Verified via `PIN:XXXX`; known devices skip
  it via `KNOWNMAC:<mac>`.
- **JSON out** (every 100 ms, only after PIN verified):
  `{"p":<pitch>,"r":<roll>,"n":"<nickname>"}`
- **Filter**: median filter, `MF_SIZE` set per-target (7 for esp32c3/m5stick, 5 for
  m5sticks3 — its BMI270 is much less noisy than the MPU6050/MPU6886). Chosen over EMA
  and complementary filter — see "Hard-won lessons".
- **Calibration**: 50 samples, saves pitch/roll offsets to NVS, survives reboots.
  After calibration, seed the filter buffers with the current reading so it snaps
  instead of crawling.
- **Buttons**: short press cycles brightness (25/50/75/100 %), hold 1 s calibrates.

### Per-target specifics

**ESP32-C3** (`firmware-esp32c3`)
- Board `esp32-c3-devkitm-1`. I2C SDA=GPIO6 SCL=GPIO7, button GPIO5.
- MPU-6050 via Adafruit libs (returns m/s², not g). Flash bootloader offset `0x0`.
- Axis: `rawPitch = atan2(-ax, sqrt(ay²+az²))`, `rawRoll = -atan2(ay, az)`, then
  swapped `rotPitch = -rawRoll`, `rotRoll = rawPitch`. Verified: lift front → pitch +,
  lift right → roll +.

**M5StickC Plus** (`firmware-m5stick`)
- Board `m5stick-c` (shared ID with original; `m5stick-c-plus` is NOT valid in the
  espressif32 platform). Build env is `m5stick-c-plus` — CI copies bins from
  `.pio/build/m5stick-c-plus/`.
- M5Unified library (NOT M5StickCPlus — that gave a blank screen). MPU6886 IMU.
  BtnA for brightness/calibrate. Battery via `M5.Power.getBatteryLevel()`.
- Display 240×135 landscape (rotation 1). Flash bootloader offset `0x1000` (4096),
  needs `boot_app0.bin` at `0xe000`.
- Mounted display-face-left: gravity on **+X**. Axis:
  `pitch = -atan2(ay, ax)`, `roll = -atan2(az, ax)` (see source for exact signs used).

**M5StickS3** (`firmware-m5sticks3`)
- Board `m5stack-sticks3`, ESP32-S3, BMI270 IMU (much less noisy than MPU6886).
  Libraries M5Unified + M5PM1 (power mgmt), both from GitHub not the registry.
  `board_build.arduino.memory_type = qio_opi` for PSRAM.
- Flash bootloader offset `0x0` (ESP32-S3), needs `boot_app0.bin` at `0xe000`.
- Mounted display-face-left: gravity on **+Y** (differs from StickC Plus). Axis:
  `pitch = -atan2(ax, ay)`, `roll = atan2(az, ay)`.
- **KNOWN ISSUE**: freezes in place (confirmed NOT a reboot — no boot screen replay)
  when connected to a PC via USB with no application actively reading the serial
  port, e.g. VS Code/PlatformIO open with no serial monitor attached. Opening a
  serial monitor un-freezes it immediately; closing the monitor freezes it again —
  consistent with a blocked/full USB-CDC TX buffer, though `Serial.setTxTimeoutMs(0)`
  (already applied, see `main.cpp`) does not fully prevent it, so the exact mechanism
  isn't fully confirmed. An earlier theory attributed this to the ESP32-S3
  USB-Serial/JTAG peripheral resetting the chip on host DTR/RTS line changes (the S3
  has no `USB_UART_CHIP_RST_DIS`-equivalent disable register, unlike the C6) — but
  a DTR/RTS-triggered chip reset would show the boot screen replay, which doesn't
  happen, so that theory doesn't fully fit the confirmed symptom either. Only affects
  this specific USB dev-workflow scenario: works fine on battery, charger, power
  bank, car USB, and through the Web Installer (its console actively reads the
  output). Documented in README, not fixable in firmware so far.

## Web installer (`docs/index.html`)

- Three hardware buttons. Each `HW` entry has `tagPrefix`, `manifestPath`, `chip`
  (chipFamily for esp-web-tools), and `blOffset`.
- Latest version → served from the GitHub Pages CDN manifest. Older versions → manifest
  built client-side from GitHub Release asset URLs.
- `buildManifest` uses `hw.blOffset` for the bootloader and conditionally includes
  `boot_app0.bin` at `0xe000` when the asset exists. **Never hardcode offsets** — ESP32
  = 4096, ESP32-C3 / ESP32-S3 = 0.
- GitHub Pages caches hard; hard-refresh (Ctrl+Shift+R) after changes.

## Flutter app (`app/lib/main.dart`)

- `flutter_blue_plus` pinned to exactly **1.35.3**.
- BLE scan uses a `StreamSubscription` (never `await for` — it blocks forever on
  Android). Filter on `Core#` name prefix; `withServices` filtering is unreliable on
  Android so scan all and filter in results.
- PIN pairing flow, known-device list in SharedPreferences, auto-connect to preferred
  MAC on startup, remote calibrate, nickname, bubble level, screen wake-lock.
- APK output `InclinoCar-installer.apk`. Version const `kAppVersion` patched by CI.

## Hard-won lessons (do not relearn these)

- **Median filter beats EMA and complementary filter here.** EMA with a low alpha
  crawled for ~200 samples after calibration; complementary filter integrated the
  gyro and made **yaw** rotation bleed into roll (worse). Pure accelerometer +
  median(7) is the validated approach. Yaw can't affect a pure-accel reading in
  principle; residual yaw sensitivity was just the device not being level.
- **Seed the filter after calibration and on boot** with the actual current reading
  (WITHOUT the mounting offset baked in) or values crawl slowly to target.
- **Mounting offset (+/-90°) belongs only at final output**, never inside filter math,
  or the filter fights a large constant.
- **Establish axis orientation empirically** with a three-position test (flat on back,
  then each edge down, then the real mounted position + directional tilts). Each IMU
  sits on a different gravity axis: MPU-6050 and MPU6886 differ from the BMI270.
- **`m5stick-c` is the board ID** for the C Plus; the plausible-looking `m5stick-c-plus`
  does not exist in the platform.
- **M5Unified, not M5StickCPlus**, for the C Plus (blank screen otherwise).
- **PlatformIO build-output folder = the env name, not the board name.** CI copy paths
  must match `[env:...]`.
- **`boot_app0.bin` at `0xe000` is required** for ESP32 and ESP32-S3 web-installs
  (absence → `flash read err, 1000` boot loop). ESP32-C3 does not need it.
- **Erase NVS** (`pio run -t erase`, or "Erase device" in the installer) when changing
  default offsets, or stale saved offsets mask the fix.
- Do a **clean build** (`pio run -t clean`) when edits seem not to take — stale objects.

## Build / flash locally

```powershell
cd firmware/firmware-<target>
pio run                     # build
pio run --target upload     # flash (M5 sticks: hold power ~2 s for download mode)
pio run --target monitor    # serial @ 115200
pio run --target clean      # force full rebuild
pio run --target erase      # wipe flash + NVS
```

App:
```powershell
cd app
flutter pub get
flutter build apk --release
```
