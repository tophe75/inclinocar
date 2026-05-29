#pragma once

// ─── I2C Pins ────────────────────────────────────────────────
#define I2C_SDA         6
#define I2C_SCL         7

// ─── MPU-6050 ────────────────────────────────────────────────
#define MPU_I2C_ADDR    0x68
#define MPU_INT_PIN     4

// ─── Calibration Button ──────────────────────────────────────
// Momentary push button between GPIO3 and GND
#define CAL_BUTTON_PIN      3
#define CAL_BUTTON_HOLD_MS  1000    // Hold 1 second to trigger calibration reset

// ─── SSD1306 OLED ────────────────────────────────────────────
#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET      -1

// ─── BLE ─────────────────────────────────────────────────────
#define BLE_DEVICE_NAME             "InclinoCar"
#define BLE_SERVICE_UUID            "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_PITCH_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_ROLL_UUID          "beb5483f-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_CALIBRATE_UUID     "beb54842-36e1-4688-b7f5-ea07361b26a8"  // Write to trigger cal reset
#define BLE_CHAR_STATUS_UUID        "beb54843-36e1-4688-b7f5-ea07361b26a8"  // Notify: calibration status

// ─── ESP-NOW ─────────────────────────────────────────────────
// Broadcast to all ESP-NOW peers (satellite display receives automatically)
#define ESPNOW_BROADCAST_ADDR   { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// ─── Sensor Fusion ───────────────────────────────────────────
#define COMP_FILTER_ALPHA   0.96f
#define UPDATE_INTERVAL_MS  50      // 20 Hz

// ─── Calibration ─────────────────────────────────────────────
// Number of samples averaged during calibration
#define CAL_SAMPLES         100
// Settle time before sampling starts (ms)
#define CAL_SETTLE_MS       500

// ─── Leveling thresholds ─────────────────────────────────────
#define LEVEL_THRESHOLD_DEG     0.5f
#define WARNING_THRESHOLD_DEG   2.0f
