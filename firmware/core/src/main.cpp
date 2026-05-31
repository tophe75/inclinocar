#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <NimBLEDevice.h>
#include "config.h"

// ─── I2C buses ───────────────────────────────────────────────
// Wire  (Bus 0): OLED — SDA=GPIO3, SCL=GPIO4 (direct-solder friendly)
// Wire1 (Bus 1): IMU  — SDA=GPIO6, SCL=GPIO7
// Using Arduino's built-in Wire/Wire1 avoids double-init issues

// ─── Peripherals ─────────────────────────────────────────────
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
Adafruit_MPU6050 mpu;

bool oledOk = false;
bool imuOk  = false;

// ─── BLE ─────────────────────────────────────────────────────
NimBLEServer*         pServer     = nullptr;
NimBLECharacteristic* pCharPitch  = nullptr;
NimBLECharacteristic* pCharRoll   = nullptr;
NimBLECharacteristic* pCharCalib  = nullptr;
NimBLECharacteristic* pCharStatus = nullptr;
bool bleConnected = false;

// ─── State ───────────────────────────────────────────────────
float pitch          = 0.0f;
float roll           = 0.0f;
float filteredPitch  = 0.0f;
float filteredRoll   = 0.0f;
float calOffsetPitch = 0.0f;
float calOffsetRoll  = 0.0f;
bool  isCalibrated   = false;
bool  calibratePending = false;
bool  fakeDataEnabled  = false;
float fakePitch = 0.0f;
float fakeRoll  = 0.0f;

unsigned long lastUpdate     = 0;
unsigned long lastSensorTime = 0;

// Button debounce
bool          btnWasPressed = false;
unsigned long btnPressTime  = 0;

// ESP-NOW
struct CoreData { float pitch; float roll; bool isCalibrated; };
uint8_t broadcastAddr[] = ESPNOW_BROADCAST_ADDR;

// ─────────────────────────────────────────────────────────────
//  OLED helpers
// ─────────────────────────────────────────────────────────────
void oledClear() { display.clearDisplay(); display.setTextColor(SSD1306_WHITE); display.setTextSize(1); }

void showBootScreen(const char* line1 = nullptr, const char* line2 = nullptr) {
  if (!oledOk) return;
  oledClear();
  display.setTextSize(2);
  display.setCursor(8, 4);
  display.print("Inclino");
  display.setTextSize(1);
  display.setCursor(92, 10);
  display.print("CAR");
  display.drawLine(0, 26, 127, 26, SSD1306_WHITE);
  display.setCursor(0, 30);
  display.print("Firmware ");
  display.print(FW_VERSION);
  if (line1) { display.setCursor(0, 44); display.print(line1); }
  if (line2) { display.setCursor(0, 54); display.print(line2); }
  display.drawPixel(0, 0, SSD1306_WHITE); display.drawPixel(127, 0, SSD1306_WHITE);
  display.drawPixel(0,63, SSD1306_WHITE); display.drawPixel(127,63, SSD1306_WHITE);
  display.display();
}

void showMessage(const char* title, const char* l1 = nullptr,
                 const char* l2 = nullptr, const char* l3 = nullptr) {
  if (!oledOk) return;
  oledClear();
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 2); display.print(title);
  display.setTextColor(SSD1306_WHITE);
  if (l1) { display.setCursor(0, 16); display.print(l1); }
  if (l2) { display.setCursor(0, 28); display.print(l2); }
  if (l3) { display.setCursor(0, 40); display.print(l3); }
  display.display();
}

void drawBar(int x, int y, int w, float angle, float maxA) {
  display.drawRect(x, y, w, 7, SSD1306_WHITE);
  display.drawFastVLine(x + w/2, y, 7, SSD1306_WHITE);
  int fill = constrain((int)((angle/maxA)*(w/2)), -(w/2-2), (w/2-2));
  if (fill > 0) display.fillRect(x+w/2,      y+1,  fill, 5, SSD1306_WHITE);
  if (fill < 0) display.fillRect(x+w/2+fill, y+1, -fill, 5, SSD1306_WHITE);
}

void updateDisplay() {
  if (!oledOk) return;
  if (!imuOk && !fakeDataEnabled) {
    showMessage(" No IMU ", "Check wiring:", "SDA=GPIO6 SCL=GPIO7", "AD0=GND");
    return;
  }
  oledClear();
  display.setCursor(24, 0); display.print("InclinoCar");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
  char buf[12];
  display.setCursor(0, 13); display.print("PITCH");
  snprintf(buf, sizeof(buf), "%+6.1f", pitch);
  display.setCursor(72, 13); display.print(buf); display.print((char)247);
  drawBar(0, 23, 127, pitch, 15.0f);
  display.setCursor(0, 34); display.print("ROLL ");
  snprintf(buf, sizeof(buf), "%+6.1f", roll);
  display.setCursor(72, 34); display.print(buf); display.print((char)247);
  drawBar(0, 44, 127, roll, 15.0f);
  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  display.setCursor(0, 56);
  bool levelOk = fabsf(pitch) <= LEVEL_THRESHOLD_DEG && fabsf(roll) <= LEVEL_THRESHOLD_DEG;
  if (levelOk) { display.print("   >> LEVEL OK <<   "); }
  else {
    if (fabsf(pitch) > LEVEL_THRESHOLD_DEG) display.print(pitch > 0 ? "Lower front " : "Raise front ");
    if (fabsf(roll)  > LEVEL_THRESHOLD_DEG) display.print(roll  > 0 ? "Rgt up" : "Lft up");
  }
  display.setCursor(108, 0); display.print(bleConnected ? "B+" : "B-");
  if (fakeDataEnabled) { display.setCursor(0, 0); display.print("SIM"); }
  display.display();
}

// ─────────────────────────────────────────────────────────────
//  BLE
// ─────────────────────────────────────────────────────────────
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*)    override { bleConnected = true;  Serial.println("[BLE] Connected"); }
  void onDisconnect(NimBLEServer*) override { bleConnected = false; Serial.println("[BLE] Disconnected"); NimBLEDevice::startAdvertising(); }
};

class CalibCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    auto v = c->getValue();
    if (v.length() > 0 && v[0] == 0x01) { calibratePending = true; Serial.println("[BLE] Cal requested"); }
  }
};

void setupBLE() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCB());
  auto* svc = pServer->createService(BLE_SERVICE_UUID);
  pCharPitch  = svc->createCharacteristic(BLE_CHAR_PITCH_UUID,    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pCharRoll   = svc->createCharacteristic(BLE_CHAR_ROLL_UUID,     NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pCharCalib  = svc->createCharacteristic(BLE_CHAR_CALIBRATE_UUID, NIMBLE_PROPERTY::WRITE);
  pCharCalib->setCallbacks(new CalibCB());
  pCharStatus = svc->createCharacteristic(BLE_CHAR_STATUS_UUID,   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  svc->start();
  auto* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("[BLE] Advertising as " BLE_DEVICE_NAME);
}

// ─────────────────────────────────────────────────────────────
//  Calibration
// ─────────────────────────────────────────────────────────────
void runCalibration() {
  if (!imuOk) return;
  Serial.println("[CAL] Starting — keep unit still");
  isCalibrated = false;
  showMessage(" Calibrating ", "Keep unit still", "Do not move...");
  delay(CAL_SETTLE_MS);
  double sp = 0, sr = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    sp += atan2(a.acceleration.y,  a.acceleration.z) * RAD_TO_DEG;
    sr += atan2(-a.acceleration.x, a.acceleration.z) * RAD_TO_DEG;
    delay(10);
  }
  calOffsetPitch = sp / CAL_SAMPLES;
  calOffsetRoll  = sr / CAL_SAMPLES;
  filteredPitch  = 0.0f;
  filteredRoll   = 0.0f;
  isCalibrated   = true;
  Serial.printf("[CAL] Done. pitch_off=%.2f roll_off=%.2f\n", calOffsetPitch, calOffsetRoll);
  if (bleConnected) { uint8_t d = 0x01; pCharStatus->setValue(&d, 1); pCharStatus->notify(); }
}

// ─────────────────────────────────────────────────────────────
//  IMU
// ─────────────────────────────────────────────────────────────
void updateIMU() {
  if (!imuOk || !isCalibrated) return;
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  unsigned long now = millis();
  float dt = (now - lastSensorTime) / 1000.0f;
  lastSensorTime = now;
  float ap = atan2(a.acceleration.y,  a.acceleration.z) * RAD_TO_DEG - calOffsetPitch;
  float ar = atan2(-a.acceleration.x, a.acceleration.z) * RAD_TO_DEG - calOffsetRoll;
  filteredPitch = COMP_FILTER_ALPHA*(filteredPitch + g.gyro.x*dt*RAD_TO_DEG) + (1-COMP_FILTER_ALPHA)*ap;
  filteredRoll  = COMP_FILTER_ALPHA*(filteredRoll  + g.gyro.y*dt*RAD_TO_DEG) + (1-COMP_FILTER_ALPHA)*ar;
  pitch = filteredPitch;
  roll  = filteredRoll;
}

// ─────────────────────────────────────────────────────────────
//  Button
// ─────────────────────────────────────────────────────────────
void handleButton() {
  bool pressed = (digitalRead(CAL_BUTTON_PIN) == LOW);
  if (pressed && !btnWasPressed) {
    btnPressTime  = millis();
    btnWasPressed = true;
  }
  if (!pressed && btnWasPressed) {
    btnWasPressed = false;
  }
  if (pressed && btnWasPressed && (millis() - btnPressTime >= CAL_BUTTON_HOLD_MS)) {
    calibratePending = true;
    btnWasPressed    = false;
    Serial.println("[BTN] Cal hold detected");
  }
}

// ─────────────────────────────────────────────────────────────
//  Serial commands
// ─────────────────────────────────────────────────────────────
void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  Serial.println();
  if (line == "help") {
    Serial.println("Commands: help, status, version, mac, cal, reboot");
    Serial.println("          pitch <val>, roll <val>, fake off");
  } else if (line == "version") {
    Serial.println("Firmware: " + String(FW_VERSION));
  } else if (line == "mac") {
    Serial.print("MAC: "); Serial.println(WiFi.macAddress());
  } else if (line == "status") {
    Serial.println("--- Status ---");
    Serial.printf("  OLED:    %s (GPIO3/4)\n",   oledOk ? "OK" : "NOT FOUND");
    Serial.printf("  IMU:     %s (GPIO6/7)\n",   imuOk  ? "OK" : "NOT FOUND");
    Serial.printf("  Cal:     %s\n", isCalibrated ? "YES" : "NO");
    Serial.printf("  BLE:     %s\n", bleConnected ? "connected" : "advertising");
    Serial.printf("  Pitch:   %+.2f deg\n", pitch);
    Serial.printf("  Roll:    %+.2f deg\n", roll);
    Serial.printf("  Fake:    %s\n", fakeDataEnabled ? "ON" : "off");
  } else if (line == "cal") {
    calibratePending = true;
  } else if (line == "reboot") {
    Serial.println("Rebooting..."); delay(200); ESP.restart();
  } else if (line == "fake off") {
    fakeDataEnabled = false; Serial.println("Fake data off");
  } else if (line.startsWith("pitch ")) {
    fakePitch = line.substring(6).toFloat();
    pitch = fakePitch; fakeDataEnabled = true; isCalibrated = true;
    Serial.printf("Fake pitch: %+.2f\n", fakePitch);
  } else if (line.startsWith("roll ")) {
    fakeRoll = line.substring(5).toFloat();
    roll = fakeRoll; fakeDataEnabled = true; isCalibrated = true;
    Serial.printf("Fake roll: %+.2f\n", fakeRoll);
  } else {
    Serial.printf("Unknown: '%s' (try 'help')\n", line.c_str());
  }
}

// ─────────────────────────────────────────────────────────────
//  Setup
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== InclinoCar Core Unit " + String(FW_VERSION) + " ===");

  pinMode(CAL_BUTTON_PIN, INPUT_PULLUP);

  // OLED on Bus 0 (GPIO3=SDA, GPIO4=SCL)
  Wire.begin(OLED_I2C_SDA, OLED_I2C_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    oledOk = true;
    Serial.println("[OK] OLED GPIO3/4");
    showBootScreen("Initialising...");
  } else {
    Serial.println("[WARN] OLED not found on GPIO3/4");
  }

  // IMU on Bus 1 (GPIO6=SDA, GPIO7=SCL)
  Wire1.begin(IMU_I2C_SDA, IMU_I2C_SCL);
  if (mpu.begin(MPU_I2C_ADDR, &Wire1)) {
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    imuOk = true;
    Serial.println("[OK] IMU GPIO6/7");
    showBootScreen("OLED OK", "IMU OK");
  } else {
    Serial.println("[WARN] IMU not found on GPIO6/7");
    showBootScreen("OLED OK", "! IMU not found");
  }

  // ESP-NOW
  WiFi.mode(WIFI_STA);
  Serial.print("[INFO] MAC: "); Serial.println(WiFi.macAddress());
  if (esp_now_init() == ESP_OK) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastAddr, 6);
    esp_now_add_peer(&peer);
    Serial.println("[OK] ESP-NOW ready");
  }

  setupBLE();
  lastSensorTime = millis();

  // Hold boot screen then calibrate
  delay(BOOT_SCREEN_MS);
  if (imuOk) runCalibration();

  Serial.println("[OK] Ready");
}

// ─────────────────────────────────────────────────────────────
//  Loop
// ─────────────────────────────────────────────────────────────
void loop() {
  handleSerial();
  handleButton();

  if (calibratePending) {
    calibratePending = false;
    runCalibration();
  }

  unsigned long now = millis();
  if (now - lastUpdate < UPDATE_INTERVAL_MS) return;
  lastUpdate = now;

  if (fakeDataEnabled) {
    pitch = fakePitch;
    roll  = fakeRoll;
  } else {
    updateIMU();
  }

  if (isCalibrated || fakeDataEnabled) {
    if (bleConnected) {
      pCharPitch->setValue(pitch); pCharPitch->notify();
      pCharRoll->setValue(roll);   pCharRoll->notify();
    }
    CoreData d = { pitch, roll, isCalibrated };
    esp_now_send(broadcastAddr, (uint8_t*)&d, sizeof(d));
    Serial.printf("[IMU] P:%+6.2f R:%+6.2f\n", pitch, roll);
  }

  updateDisplay();
}
