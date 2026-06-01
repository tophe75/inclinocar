#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "protocol.h"

// I2C: SDA=GPIO6, SCL=GPIO7
// Button: GPIO5 → GND
#define SDA_PIN      6
#define SCL_PIN      7
#define BTN_PIN      5
#define PAIR_DBL_MS  400

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Preferences prefs;

uint8_t coreMAC[6] = {0,0,0,0,0,0};
bool    hasCore     = false;
bool    pairingMode = false;
unsigned long pairStart   = 0;
unsigned long lastDataMs  = 0;

float rxPitch = 0.0;
float rxRoll  = 0.0;
bool  newData = false;

bool          btnWasPressed  = false;
unsigned long btnPressTime   = 0;
unsigned long btnLastRelease = 0xFFFFFFFF;  // prevents false double-press on boot
int           btnPressCount  = 0;

void saveCoreMAC() {
  prefs.begin("inclinocar", false);
  prefs.putBytes("coreMAC", coreMAC, 6);
  prefs.putBool("hasCore", true);
  prefs.end();
}

void loadCoreMAC() {
  prefs.begin("inclinocar", true);
  hasCore = prefs.getBool("hasCore", false);
  if (hasCore) prefs.getBytes("coreMAC", coreMAC, 6);
  prefs.end();
  if (hasCore)
    Serial.printf("Core MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      coreMAC[0],coreMAC[1],coreMAC[2],
      coreMAC[3],coreMAC[4],coreMAC[5]);
}

void showStatus(const char* l1, const char* l2=nullptr, const char* l3=nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,  0); display.println(l1);
  if (l2) { display.setCursor(0, 16); display.println(l2); }
  if (l3) { display.setCursor(0, 32); display.println(l3); }
  display.display();
}

void sendPairRequest() {
  PairPacket req;
  req.type = MSG_PAIR_REQ;
  WiFi.macAddress(req.mac);
  uint8_t broadcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_send(broadcast, (uint8_t*)&req, sizeof(req));
}

void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  if (len < 1) return;
  uint8_t type = data[0];

  if (type == MSG_PAIR_ACK && pairingMode) {
    // Core responded to our pair request
    Serial.printf("Paired with core: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    memcpy(coreMAC, mac, 6);
    hasCore     = true;
    pairingMode = false;
    saveCoreMAC();
    // Register core as peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, coreMAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    if (!esp_now_is_peer_exist(coreMAC)) esp_now_add_peer(&peer);
    showStatus("  Paired!", "  Core unit", "  connected");
    delay(2000);
  }

  if (type == MSG_DATA && hasCore && memcmp(mac, coreMAC, 6) == 0) {
    DataPacket* pkt = (DataPacket*)data;
    rxPitch   = pkt->pitch;
    rxRoll    = pkt->roll;
    newData   = true;
    lastDataMs = millis();
  }
}

void setupESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW failed"); return; }
  esp_now_register_recv_cb(onDataReceived);
  // Add broadcast peer for sending pair requests
  uint8_t broadcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_peer_info_t bp = {};
  memcpy(bp.peer_addr, broadcast, 6);
  bp.channel = 0;
  esp_now_add_peer(&bp);
  // Re-add saved core if we have one
  if (hasCore) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, coreMAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    esp_now_add_peer(&peer);
  }
  Serial.print("Satellite MAC: "); Serial.println(WiFi.macAddress());
}

void enterPairingMode() {
  pairingMode = true;
  pairStart   = millis();
  Serial.println("Satellite: pairing mode");
}

void handleButton() {
  bool pressed = (digitalRead(BTN_PIN) == LOW);
  if (pressed && !btnWasPressed) {
    btnPressTime  = millis();
    btnWasPressed = true;
  }
  if (!pressed && btnWasPressed) {
    btnWasPressed = false;
    unsigned long held = millis() - btnPressTime;
    if (held < 1000) {
      if (millis() - btnLastRelease < PAIR_DBL_MS) btnPressCount++;
      else btnPressCount = 1;
      btnLastRelease = millis();
      if (btnPressCount >= 2) {
        btnPressCount = 0;
        enterPairingMode();
      }
    }
  }
  if (pairingMode && pairStart > 0 && millis() - pairStart > PAIR_MODE_MS) {
    pairingMode = false;
    Serial.println("Pair timeout");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { Serial.println("OLED failed"); while(1); }
  showStatus("  InclinoCar", "  Remote Display", "  " FW_VERSION);
  loadCoreMAC();
  setupESPNow();

  if (!hasCore) {
    // No saved core — auto enter pairing mode on first boot
    enterPairingMode();
    showStatus("  Looking for", "  core unit...", "  or dbl-press btn");
  } else {
    showStatus("  Remote Display", "  " FW_VERSION, "  Waiting for core");
  }
  delay(1500);
}

void loop() {
  handleButton();

  // In pairing mode — broadcast pair request every 500ms
  if (pairingMode) {
    static unsigned long lastReq = 0;
    if (millis() - lastReq > PAIR_REQ_MS) {
      lastReq = millis();
      sendPairRequest();
    }
    int rem = (PAIR_MODE_MS - (millis()-pairStart)) / 1000;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0, 0);  display.println("  Looking for");
    display.setCursor(0, 12); display.println("  core unit...");
    display.setCursor(0, 28); display.println("  Dbl-press core");
    display.setCursor(0, 40); display.println("  button to pair");
    display.setCursor(0, 54); display.printf("  Timeout: %ds", rem);
    display.display();
    delay(100);
    return;
  }

  // No signal timeout
  bool hasSignal = hasCore && (millis() - lastDataMs < SIGNAL_TIMEOUT);

  if (!hasSignal) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0, 0);  display.println("  Remote Display");
    display.setCursor(0, 12); display.println("  " FW_VERSION);
    display.setCursor(0, 28); display.println("  No signal from");
    display.setCursor(0, 40); display.println("  core unit");
    display.setCursor(0, 54); display.println(!hasCore ? "  Dbl-press to pair" : "  Check core power");
    display.display();
    delay(200);
    return;
  }

  if (!newData) return;
  newData = false;

  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.println("  Remote  " FW_VERSION);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 16); display.printf("P%+6.1f", rxPitch);
  display.setTextSize(1);   display.print((char)247);
  display.setTextSize(2);
  display.setCursor(0, 38); display.printf("R%+6.1f", rxRoll);
  display.setTextSize(1);   display.print((char)247);
  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  display.setCursor(0, 57); display.setTextSize(1);
  display.print(abs(rxPitch)<1.0&&abs(rxRoll)<1.0 ? "  ** LEVEL **" : "  Adjust...");
  display.display();
}
