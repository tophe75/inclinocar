#pragma once

// ─── I2C Bus 0 — OLED Display (direct-solder friendly) ───────
// Pin order on display module left→right: GND, VCC, SCL, SDA
// Direct-solder to ESP32-C3: GND→GND, VCC→3.3V, SCL→GPIO4, SDA→GPIO3
#define OLED_I2C_SDA    3
#define OLED_I2C_SCL    4

// ─── SSD1306 OLED ─────────────────────────────────────────────
#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET      -1

// ─── Boot screen ──────────────────────────────────────────────
#define BOOT_SCREEN_MS  2500

// ─── ESP-NOW ──────────────────────────────────────────────────
#define SIGNAL_TIMEOUT_MS   3000

// ─── Firmware version ─────────────────────────────────────────
// Set via platformio.ini build_flag, falls back if not defined
#ifndef FW_VERSION
  #define FW_VERSION "v?.?.?"
#endif
