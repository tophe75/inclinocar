#pragma once

// ─── I2C Pins ────────────────────────────────────────────────
#define I2C_SDA         6
#define I2C_SCL         7

// ─── MPU-6050 ────────────────────────────────────────────────
#define MPU_I2C_ADDR    0x68
#define MPU_INT_PIN     4       // Optional interrupt pin

// ─── SSD1306 OLED ────────────────────────────────────────────
#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET      -1      // No reset pin

// ─── BLE ─────────────────────────────────────────────────────
#define BLE_DEVICE_NAME             "InclinoCar"
#define BLE_SERVICE_UUID            "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CHAR_PITCH_UUID         "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_ROLL_UUID          "beb5483f-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_SAT_PITCH_UUID     "beb54840-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_SAT_ROLL_UUID      "beb54841-36e1-4688-b7f5-ea07361b26a8"

// ─── ESP-NOW ─────────────────────────────────────────────────
// Set USE_ESPNOW=1 in platformio.ini build_flags to enable
// Replace with the actual MAC address of your satellite ESP32
#define SAT_MAC_ADDR    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// ─── Sensor Fusion ───────────────────────────────────────────
// Complementary filter coefficient (0.0 = pure accel, 1.0 = pure gyro)
#define COMP_FILTER_ALPHA   0.96f

// Update interval in milliseconds
#define UPDATE_INTERVAL_MS  50      // 20 Hz

// ─── Leveling thresholds ─────────────────────────────────────
#define LEVEL_THRESHOLD_DEG     0.5f    // Within this = level (green)
#define WARNING_THRESHOLD_DEG   2.0f    // Within this = close (yellow)
