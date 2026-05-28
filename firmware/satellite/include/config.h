#pragma once

// ─── I2C Pins ─────────────────────────────────────────────────
#define I2C_SDA     6
#define I2C_SCL     7

// ─── MPU-6050 ─────────────────────────────────────────────────
#define MPU_I2C_ADDR    0x68

// ─── ESP-NOW ──────────────────────────────────────────────────
// Replace with the actual MAC address of your CORE ESP32-C3
// Find it by running the core firmware and checking Serial output
#define CORE_MAC_ADDR   { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// ─── Sensor Fusion ────────────────────────────────────────────
#define COMP_FILTER_ALPHA   0.96f
#define UPDATE_INTERVAL_MS  50
