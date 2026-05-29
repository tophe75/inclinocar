#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <NimBLEDevice.h>
#include "config.h"

#if USE_OLED
  #include <Adafruit_SSD1306.h>
#endif

// ─── Globals ─────────────────────────────────────────────────
Adafruit_MPU6050 mpu;

#if USE_OLED
  Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
#endif

NimBLEServer*         pServer       = nullptr;
NimBLECharacteristic* pCharPitch    = nullptr;
NimBLECharacteristic* pCharRoll     = nullptr;
NimBLECharacteristic* pCharCalib    = nullptr;
NimBLECharacteristic* pCharStatus   = nullptr;

bool  bleConnected    = false;
float pitch           = 0.0f;
float roll            = 0.0f;
float filteredPitch   = 0.0f;
float filteredRoll    = 0.0f;

// ─── Calibration ─────────────────────────────────────────────
float   calOffsetPitch  = 0.0f;
float   calOffsetRoll   = 0.0f;
bool    isCalibrated    = false;
bool    calibratePending = false;

// Button debounce
unsigned long btnPressTime  = 0;
bool          btnWasPressed = false;

unsigned long lastUpdate     = 0;
unsigned long lastSensorTime = 0;

// ─── ESP-NOW packet ──────────────────────────────────────────
struct CoreData {
  float pitch;
  float roll;
  bool  isCalibrated;
};

uint8_t broadcastAddr[] = ESPNOW_BROADCAST_ADDR;

// ─── BLE Callbacks ───────────────────────────────────────────
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

// Write "1" to the calibrate characteristic to trigger reset
class CalibWriteCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar) override {
    std::string val = pChar->getValue();
    if (val.length() > 0 && val[0] == 0x01) {
      calibratePending = true;
      Serial.println("[BLE] Calibration reset requested via app");
    }
  }
};

// ─── Calibration routine ─────────────────────────────────────
void runCalibration() {
  Serial.println("[CAL] Starting calibration — keep unit still");
  isCalibrated = false;

#if USE_OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 20);
  display.println("Calibrating...");
  display.setCursor(4, 34);
  display.println("Keep unit still");
  display.display();
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

  Serial.printf("[CAL] Done. Offsets: pitch=%.2f roll=%.2f\n",
                calOffsetPitch, calOffsetRoll);

  // Notify app
  if (bleConnected) {
    uint8_t calDone = 0x01;
    pCharStatus->setValue(&calDone, 1);
    pCharStatus->notify();
  }
}

// ─── Sensor update ───────────────────────────────────────────
void updateIMU() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  unsigned long now = millis();
  float dt = (now - lastSensorTime) / 1000.0f;
  lastSensorTime = now;

  float accelPitch = atan2(accel.acceleration.y, accel.acceleration.z) * RAD_TO_DEG - calOffsetPitch;
  float accelRoll  = atan2(-accel.acceleration.x, accel.acceleration.z) * RAD_TO_DEG - calOffsetRoll;

  filteredPitch = COMP_FILTER_ALPHA * (filteredPitch + gyro.gyro.x * dt * RAD_TO_DEG)
                + (1.0f - COMP_FILTER_ALPHA) * accelPitch;
  filteredRoll  = COMP_FILTER_ALPHA * (filteredRoll  + gyro.gyro.y * dt * RAD_TO_DEG)
                + (1.0f - COMP_FILTER_ALPHA) * accelRoll;

  pitch = filteredPitch;
  roll  = filteredRoll;
}

// ─── Button handler ──────────────────────────────────────────
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
      btnWasPressed    = false;   // Prevent re-trigger
      Serial.println("[BTN] Calibration hold detected");
    }
  }
}

// ─── OLED update ─────────────────────────────────────────────
#if USE_OLED
void drawAngleBar(int x, int y, int w, float angle, float maxAngle) {
  display.drawRect(x, y, w, 7, SSD1306_WHITE);
  display.drawFastVLine(x + w / 2, y, 7, SSD1306_WHITE);
  int center = x + w / 2;
  int fill   = (int)((angle / maxAngle) * (w / 2));
  fill = constrain(fill, -(w / 2 - 2), (w / 2 - 2));
  if (fill > 0)      display.fillRect(center, y + 1, fill, 5, SSD1306_WHITE);
  else if (fill < 0) display.fillRect(center + fill, y + 1, -fill, 5, SSD1306_WHITE);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Title
  display.setCursor(24, 0);
  display.print("InclinoCar");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Pitch
  display.setCursor(0, 13);
  display.print("PITCH");
  char buf[12];
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

  // BLE indicator
  display.setCursor(110, 0);
  display.print(bleConnected ? "B+" : "B-");

  display.display();
}
#endif

// ─── BLE Setup ───────────────────────────────────────────────
void setupBLE() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  pCharPitch = pService->createCharacteristic(
    BLE_CHAR_PITCH_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pCharRoll = pService->createCharacteristic(
    BLE_CHAR_ROLL_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pCharCalib = pService->createCharacteristic(
    BLE_CHAR_CALIBRATE_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  pCharCalib->setCallbacks(new CalibWriteCallback());

  pCharStatus = pService->createCharacteristic(
    BLE_CHAR_STATUS_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  pService->start();
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("[BLE] Advertising as InclinoCar");
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== InclinoCar Core Unit v0.1.0 ===");

  // Calibration button
  pinMode(CAL_BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);

  // MPU-6050
  if (!mpu.begin(MPU_I2C_ADDR)) {
    Serial.println("[ERROR] MPU-6050 not found! Check wiring.");
    while (1) delay(100);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("[OK] MPU-6050 initialized");

#if USE_OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println("[WARN] SSD1306 not found");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 20);
    display.println("InclinoCar v0.1");
    display.setCursor(28, 32);
    display.println("Core Unit");
    display.display();
    Serial.println("[OK] OLED initialized");
    delay(1200);
  }
#endif

#if USE_ESPNOW
  WiFi.mode(WIFI_STA);
  Serial.print("[INFO] Core MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed");
  } else {
    // Register broadcast peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastAddr, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    Serial.println("[OK] ESP-NOW broadcast initialized");
  }
#endif

  setupBLE();

  lastSensorTime = millis();

  // Initial calibration on boot
  runCalibration();

  Serial.println("[OK] Setup complete");
}

// ─── Loop ────────────────────────────────────────────────────
void loop() {
  handleButton();

  if (calibratePending) {
    calibratePending = false;
    runCalibration();
  }

  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;

    if (isCalibrated) {
      updateIMU();

      // BLE notify
      if (bleConnected) {
        pCharPitch->setValue(pitch);
        pCharPitch->notify();
        pCharRoll->setValue(roll);
        pCharRoll->notify();
      }

#if USE_ESPNOW
      // Broadcast to satellite display
      CoreData data = { pitch, roll, isCalibrated };
      esp_now_send(broadcastAddr, (uint8_t*)&data, sizeof(data));
#endif

      Serial.printf("[IMU] Pitch: %+6.2f  Roll: %+6.2f\n", pitch, roll);
    }

#if USE_OLED
    updateDisplay();
#endif
  }
}
