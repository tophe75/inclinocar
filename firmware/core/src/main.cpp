#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <NimBLEDevice.h>
#include "config.h"

#if USE_OLED
  #include <Adafruit_SSD1306.h>
  #include <Adafruit_GFX.h>
#endif

#if USE_IMU
  #include <Adafruit_MPU6050.h>
  #include <Adafruit_Sensor.h>
#endif

// ─── I2C buses ───────────────────────────────────────────────
TwoWire WireOLED = TwoWire(0);   // Bus 0 — display  (GPIO3/4)
TwoWire WireIMU  = TwoWire(1);   // Bus 1 — MPU-6050 (GPIO6/7)

// ─── Peripherals ─────────────────────────────────────────────
#if USE_OLED
  Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &WireOLED, OLED_RESET);
  bool oledOk = false;
#endif

#if USE_IMU
  Adafruit_MPU6050 mpu;
  bool imuOk = false;
#endif

// ─── BLE ─────────────────────────────────────────────────────
NimBLEServer*         pServer     = nullptr;
NimBLECharacteristic* pCharPitch  = nullptr;
NimBLECharacteristic* pCharRoll   = nullptr;
NimBLECharacteristic* pCharCalib  = nullptr;
NimBLECharacteristic* pCharStatus = nullptr;

bool bleConnected = false;

// ─── State ───────────────────────────────────────────────────
float pitch         = 0.0f;
float roll          = 0.0f;
float filteredPitch = 0.0f;
float filteredRoll  = 0.0f;

float   calOffsetPitch  = 0.0f;
float   calOffsetRoll   = 0.0f;
bool    isCalibrated    = false;
bool    calibratePending = false;

unsigned long btnPressTime  = 0;
bool          btnWasPressed = false;
unsigned long lastUpdate    = 0;
unsigned long lastSensorTime = 0;

// ESP-NOW
struct CoreData { float pitch; float roll; bool isCalibrated; };

// ─── Fake data override (set via serial commands) ────────────
bool  fakeDataEnabled = false;
float fakePitch       = 0.0f;
float fakeRoll        = 0.0f;
uint8_t broadcastAddr[] = ESPNOW_BROADCAST_ADDR;

// ─── OLED helpers ────────────────────────────────────────────
#if USE_OLED

void drawAngleBar(int x, int y, int w, float angle, float maxAngle) {
  display.drawRect(x, y, w, 7, SSD1306_WHITE);
  display.drawFastVLine(x + w / 2, y, 7, SSD1306_WHITE);
  int center = x + w / 2;
  int fill   = (int)((angle / maxAngle) * (w / 2));
  fill = constrain(fill, -(w / 2 - 2), (w / 2 - 2));
  if (fill > 0)      display.fillRect(center,        y + 1, fill,  5, SSD1306_WHITE);
  else if (fill < 0) display.fillRect(center + fill, y + 1, -fill, 5, SSD1306_WHITE);
}

// ── Boot screen ──────────────────────────────────────────────
void showBootScreen(const char* status1 = nullptr, const char* status2 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Logo area — top half
  display.setTextSize(2);
  display.setCursor(8, 4);
  display.print("Inclino");
  display.setTextSize(1);
  display.setCursor(92, 10);
  display.print("CAR");

  // Divider
  display.drawLine(0, 26, 127, 26, SSD1306_WHITE);

  // Version
  display.setTextSize(1);
  display.setCursor(0, 30);
  display.print("Firmware ");
  display.print(FW_VERSION);

  // Status lines
  if (status1) {
    display.setCursor(0, 44);
    display.print(status1);
  }
  if (status2) {
    display.setCursor(0, 54);
    display.print(status2);
  }

  // Corner dots — decorative
  display.drawPixel(0,   0,   SSD1306_WHITE);
  display.drawPixel(127, 0,   SSD1306_WHITE);
  display.drawPixel(0,   63,  SSD1306_WHITE);
  display.drawPixel(127, 63,  SSD1306_WHITE);

  display.display();
}

// ── Error / status screen ────────────────────────────────────
void showStatusScreen(const char* title, const char* line1,
                      const char* line2 = nullptr, const char* line3 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Title bar
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print(title);
  display.setTextColor(SSD1306_WHITE);

  if (line1) { display.setCursor(0, 16); display.print(line1); }
  if (line2) { display.setCursor(0, 28); display.print(line2); }
  if (line3) { display.setCursor(0, 40); display.print(line3); }

  display.display();
}

// ── Main angle readout ───────────────────────────────────────
void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Title
  display.setCursor(24, 0);
  display.print("InclinoCar");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

#if USE_IMU
  if (!imuOk) {
    display.setCursor(0, 16);
    display.print("! IMU not found");
    display.setCursor(0, 28);
    display.print("Check wiring");
    display.setCursor(0, 40);
    display.print("GPIO6=SDA GPIO7=SCL");
    display.display();
    return;
  }

  // Pitch
  char buf[12];
  display.setCursor(0, 13);
  display.print("PITCH");
  snprintf(buf, sizeof(buf), "%+6.1f", pitch);
  display.setCursor(72, 13);
  display.print(buf);
  display.print((char)247);
  drawAngleBar(0, 23, 127, pitch, 15.0f);

  // Roll
  display.setCursor(0, 34);
  display.print("ROLL ");
  snprintf(buf, sizeof(buf), "%+6.1f", roll);
  display.setCursor(72, 34);
  display.print(buf);
  display.print((char)247);
  drawAngleBar(0, 44, 127, roll, 15.0f);

  // Status bar
  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  bool pitchOk = fabsf(pitch) <= LEVEL_THRESHOLD_DEG;
  bool rollOk  = fabsf(roll)  <= LEVEL_THRESHOLD_DEG;
  display.setCursor(0, 56);
  if (pitchOk && rollOk) {
    display.print("   >> LEVEL OK <<   ");
  } else {
    if (!pitchOk) display.print(pitch > 0 ? "Lower front  " : "Raise front  ");
    if (!rollOk)  display.print(roll  > 0 ? "Lower right" : "Lower left");
  }
#else
  display.setCursor(0, 20);
  display.print("IMU disabled");
  display.setCursor(0, 32);
  display.print("Build: USE_IMU=1");
#endif

  // BLE indicator top-right
  display.setCursor(108, 0);
  display.print(bleConnected ? "B+" : "B-");

  display.display();
}
#endif  // USE_OLED

// ─── BLE callbacks ───────────────────────────────────────────
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pSrv) override {
    bleConnected = true;
    Serial.println("[BLE] Client connected");
  }
  void onDisconnect(NimBLEServer* pSrv) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected");
    NimBLEDevice::startAdvertising();
  }
};

class CalibWriteCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar) override {
    std::string val = pChar->getValue();
    if (val.length() > 0 && val[0] == 0x01) {
      calibratePending = true;
      Serial.println("[BLE] Calibration reset requested via app");
    }
  }
};

// ─── Calibration ─────────────────────────────────────────────
void runCalibration() {
#if USE_IMU
  if (!imuOk) return;

  Serial.println("[CAL] Starting — keep unit still");
  isCalibrated = false;

#if USE_OLED
  if (oledOk) showStatusScreen(" Calibrating ", "Keep unit still", "Do not move...");
#endif

  delay(CAL_SETTLE_MS);

  double sumPitch = 0, sumRoll = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sensors_event_t accel, gyro, temp;
    mpu.getEvent(&accel, &gyro, &temp);
    sumPitch += atan2(accel.acceleration.y, accel.acceleration.z) * RAD_TO_DEG;
    sumRoll  += atan2(-accel.acceleration.x, accel.acceleration.z) * RAD_TO_DEG;
    delay(10);
  }
  calOffsetPitch = sumPitch / CAL_SAMPLES;
  calOffsetRoll  = sumRoll  / CAL_SAMPLES;
  filteredPitch  = 0.0f;
  filteredRoll   = 0.0f;
  isCalibrated   = true;

  Serial.printf("[CAL] Done. Offsets: pitch=%.2f roll=%.2f\n", calOffsetPitch, calOffsetRoll);

  if (bleConnected) {
    uint8_t done = 0x01;
    pCharStatus->setValue(&done, 1);
    pCharStatus->notify();
  }
#endif
}

// ─── IMU update ──────────────────────────────────────────────
void updateIMU() {
#if USE_IMU
  if (!imuOk) return;
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);
  unsigned long now = millis();
  float dt = (now - lastSensorTime) / 1000.0f;
  lastSensorTime = now;
  float accelPitch = atan2(accel.acceleration.y,  accel.acceleration.z) * RAD_TO_DEG - calOffsetPitch;
  float accelRoll  = atan2(-accel.acceleration.x, accel.acceleration.z) * RAD_TO_DEG - calOffsetRoll;
  filteredPitch = COMP_FILTER_ALPHA * (filteredPitch + gyro.gyro.x * dt * RAD_TO_DEG)
                + (1.0f - COMP_FILTER_ALPHA) * accelPitch;
  filteredRoll  = COMP_FILTER_ALPHA * (filteredRoll  + gyro.gyro.y * dt * RAD_TO_DEG)
                + (1.0f - COMP_FILTER_ALPHA) * accelRoll;
  pitch = filteredPitch;
  roll  = filteredRoll;
#endif
}

// ─── Button ──────────────────────────────────────────────────
void handleButton() {
  bool pressed = (digitalRead(CAL_BUTTON_PIN) == LOW);
  if (pressed && !btnWasPressed) {
    btnPressTime  = millis();
    btnWasPressed = true;
  } else if (!pressed && btnWasPressed) {
    btnWasPressed = false;
  } else if (pressed && btnWasPressed) {
    if (millis() - btnPressTime >= CAL_BUTTON_HOLD_MS) {
      calibratePending = true;
      btnWasPressed    = false;
      Serial.println("[BTN] Calibration hold detected");
    }
  }
}

// ─── BLE setup ───────────────────────────────────────────────
void setupBLE() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  NimBLEService* pSvc = pServer->createService(BLE_SERVICE_UUID);

  pCharPitch = pSvc->createCharacteristic(BLE_CHAR_PITCH_UUID,
                  NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pCharRoll  = pSvc->createCharacteristic(BLE_CHAR_ROLL_UUID,
                  NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pCharCalib = pSvc->createCharacteristic(BLE_CHAR_CALIBRATE_UUID,
                  NIMBLE_PROPERTY::WRITE);
  pCharCalib->setCallbacks(new CalibWriteCallback());
  pCharStatus = pSvc->createCharacteristic(BLE_CHAR_STATUS_UUID,
                  NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  pSvc->start();
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("[BLE] Advertising as " BLE_DEVICE_NAME);
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== InclinoCar Core Unit " + String(FW_VERSION) + " ===");

  pinMode(CAL_BUTTON_PIN, INPUT_PULLUP);

  // ── Init OLED bus (GPIO3=SDA, GPIO4=SCL) ──
#if USE_OLED
  WireOLED.begin(OLED_I2C_SDA, OLED_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("[WARN] SSD1306 not found (GPIO3/4) — display disabled");
    oledOk = false;
  } else {
    oledOk = true;
    Serial.println("[OK] OLED on GPIO3(SDA)/GPIO4(SCL)");
    // Boot screen — show immediately, status will be filled in below
    showBootScreen("Initialising...");
  }
#endif

  // ── Init IMU bus (GPIO6=SDA, GPIO7=SCL) ──
#if USE_IMU
  WireIMU.begin(IMU_I2C_SDA, IMU_I2C_SCL);
  if (!mpu.begin(MPU_I2C_ADDR, &WireIMU)) {
    Serial.println("[WARN] MPU-6050 not found (GPIO6/7)");
    imuOk = false;
#if USE_OLED
    if (oledOk) showBootScreen("OLED OK", "! IMU not found");
#endif
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
    mpu.setGyroRange(MPU6050_RANGE_250_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    imuOk = true;
    Serial.println("[OK] MPU-6050 on GPIO6(SDA)/GPIO7(SCL)");
#if USE_OLED
    if (oledOk) showBootScreen("OLED OK", "IMU OK");
#endif
  }
#else
  // IMU disabled in build flags
#if USE_OLED
  if (oledOk) showBootScreen("OLED OK", "IMU: disabled");
#endif
#endif

  // ── ESP-NOW ──
#if USE_ESPNOW
  WiFi.mode(WIFI_STA);
  Serial.print("[INFO] Core MAC: ");
  Serial.println(WiFi.macAddress());
  if (esp_now_init() != ESP_OK) {
    Serial.println("[WARN] ESP-NOW init failed");
  } else {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastAddr, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    Serial.println("[OK] ESP-NOW broadcast ready");
  }
#endif

  setupBLE();
  lastSensorTime = millis();

  // Show final boot screen status and hold for BOOT_SCREEN_MS
#if USE_OLED
  if (oledOk) {
  #if USE_IMU
    if (imuOk)
      showBootScreen("OLED OK  IMU OK", "Calibrating...");
    else
      showBootScreen("OLED OK", "! IMU not found");
  #else
    showBootScreen("OLED OK", "IMU: disabled");
  #endif
  }
#endif

  Serial.println("[BOOT] Showing boot screen...");
  delay(BOOT_SCREEN_MS);

#if USE_IMU
  if (imuOk) {
    runCalibration();
  }
#endif

  Serial.println("[OK] Setup complete — entering main loop");
}

// ─── Loop ────────────────────────────────────────────────────
// ─── Serial command handler ──────────────────────────────────
void handleSerial() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  Serial.println();

  // ── help ──
  if (line == "help") {
    Serial.println("InclinoCar Serial Commands");
    Serial.println("─────────────────────────────────────");
    Serial.println("  help           This help text");
    Serial.println("  status         System status summary");
    Serial.println("  version        Firmware version");
    Serial.println("  mac            Device MAC address");
    Serial.println("  cal            Trigger calibration reset");
    Serial.println("  pitch <val>    Inject fake pitch (e.g. pitch 5.2)");
    Serial.println("  roll <val>     Inject fake roll  (e.g. roll -3.1)");
    Serial.println("  fake off       Disable fake data, use real IMU");
    Serial.println("  reboot         Reboot the ESP32");
    Serial.println("─────────────────────────────────────");
    return;
  }

  // ── version ──
  if (line == "version") {
    Serial.println("Firmware:  " + String(FW_VERSION));
    Serial.println("Device:    InclinoCar Core Unit");
    return;
  }

  // ── mac ──
  if (line == "mac") {
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    return;
  }

  // ── status ──
  if (line == "status") {
    Serial.println("─── System Status ───────────────────");
#if USE_OLED
    Serial.printf("  OLED:      %s (GPIO3/4)\n", oledOk   ? "OK" : "NOT FOUND");
#else
    Serial.println("  OLED:      disabled in build");
#endif
#if USE_IMU
    Serial.printf("  IMU:       %s (GPIO6/7)\n", imuOk    ? "OK" : "NOT FOUND");
    Serial.printf("  Calibrated:%s\n",            isCalibrated ? "YES" : "NO");
    if (isCalibrated) {
      Serial.printf("  Offsets:   pitch=%.2f  roll=%.2f\n", calOffsetPitch, calOffsetRoll);
    }
    Serial.printf("  Pitch:     %+.2f deg\n", pitch);
    Serial.printf("  Roll:      %+.2f deg\n", roll);
#else
    Serial.println("  IMU:       disabled in build");
#endif
    Serial.printf("  BLE:       %s\n",   bleConnected ? "connected" : "advertising");
    Serial.printf("  Fake data: %s\n",   fakeDataEnabled ? "ON" : "off");
    Serial.println("─────────────────────────────────────");
    return;
  }

  // ── cal ──
  if (line == "cal") {
    Serial.println("[CMD] Calibration triggered");
    calibratePending = true;
    return;
  }

  // ── reboot ──
  if (line == "reboot") {
    Serial.println("[CMD] Rebooting...");
    delay(200);
    ESP.restart();
    return;
  }

  // ── fake off ──
  if (line == "fake off") {
    fakeDataEnabled = false;
    Serial.println("[CMD] Fake data disabled — using real IMU");
    return;
  }

  // ── pitch <val> ──
  if (line.startsWith("pitch ")) {
    float val = line.substring(6).toFloat();
    fakePitch       = val;
    fakeDataEnabled = true;
    pitch           = val;
    Serial.printf("[CMD] Fake pitch set to %+.2f deg\n", val);
    return;
  }

  // ── roll <val> ──
  if (line.startsWith("roll ")) {
    float val = line.substring(5).toFloat();
    fakeRoll        = val;
    fakeDataEnabled = true;
    roll            = val;
    Serial.printf("[CMD] Fake roll set to %+.2f deg\n", val);
    return;
  }

  // ── unknown ──
  Serial.printf("Unknown command: '%s'  (type 'help' for list)\n", line.c_str());
}

void loop() {
  handleSerial();
  handleButton();

  if (calibratePending) {
    calibratePending = false;
    runCalibration();
  }

  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;

#if USE_IMU
    if (fakeDataEnabled) {
      // Use injected values — good for testing display/BLE without IMU
      pitch = fakePitch;
      roll  = fakeRoll;
      isCalibrated = true;
    } else if (imuOk && isCalibrated) {
      updateIMU();

      if (bleConnected) {
        pCharPitch->setValue(pitch);
        pCharPitch->notify();
        pCharRoll->setValue(roll);
        pCharRoll->notify();
      }

#if USE_ESPNOW
      CoreData data = { pitch, roll, isCalibrated };
      esp_now_send(broadcastAddr, (uint8_t*)&data, sizeof(data));
#endif

      Serial.printf("[IMU] Pitch: %+6.2f  Roll: %+6.2f\n", pitch, roll);
    }
#endif

#if USE_OLED
    if (oledOk) updateDisplay();
#endif
  }
}
