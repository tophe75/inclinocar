#pragma once

// ─── I2C Pins ─────────────────────────────────────────────────
#define I2C_SDA     6
#define I2C_SCL     7

// ─── SSD1306 OLED ─────────────────────────────────────────────
#define OLED_I2C_ADDR   0x3C
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_RESET      -1

// ─── ESP-NOW ──────────────────────────────────────────────────
// No MAC config needed — satellite only receives, never transmits

// ─── Display timeouts ─────────────────────────────────────────
// Show "No signal" after this many ms without a packet
#define SIGNAL_TIMEOUT_MS   3000
