#pragma once

// ─── I2C Bus 0 — OLED Display (direct-solder friendly) ───────
// Pin order on display module left→right: GND, VCC, SCL, SDA
// Direct-solder to ESP32-C3: GND→GND, VCC→3.3V, SCL→GPIO4, SDA→GPIO3
#define OLED_I2C_SDA    3
#define OLED_I2C_SCL    4

// ─── I2C Bus 1 — MPU-6050 IMU ────────────────────────────────
#define IMU_I2C_SDA     6
#define IMU_I2C_SCL     7

// ─── MPU-6050 ────────────────────────────────────────────────
#define MPU_I2C_ADDR    0x68

// ─── Calibration Button ──────────────────────────────────────
// Momentary push button between GPIO5 and GND
// (moved from GPIO3 to free up I2C SDA for direct-solder display)
#define CAL_BUTTON_PIN      5
#define CAL_BUTTON_HOLD_MS  1000    // Hold 1 second to trigger calibration reset

// ─── SSD1306 OLED ────────────────────────────────────────────
#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET      -1

// ─── Boot screen ─────────────────────────────────────────────
#define BOOT_SCREEN_MS  2500        // How long to show boot screen (ms)

// ─── BLE ─────────────────────────────────────────────────────
#define BLE_DEVICE_NAME             "InclinoCar"
#define BLE_SERVICE_UUID            "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_PITCH_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_ROLL_UUID          "beb5483f-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_CALIBRATE_UUID     "beb54842-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_STATUS_UUID        "beb54843-36e1-4688-b7f5-ea07361b26a8"

// ─── ESP-NOW ─────────────────────────────────────────────────
#define ESPNOW_BROADCAST_ADDR   { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// ─── Sensor Fusion ───────────────────────────────────────────
#define COMP_FILTER_ALPHA   0.96f
#define UPDATE_INTERVAL_MS  50      // 20 Hz

// ─── Calibration ─────────────────────────────────────────────
#define CAL_SAMPLES         100
#define CAL_SETTLE_MS       500

// ─── Leveling thresholds ─────────────────────────────────────
#define LEVEL_THRESHOLD_DEG     0.5f
#define WARNING_THRESHOLD_DEG   2.0f

// ─── Firmware version ────────────────────────────────────────
// Overridden at build time via platformio.ini -DFW_VERSION="vX.X.X"
#ifndef FW_VERSION
  #define FW_VERSION "v0.1.14"
#endif
