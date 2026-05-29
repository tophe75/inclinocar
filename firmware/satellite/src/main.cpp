#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "config.h"

// ─── Display ──────────────────────────────────────────────────
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ─── Received data ────────────────────────────────────────────
struct CoreData {
  float pitch;
  float roll;
  bool  isCalibrated;
};

volatile CoreData rxData       = { 0.0f, 0.0f, false };
volatile bool     newData      = false;
unsigned long     lastRxTime   = 0;

// ─── ESP-NOW Receive Callback ─────────────────────────────────
void onDataReceive(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(CoreData)) {
    memcpy((void*)&rxData, data, sizeof(CoreData));
    newData    = true;
    lastRxTime = millis();
  }
}

// ─── Display helpers ──────────────────────────────────────────
void drawAngleBar(int x, int y, int w, float angle, float maxAngle) {
  // Outer frame
  display.drawRect(x, y, w, 7, SSD1306_WHITE);
  // Center tick
  display.drawFastVLine(x + w / 2, y, 7, SSD1306_WHITE);
  // Fill bar from center
  int center  = x + w / 2;
  int fill    = (int)((angle / maxAngle) * (w / 2));
  fill = constrain(fill, -(w / 2 - 2), (w / 2 - 2));
  if (fill > 0)      display.fillRect(center, y + 1, fill, 5, SSD1306_WHITE);
  else if (fill < 0) display.fillRect(center + fill, y + 1, -fill, 5, SSD1306_WHITE);
}

void updateDisplay(float pitch, float roll, bool calibrated, bool signal) {
  display.clearDisplay();

  if (!signal) {
    // ── No signal screen ──
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(22, 20);
    display.println("No Signal");
    display.setCursor(8, 34);
    display.println("Waiting for Core...");
    display.display();
    return;
  }

  if (!calibrated) {
    // ── Calibrating screen ──
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(16, 20);
    display.println("Calibrating...");
    display.setCursor(4, 34);
    display.println("Keep unit still");
    display.display();
    return;
  }

  // ── Main readout ──
  display.setTextColor(SSD1306_WHITE);

  // Title bar
  display.setTextSize(1);
  display.setCursor(24, 0);
  display.print("InclinoCar");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Pitch
  display.setCursor(0, 13);
  display.print("PITCH");
  display.setCursor(72, 13);
  char buf[12];
  snprintf(buf, sizeof(buf), "%+6.1f", pitch);
  display.print(buf);
  display.print((char)247);  // degree symbol
  drawAngleBar(0, 23, 127, pitch, 15.0f);

  // Roll
  display.setCursor(0, 34);
  display.print("ROLL ");
  display.setCursor(72, 34);
  snprintf(buf, sizeof(buf), "%+6.1f", roll);
  display.print(buf);
  display.print((char)247);
  drawAngleBar(0, 44, 127, roll, 15.0f);

  // Level status
  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  bool pitchOk = fabsf(pitch) <= 0.5f;
  bool rollOk  = fabsf(roll)  <= 0.5f;
  display.setCursor(0, 56);
  if (pitchOk && rollOk) {
    display.print("   >> LEVEL OK <<   ");
  } else {
    // Guidance
    if (!pitchOk) {
      display.print(pitch > 0 ? "Lower front  " : "Raise front  ");
    }
    if (!rollOk) {
      display.print(roll > 0 ? "Lower right" : "Lower left");
    }
  }

  display.display();
}

// ─── Setup ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== InclinoCar Satellite Display Unit ===");

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("[ERROR] SSD1306 not found! Check wiring.");
    while (1) delay(100);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 24);
  display.println("InclinoCar v0.1");
  display.setCursor(18, 36);
  display.println("Remote Display");
  display.display();
  Serial.println("[OK] OLED initialized");
  delay(1500);

  WiFi.mode(WIFI_STA);
  Serial.print("[INFO] Display MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed");
    while (1) delay(100);
  }
  esp_now_register_recv_cb(onDataReceive);
  Serial.println("[OK] ESP-NOW initialized — waiting for Core Unit");

  // Show waiting screen
  updateDisplay(0, 0, false, false);
}

// ─── Loop ─────────────────────────────────────────────────────
void loop() {
  if (newData) {
    newData = false;
    updateDisplay(rxData.pitch, rxData.roll, rxData.isCalibrated, true);
    Serial.printf("[RX] Pitch: %+6.2f  Roll: %+6.2f  Cal: %d\n",
                  rxData.pitch, rxData.roll, rxData.isCalibrated);
  }

  // Signal timeout check
  if (lastRxTime > 0 && millis() - lastRxTime > SIGNAL_TIMEOUT_MS) {
    updateDisplay(0, 0, false, false);
    lastRxTime = millis();  // Throttle redraws
  }
}
