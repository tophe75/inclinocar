#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <math.h>

#define CAL_HOLD_MS 1000

#define NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define C_BG    TFT_BLACK
#define C_GREEN 0x07E0
#define C_AMBER 0xFD20
#define C_WHITE TFT_WHITE
#define C_DIM   0x4208
#define C_RED   TFT_RED

char     deviceNickname[32] = "InclinoCore";
char     bleAdvName[32]     = "InclinoCore";
uint16_t devicePIN          = 0;

void generateBleAdvName() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(bleAdvName, sizeof(bleAdvName), "Core#%02X%02X%02X%02X%02X%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

uint16_t generatePIN() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  return (mac[4] * 256 + mac[5]) % 9000 + 1000;
}

Preferences prefs;

float pitchOffset   = 0.0;
float rollOffset    = 0.0;
float smoothedPitch = 0.0;
float smoothedRoll  = 0.0;
const float ALPHA   = 0.15f;

const uint8_t BRIGHTNESS_LEVELS[] = {64, 128, 192, 255};
const int     BRIGHTNESS_COUNT    = 4;
int           brightnessIndex     = 0;

bool          btnWasPressed    = false;
unsigned long btnPressTime     = 0;
bool          calibratePending = false;
bool          pinVerified      = false;

NimBLEServer*         pServer = nullptr;
NimBLECharacteristic* pTxChar = nullptr;
bool bleConnected = false;

// ── BLE ─────────────────────────────────────────────────────
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s) override {
    bleConnected = true;
    Serial.println("BLE connected");
  }
  void onDisconnect(NimBLEServer* s) override {
    bleConnected = false;
    pinVerified  = false;
    Serial.println("BLE disconnected");
    NimBLEDevice::startAdvertising();
  }
};

void saveNickname();

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string val = c->getValue();
    String cmd = String(val.c_str());
    cmd.trim();
    Serial.printf("BLE RX: %s\n", cmd.c_str());
    if (cmd.startsWith("KNOWNMAC:")) {
      pinVerified = true;
      if (pTxChar) {
        String ack = "{\"pin\":\"ok\",\"n\":\"" + String(deviceNickname) + "\"}\n";
        pTxChar->setValue((uint8_t*)ack.c_str(), ack.length());
        pTxChar->notify();
      }
    } else if (cmd.startsWith("PIN:")) {
      uint16_t entered = cmd.substring(4).toInt();
      if (entered == devicePIN) {
        pinVerified = true;
        if (pTxChar) {
          String ack = String("{\"pin\":\"ok\",\"n\":\"") + deviceNickname + "\"}\n";
          pTxChar->setValue((uint8_t*)ack.c_str(), ack.length());
          pTxChar->notify();
        }
      } else {
        if (pTxChar) {
          String nak = "{\"pin\":\"fail\"}\n";
          pTxChar->setValue((uint8_t*)nak.c_str(), nak.length());
          pTxChar->notify();
        }
      }
    } else if (cmd == "CAL") {
      if (pinVerified) calibratePending = true;
    } else if (cmd.startsWith("NICK:")) {
      String nick = cmd.substring(5);
      nick.trim();
      if (nick.length() > 0 && nick.length() < 32) {
        nick.toCharArray(deviceNickname, sizeof(deviceNickname));
        saveNickname();
      }
    }
  }
};

void setupBLE() {
  NimBLEDevice::init(bleAdvName);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  NimBLEService* pService = pServer->createService(NUS_SERVICE_UUID);
  pTxChar = pService->createCharacteristic(NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
  auto* pRxChar = pService->createCharacteristic(
    NUS_RX_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pRxChar->setCallbacks(new RxCallbacks());
  pService->start();
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(NUS_SERVICE_UUID);
  pAdv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
  Serial.printf("BLE advertising as '%s'\n", bleAdvName);
}

// ── NVS ─────────────────────────────────────────────────────
void saveNickname() {
  prefs.begin("inclinocar", false);
  prefs.putString("nickname", deviceNickname);
  prefs.end();
}

void loadNickname() {
  prefs.begin("inclinocar", true);
  String s = prefs.getString("nickname", "InclinoCore");
  prefs.end();
  s.toCharArray(deviceNickname, sizeof(deviceNickname));
}

void saveOffsets() {
  prefs.begin("inclinocar", false);
  prefs.putFloat("pitchOff", pitchOffset);
  prefs.putFloat("rollOff",  rollOffset);
  prefs.end();
}

void loadOffsets() {
  prefs.begin("inclinocar", true);
  pitchOffset     = prefs.getFloat("pitchOff", 0.0);
  rollOffset      = prefs.getFloat("rollOff",  0.0);
  brightnessIndex = prefs.getInt("brightness", 0);
  prefs.end();
}

void applyBrightness() {
  M5.Display.setBrightness(BRIGHTNESS_LEVELS[brightnessIndex]);
}

void saveBrightness() {
  prefs.begin("inclinocar", false);
  prefs.putInt("brightness", brightnessIndex);
  prefs.end();
}

// ── Display ──────────────────────────────────────────────────
void drawMainScreen(float pitch, float roll) {
  bool level = abs(pitch) < 1.0 && abs(roll) < 1.0;

  M5.Display.fillScreen(C_BG);

  // Header
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_GREEN, C_BG);
  M5.Display.setCursor(4, 4);
  M5.Display.print(deviceNickname);

  if (bleConnected && pinVerified) {
    M5.Display.setCursor(106, 4);
    M5.Display.print("BT");
  } else if (!bleConnected) {
    M5.Display.setTextColor(C_DIM, C_BG);
    M5.Display.setCursor(74, 4);
    char pinStr[10];
    snprintf(pinStr, sizeof(pinStr), "%04d", devicePIN);
    M5.Display.print(pinStr);
  }

  M5.Display.drawLine(0, 18, 135, 18, C_DIM);

  // Pitch
  uint16_t pc = abs(pitch) < 1.0 ? C_GREEN : (abs(pitch) < 3.0 ? C_AMBER : C_RED);
  M5.Display.setTextColor(C_DIM, C_BG);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(4, 26);
  M5.Display.print("PITCH");
  M5.Display.setTextColor(pc, C_BG);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(4, 40);
  char buf[12];
  snprintf(buf, sizeof(buf), "%+.1f", pitch);
  M5.Display.print(buf);

  M5.Display.drawLine(0, 100, 135, 100, C_DIM);

  // Roll
  uint16_t rc = abs(roll) < 1.0 ? C_GREEN : (abs(roll) < 3.0 ? C_AMBER : C_RED);
  M5.Display.setTextColor(C_DIM, C_BG);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(4, 108);
  M5.Display.print("ROLL");
  M5.Display.setTextColor(rc, C_BG);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(4, 122);
  snprintf(buf, sizeof(buf), "%+.1f", roll);
  M5.Display.print(buf);

  M5.Display.drawLine(0, 182, 135, 182, C_DIM);

  // Status
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(level ? C_GREEN : C_AMBER, C_BG);
  M5.Display.setCursor(4, 192);
  M5.Display.print(level ? "** LEVEL **" : "Adjust...");

  // Version
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_DIM, C_BG);
  M5.Display.setCursor(4, 226);
  M5.Display.print(FW_VERSION);
}

// ── Calibration ──────────────────────────────────────────────
void calibrate() {
  M5.Display.fillScreen(C_BG);
  M5.Display.setTextColor(C_AMBER, C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 80);
  M5.Display.print("Calibrating");
  M5.Display.setCursor(4, 106);
  M5.Display.print("Keep still!");
  delay(500);

  float pSum = 0, rSum = 0;
  for (int i = 0; i < 50; i++) {
    float ax, ay, az;
    M5.Imu.getAccelData(&ax, &ay, &az);
    float rp = atan2(-ax, sqrt(ay*ay + az*az)) * 180.0/PI;
    float rr = atan2(ay, az) * 180.0/PI;
    pSum += rr;
    rSum += rp;
    delay(20);
  }
  pitchOffset = pSum / 50.0;
  rollOffset  = rSum / 50.0;
  saveOffsets();

  M5.Display.fillScreen(C_BG);
  M5.Display.setTextColor(C_GREEN, C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 80);
  M5.Display.print("Cal saved!");
  char buf[20];
  snprintf(buf, sizeof(buf), "P: %.1f", pitchOffset);
  M5.Display.setCursor(4, 110); M5.Display.print(buf);
  snprintf(buf, sizeof(buf), "R: %.1f", rollOffset);
  M5.Display.setCursor(4, 134); M5.Display.print(buf);
  delay(3000);
}

void cycleBrightness() {
  brightnessIndex = (brightnessIndex + 1) % BRIGHTNESS_COUNT;
  applyBrightness();
  saveBrightness();
  M5.Display.fillScreen(C_BG);
  M5.Display.setTextColor(C_GREEN, C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 90);
  M5.Display.print("Brightness");
  int pct = (brightnessIndex + 1) * 25;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  M5.Display.setTextSize(4);
  M5.Display.setCursor(30, 120);
  M5.Display.print(buf);
  delay(800);
}

void handleButton() {
  M5.update();
  bool pressed = M5.BtnA.isPressed();
  if (pressed && !btnWasPressed) {
    btnPressTime  = millis();
    btnWasPressed = true;
  }
  if (!pressed && btnWasPressed) {
    btnWasPressed = false;
    if (millis() - btnPressTime < CAL_HOLD_MS) cycleBrightness();
  }
  if (pressed && btnWasPressed && millis() - btnPressTime >= CAL_HOLD_MS) {
    btnWasPressed = false;
    calibrate();
  }
}

// ── Setup ────────────────────────────────────────────────────
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  Serial.begin(115200);
  Serial.println("InclinoCore M5StickC Plus starting...");

  M5.Display.setRotation(0);  // Portrait 135x240
  M5.Display.fillScreen(C_BG);
  M5.Display.setTextColor(C_GREEN, C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 80);
  M5.Display.print("InclinoCore");
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_DIM, C_BG);
  M5.Display.setCursor(4, 108);
  M5.Display.print(FW_VERSION);
  M5.Display.setCursor(4, 124);
  M5.Display.print("Starting...");

  M5.Imu.init();

  loadOffsets();
  loadNickname();
  generateBleAdvName();
  devicePIN = generatePIN();
  applyBrightness();
  setupBLE();

  // Boot screen
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  M5.Display.fillScreen(C_BG);
  M5.Display.setTextColor(C_GREEN, C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 20);
  M5.Display.print(deviceNickname);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_DIM, C_BG);
  M5.Display.setCursor(4, 54);
  M5.Display.print(FW_VERSION);
  M5.Display.setCursor(4, 70);
  M5.Display.print(macStr);
  M5.Display.setTextColor(C_WHITE, C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 100);
  char pinStr[14];
  snprintf(pinStr, sizeof(pinStr), "PIN: %04d", devicePIN);
  M5.Display.print(pinStr);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_DIM, C_BG);
  M5.Display.setCursor(4, 140);
  M5.Display.print((pitchOffset != 0.0 || rollOffset != 0.0) ?
    "Cal loaded" : "Hold BtnA: calibrate");

  delay(5000);
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
  handleButton();
  if (calibratePending) { calibratePending = false; calibrate(); }

  float ax, ay, az;
  M5.Imu.getAccelData(&ax, &ay, &az);

  float rawPitch = atan2(-ax, sqrt(ay*ay + az*az)) * 180.0/PI;
  float rawRoll  = atan2(ay, az) * 180.0/PI;
  float rotPitch = rawRoll  - pitchOffset;
  float rotRoll  = rawPitch - rollOffset;
  smoothedPitch  = ALPHA * rotPitch + (1.0f - ALPHA) * smoothedPitch;
  smoothedRoll   = ALPHA * rotRoll  + (1.0f - ALPHA) * smoothedRoll;

  float pitch = smoothedPitch;
  float roll  = smoothedRoll;

  if (bleConnected && pinVerified) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"p\":%.1f,\"r\":%.1f,\"n\":\"%s\"}\n",
      pitch, roll, deviceNickname);
    pTxChar->setValue((uint8_t*)buf, strlen(buf));
    pTxChar->notify();
  }

  Serial.printf("P:%+.1f R:%+.1f BLE:%s\n",
    pitch, roll, bleConnected ? "connected" : "advertising");

  drawMainScreen(pitch, roll);
  delay(100);
}
