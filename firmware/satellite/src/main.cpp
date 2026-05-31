#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "config.h"

// ─── I2C Bus ─────────────────────────────────────────────────


// ─── Display ─────────────────────────────────────────────────
TwoWire oledWire(0);
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &oledWire, OLED_RESET);
bool oledOk = false;

// ─── Received data ────────────────────────────────────────────
struct CoreData {
  float pitch;
  float roll;
  bool  isCalibrated;
};

volatile CoreData rxData     = { 0.0f, 0.0f, false };
volatile bool     newData    = false;
unsigned long     lastRxTime = 0;

// ─── ESP-NOW Receive Callback ─────────────────────────────────
void onDataReceive(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(CoreData)) {
    memcpy((void*)&rxData, data, sizeof(CoreData));
    newData    = true;
    lastRxTime = millis();
  }
}

// ─── OLED helpers ─────────────────────────────────────────────
void drawAngleBar(int x, int y, int w, float angle, float maxAngle) {
  display.drawRect(x, y, w, 7, SSD1306_WHITE);
  display.drawFastVLine(x + w / 2, y, 7, SSD1306_WHITE);
  int center = x + w / 2;
  int fill   = (int)((angle / maxAngle) * (w / 2));
  fill = constrain(fill, -(w / 2 - 2), (w / 2 - 2));
  if (fill > 0)      display.fillRect(center,        y + 1, fill,  5, SSD1306_WHITE);
  else if (fill < 0) display.fillRect(center + fill, y + 1, -fill, 5, SSD1306_WHITE);
}

void showBootScreen(const char* status1 = nullptr, const char* status2 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(8, 4);
  display.print("Inclino");
  display.setTextSize(1);
  display.setCursor(92, 10);
  display.print("CAR");

  display.drawLine(0, 26, 127, 26, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 30);
  display.print("Firmware ");
  display.print(FW_VERSION);
  display.setCursor(0, 40);
  display.print("Remote Display");

  if (status1) { display.setCursor(0, 52); display.print(status1); }
  if (status2) { display.setCursor(64,52); display.print(status2); }

  display.drawPixel(0,   0,   SSD1306_WHITE);
  display.drawPixel(127, 0,   SSD1306_WHITE);
  display.drawPixel(0,   63,  SSD1306_WHITE);
  display.drawPixel(127, 63,  SSD1306_WHITE);

  display.display();
}

void showStatusScreen(const char* title, const char* line1,
                      const char* line2 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print(title);
  display.setTextColor(SSD1306_WHITE);
  if (line1) { display.setCursor(0, 18); display.print(line1); }
  if (line2) { display.setCursor(0, 30); display.print(line2); }
  display.display();
}

void updateDisplay(float pitch, float roll, bool calibrated, bool signal) {
  display.clearDisplay();

  if (!signal) {
    showStatusScreen(" No Signal ", "Waiting for Core...", "Is Core powered on?");
    return;
  }
  if (!calibrated) {
    showStatusScreen(" Calibrating ", "Keep Core unit", "still for 2s...");
    return;
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(24, 0);
  display.print("InclinoCar");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  char buf[12];
  display.setCursor(0, 13);
  display.print("PITCH");
  snprintf(buf, sizeof(buf), "%+6.1f", pitch);
  display.setCursor(72, 13);
  display.print(buf);
  display.print((char)247);
  drawAngleBar(0, 23, 127, pitch, 15.0f);

  display.setCursor(0, 34);
  display.print("ROLL ");
  snprintf(buf, sizeof(buf), "%+6.1f", roll);
  display.setCursor(72, 34);
  display.print(buf);
  display.print((char)247);
  drawAngleBar(0, 44, 127, roll, 15.0f);

  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  bool pitchOk = fabsf(pitch) <= 0.5f;
  bool rollOk  = fabsf(roll)  <= 0.5f;
  display.setCursor(0, 56);
  if (pitchOk && rollOk) {
    display.print("   >> LEVEL OK <<   ");
  } else {
    if (!pitchOk) display.print(pitch > 0 ? "Lower front  " : "Raise front  ");
    if (!rollOk)  display.print(roll  > 0 ? "Lower right" : "Lower left");
  }

  display.display();
}

// ─── Setup ────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== InclinoCar Satellite Display " FW_VERSION " ===");

  oledWire.begin(OLED_I2C_SDA, OLED_I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR, false)) {
    Serial.println("[ERROR] SSD1306 not found! Check GPIO3(SDA)/GPIO4(SCL)");
    oledOk = false;
    // Carry on — Serial is still useful
  } else {
    oledOk = true;
    Serial.println("[OK] OLED on GPIO3(SDA)/GPIO4(SCL)");
  }

  WiFi.mode(WIFI_STA);
  Serial.print("[INFO] Satellite MAC: ");
  Serial.println(WiFi.macAddress());

  // Show boot screen before ESP-NOW init
  if (oledOk) showBootScreen("ESP-NOW init...");

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed");
    if (oledOk) showBootScreen("ESP-NOW OK", "! ESP-NOW FAIL");
  } else {
    esp_now_register_recv_cb(onDataReceive);
    Serial.println("[OK] ESP-NOW initialized — waiting for Core");
    if (oledOk) showBootScreen("ESP-NOW OK");
  }

  delay(BOOT_SCREEN_MS);

  // Show waiting screen after boot
  if (oledOk) showStatusScreen(" No Signal ", "Waiting for Core...", "Is Core powered on?");
}

// ─── Loop ─────────────────────────────────────────────────────
void loop() {
  if (!oledOk) return;

  if (newData) {
    newData = false;
    updateDisplay(rxData.pitch, rxData.roll, rxData.isCalibrated, true);
    Serial.printf("[RX] Pitch: %+6.2f  Roll: %+6.2f  Cal: %d\n",
                  rxData.pitch, rxData.roll, rxData.isCalibrated);
  }

  if (lastRxTime > 0 && millis() - lastRxTime > SIGNAL_TIMEOUT_MS) {
    updateDisplay(0, 0, false, false);
    lastRxTime = millis();
  }
}
