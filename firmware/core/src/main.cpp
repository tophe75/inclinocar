#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <NimBLEDevice.h>
#include "config.h"

#if USE_OLED
  #include <Adafruit_SSD1306.h>
#endif

#if USE_ESPNOW
  #include <esp_now.h>
  #include <WiFi.h>
#endif

// ─── Globals ─────────────────────────────────────────────────
Adafruit_MPU6050 mpu;

#if USE_OLED
  Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
#endif

NimBLEServer*         pServer         = nullptr;
NimBLECharacteristic* pCharPitch      = nullptr;
NimBLECharacteristic* pCharRoll       = nullptr;
NimBLECharacteristic* pCharSatPitch   = nullptr;
NimBLECharacteristic* pCharSatRoll    = nullptr;

bool    bleConnected  = false;
float   pitch         = 0.0f;
float   roll          = 0.0f;
float   satPitch      = 0.0f;   // From satellite unit via ESP-NOW
float   satRoll       = 0.0f;
bool    satDataValid  = false;
unsigned long lastUpdate = 0;

// Complementary filter state
float filteredPitch = 0.0f;
float filteredRoll  = 0.0f;
unsigned long lastSensorTime = 0;

// ─── BLE Callbacks ───────────────────────────────────────────
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pSrv) override {
    bleConnected = true;
    Serial.println("[BLE] Client connected");
  }
  void onDisconnect(NimBLEServer* pSrv) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected — restarting advertising");
    NimBLEDevice::startAdvertising();
  }
};

// ─── ESP-NOW Receive Callback ─────────────────────────────────
#if USE_ESPNOW
struct SatelliteData {
  float pitch;
  float roll;
};

void onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len) {
  if (len == sizeof(SatelliteData)) {
    SatelliteData sat;
    memcpy(&sat, data, sizeof(sat));
    satPitch    = sat.pitch;
    satRoll     = sat.roll;
    satDataValid = true;
    Serial.printf("[ESP-NOW] Sat pitch=%.2f roll=%.2f\n", satPitch, satRoll);
  }
}
#endif

// ─── Sensor Fusion ───────────────────────────────────────────
void updateIMU() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  unsigned long now = millis();
  float dt = (now - lastSensorTime) / 1000.0f;
  lastSensorTime = now;

  // Accelerometer-based angles
  float accelPitch = atan2(accel.acceleration.y, accel.acceleration.z) * RAD_TO_DEG;
  float accelRoll  = atan2(-accel.acceleration.x, accel.acceleration.z) * RAD_TO_DEG;

  // Complementary filter: blend gyro integration with accelerometer
  filteredPitch = COMP_FILTER_ALPHA * (filteredPitch + gyro.gyro.x * dt * RAD_TO_DEG)
                + (1.0f - COMP_FILTER_ALPHA) * accelPitch;
  filteredRoll  = COMP_FILTER_ALPHA * (filteredRoll  + gyro.gyro.y * dt * RAD_TO_DEG)
                + (1.0f - COMP_FILTER_ALPHA) * accelRoll;

  pitch = filteredPitch;
  roll  = filteredRoll;
}

// ─── OLED Display ────────────────────────────────────────────
#if USE_OLED
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setCursor(0, 0);
  display.println("  InclinoCar");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  // Tent angles
  display.setCursor(0, 13);
  display.printf("Pitch: %+6.1f deg", pitch);
  display.setCursor(0, 23);
  display.printf("Roll:  %+6.1f deg", roll);

  // Level indicator
  bool pitchOk = abs(pitch) <= LEVEL_THRESHOLD_DEG;
  bool rollOk  = abs(roll)  <= LEVEL_THRESHOLD_DEG;
  display.setCursor(0, 35);
  if (pitchOk && rollOk) {
    display.println("  >> LEVEL OK <<");
  } else {
    display.println("  Adjust needed");
  }

  // Satellite data (if available)
  if (satDataValid) {
    display.drawLine(0, 45, 127, 45, SSD1306_WHITE);
    display.setCursor(0, 48);
    display.printf("Car P:%+5.1f R:%+5.1f", satPitch, satRoll);
  }

  // BLE status
  display.setCursor(100, 56);
  display.print(bleConnected ? "BLE+" : "BLE-");

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
  pCharSatPitch = pService->createCharacteristic(
    BLE_CHAR_SAT_PITCH_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pCharSatRoll = pService->createCharacteristic(
    BLE_CHAR_SAT_ROLL_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising as InclinoCar");
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== InclinoCar Core Unit ===");

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
    Serial.println("[WARN] SSD1306 not found — display disabled");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 24);
    display.println("InclinoCar v1.0");
    display.display();
    Serial.println("[OK] OLED initialized");
    delay(1500);
  }
#endif

#if USE_ESPNOW
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed");
  } else {
    esp_now_register_recv_cb(onEspNowReceive);
    Serial.println("[OK] ESP-NOW initialized");
  }
#endif

  setupBLE();
  lastSensorTime = millis();
  Serial.println("[OK] Setup complete — running");
}

// ─── Loop ────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;

    updateIMU();

    // Publish over BLE
    if (bleConnected) {
      pCharPitch->setValue(pitch);
      pCharPitch->notify();
      pCharRoll->setValue(roll);
      pCharRoll->notify();

      if (satDataValid) {
        pCharSatPitch->setValue(satPitch);
        pCharSatPitch->notify();
        pCharSatRoll->setValue(satRoll);
        pCharSatRoll->notify();
      }
    }

    Serial.printf("[IMU] Pitch: %+6.2f  Roll: %+6.2f\n", pitch, roll);

#if USE_OLED
    updateDisplay();
#endif
  }
}
