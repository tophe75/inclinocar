#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <NimBLEDevice.h>

// I2C: SDA=GPIO6, SCL=GPIO7
// Button: GPIO5 → GND (hold 1s = calibrate)
#define SDA_PIN       6
#define SCL_PIN       7
#define BTN_PIN       5
#define CAL_HOLD_MS   1000

// Nordic UART Service UUIDs
#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // phone → device
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // device → phone

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_MPU6050 mpu;
Preferences prefs;

float pitchOffset    = 0.0;
float rollOffset     = 0.0;
float smoothedPitch  = 0.0;
float smoothedRoll   = 0.0;
const float ALPHA    = 0.15f;  // lower = smoother, higher = more responsive

bool          btnWasPressed    = false;
unsigned long btnPressTime      = 0;
bool          calibratePending  = false;

// BLE
NimBLEServer*         pServer  = nullptr;
NimBLECharacteristic* pTxChar  = nullptr;
bool bleConnected = false;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s) override {
    bleConnected = true;
    Serial.println("BLE connected");
  }
  void onDisconnect(NimBLEServer* s) override {
    bleConnected = false;
    Serial.println("BLE disconnected");
    NimBLEDevice::startAdvertising();
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string val = c->getValue();
    String cmd = String(val.c_str());
    cmd.trim();
    Serial.printf("BLE RX: %s\n", cmd.c_str());
    if (cmd == "CAL") {
      calibratePending = true;
      Serial.println("BLE: calibration requested");
    }
  }
};

void setupBLE() {
  NimBLEDevice::init("InclinoCar");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(NUS_SERVICE_UUID);

  // TX characteristic — device sends data to phone
  pTxChar = pService->createCharacteristic(
    NUS_TX_UUID,
    NIMBLE_PROPERTY::NOTIFY
  );

  // RX characteristic — phone sends commands to device
  auto* pRxChar = pService->createCharacteristic(
    NUS_RX_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pRxChar->setCallbacks(new RxCallbacks());

  pService->start();

  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(NUS_SERVICE_UUID);
  pAdv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.println("BLE advertising as 'InclinoCar'");
}

void saveOffsets() {
  prefs.begin("inclinocar", false);
  prefs.putFloat("pitchOff", pitchOffset);
  prefs.putFloat("rollOff",  rollOffset);
  prefs.end();
}

void loadOffsets() {
  prefs.begin("inclinocar", true);
  pitchOffset = prefs.getFloat("pitchOff", 0.0);
  rollOffset  = prefs.getFloat("rollOff",  0.0);
  prefs.end();
  Serial.printf("Offsets loaded: pitch=%.2f roll=%.2f\n", pitchOffset, rollOffset);
}

void calibrate() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println("  Calibrating...");
  display.setCursor(0, 16); display.println("  Keep still!");
  display.display();
  delay(500);

  float pSum = 0, rSum = 0;
  for (int i = 0; i < 50; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    float rp = atan2(-a.acceleration.x,
                  sqrt(a.acceleration.y*a.acceleration.y +
                       a.acceleration.z*a.acceleration.z)) * 180.0/PI;
    float rr = atan2(a.acceleration.y, a.acceleration.z) * 180.0/PI;
    // Apply same CW 90 rotation
    pSum +=  rr;
    rSum +=  rp;
    delay(20);
  }
  pitchOffset = pSum / 50.0;
  rollOffset  = rSum / 50.0;
  saveOffsets();

  display.clearDisplay();
  display.setCursor(0, 0);  display.println("  Cal saved!");
  display.setCursor(0, 16); display.printf("  P: %.1f\n", pitchOffset);
  display.setCursor(0, 28); display.printf("  R: %.1f\n", rollOffset);
  display.display();
  delay(1500);
}

void handleButton() {
  bool pressed = (digitalRead(BTN_PIN) == LOW);
  if (pressed && !btnWasPressed) {
    btnPressTime  = millis();
    btnWasPressed = true;
  }
  if (!pressed && btnWasPressed) {
    btnWasPressed = false;
  }
  if (pressed && btnWasPressed && millis() - btnPressTime >= CAL_HOLD_MS) {
    btnWasPressed = false;
    calibrate();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed"); while(1);
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println("  InclinoCar");
  display.setCursor(0, 12); display.println("  " FW_VERSION);
  display.setCursor(0, 28); display.println("  Starting...");
  display.display();

  if (!mpu.begin()) {
    display.setCursor(0, 44); display.println("  IMU not found!");
    display.display();
    Serial.println("MPU-6050 failed"); while(1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  loadOffsets();
  setupBLE();

  display.clearDisplay();
  display.setCursor(0, 0);  display.println("  InclinoCar");
  display.setCursor(0, 12); display.println("  " FW_VERSION);
  display.setCursor(0, 28); display.println(
    (pitchOffset != 0.0 || rollOffset != 0.0) ? "  Cal loaded" : "  Hold btn: cal");
  display.setCursor(0, 44); display.println("  BLE ready");
  display.display();
  delay(1500);
}

void loop() {
  handleButton();

  if (calibratePending) {
    calibratePending = false;
    calibrate();
  }

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Raw axis values (no offset yet)
  float rawPitch = atan2(-a.acceleration.x,
                   sqrt(a.acceleration.y*a.acceleration.y +
                        a.acceleration.z*a.acceleration.z)) * 180.0/PI;
  float rawRoll  = atan2(a.acceleration.y, a.acceleration.z) * 180.0/PI;

  // Rotate axes CW 90 degrees then apply calibration offsets
  float rotPitch =  rawRoll  - pitchOffset;
  float rotRoll  =  rawPitch - rollOffset;

  // Exponential moving average to reduce jitter
  smoothedPitch = ALPHA * rotPitch + (1.0f - ALPHA) * smoothedPitch;
  smoothedRoll  = ALPHA * rotRoll  + (1.0f - ALPHA) * smoothedRoll;

  float pitch = smoothedPitch;
  float roll  = smoothedRoll;

  // Send JSON over BLE NUS
  if (bleConnected) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"p\":%.1f,\"r\":%.1f}\n", pitch, roll);
    pTxChar->setValue((uint8_t*)buf, strlen(buf));
    pTxChar->notify();
  }

  Serial.printf("P:%+6.1f  R:%+6.1f  BLE:%s\n", pitch, roll, bleConnected ? "connected" : "advertising");

  // Update display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // Left: "InclinoCar vX.X.X"  Right: "BT+" — drawn separately so they never overlap
  display.setCursor(0, 0);
  display.print("InclinoCar " FW_VERSION);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.printf("P%6.1f", pitch);
  display.setTextSize(1); display.print((char)247);

  display.setTextSize(2);
  display.setCursor(0, 38);
  display.printf("R%6.1f", roll);
  display.setTextSize(1); display.print((char)247);

  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  display.setCursor(0, 57);
  display.setTextSize(1);
  bool level = abs(pitch) < 1.0 && abs(roll) < 1.0;
  display.print(level ? "  ** LEVEL **" : "  Adjust...");

  // BT indicator — only show when connected, pinned to right edge
  if (bleConnected) {
    display.setCursor(116, 0);
    display.print("BT");
  }

  display.display();
  delay(100);
}
