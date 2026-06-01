#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include "protocol.h"

// I2C: SDA=GPIO6, SCL=GPIO7
// Button: GPIO5 → GND
#define SDA_PIN       6
#define SCL_PIN       7
#define BTN_PIN       5
#define CAL_HOLD_MS   1000
#define PAIR_DBL_MS   400

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_MPU6050 mpu;
Preferences prefs;

float pitchOffset = 0.0;
float rollOffset  = 0.0;

bool          pairingMode  = false;
unsigned long pairStart    = 0;
uint8_t       satelliteMAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
bool          hasSatellite = false;

bool          btnWasPressed  = false;
unsigned long btnPressTime   = 0;
unsigned long btnLastRelease = 0;
int           btnPressCount  = 0;

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
  Serial.printf("Offsets: pitch=%.2f roll=%.2f\n", pitchOffset, rollOffset);
}

void saveSatelliteMAC() {
  prefs.begin("inclinocar", false);
  prefs.putBytes("satMAC", satelliteMAC, 6);
  prefs.putBool("hasSat", true);
  prefs.end();
}

void loadSatelliteMAC() {
  prefs.begin("inclinocar", true);
  hasSatellite = prefs.getBool("hasSat", false);
  if (hasSatellite) prefs.getBytes("satMAC", satelliteMAC, 6);
  prefs.end();
  if (hasSatellite)
    Serial.printf("Sat MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
      satelliteMAC[0],satelliteMAC[1],satelliteMAC[2],
      satelliteMAC[3],satelliteMAC[4],satelliteMAC[5]);
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

void onDataReceived(const uint8_t* mac, const uint8_t* data, int len) {
  Serial.printf("RX: len=%d type=%d pairingMode=%d\n", len, len>0?data[0]:0, pairingMode);
  if (len < 1) return;
  if (data[0] == MSG_PAIR_REQ && pairingMode) {
    Serial.printf("Pair req from: %02X:%02X:%02X:%02X:%02X:%02X\n",
      mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    memcpy(satelliteMAC, mac, 6);
    hasSatellite = true;
    saveSatelliteMAC();
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, satelliteMAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    if (!esp_now_is_peer_exist(satelliteMAC)) esp_now_add_peer(&peer);
    PairPacket ack;
    ack.type = MSG_PAIR_ACK;
    WiFi.macAddress(ack.mac);
    esp_now_send(satelliteMAC, (uint8_t*)&ack, sizeof(ack));
    pairingMode = false;
    showStatus("  Paired!", "  Remote display", "  connected");
    delay(2000);
  }
}

void setupESPNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_err_t ch_err = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  uint8_t ch; wifi_second_chan_t sc; esp_wifi_get_channel(&ch, &sc);
  Serial.printf("Core WiFi channel: %d (set_err=%d)\n", ch, ch_err);
  esp_err_t init_err = esp_now_init();
  Serial.printf("ESP-NOW init: %d\n", init_err);
  if (init_err != ESP_OK) { Serial.println("ESP-NOW failed"); return; }
  esp_err_t cb_err = esp_now_register_recv_cb(onDataReceived);
  Serial.printf("RX CB registered: %d\n", cb_err);
  uint8_t broadcast[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  esp_now_peer_info_t bp = {};
  memcpy(bp.peer_addr, broadcast, 6);
  bp.channel = 0;
  esp_err_t peer_err = esp_now_add_peer(&bp);
  Serial.printf("Broadcast peer added: %d\n", peer_err);
  if (hasSatellite) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, satelliteMAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    esp_now_add_peer(&peer);
  }
  Serial.print("Core MAC: "); Serial.println(WiFi.macAddress());
}

void calibrate() {
  showStatus("  Calibrating...", "  Keep still!");
  delay(500);
  float pSum = 0, rSum = 0;
  for (int i = 0; i < 50; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    pSum += atan2(-a.acceleration.x, sqrt(a.acceleration.y*a.acceleration.y + a.acceleration.z*a.acceleration.z)) * 180.0/PI;
    rSum += atan2(a.acceleration.y, a.acceleration.z) * 180.0/PI;
    delay(20);
  }
  pitchOffset = pSum/50.0;
  rollOffset  = rSum/50.0;
  saveOffsets();
  display.clearDisplay();
  display.setCursor(0,0); display.println("  Cal saved!");
  display.printf("  P: %.1f\n", pitchOffset);
  display.printf("  R: %.1f\n", rollOffset);
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
    unsigned long held = millis() - btnPressTime;
    if (held >= CAL_HOLD_MS) {
      btnPressCount = 0;
      calibrate();
    } else {
      if (millis() - btnLastRelease < PAIR_DBL_MS) btnPressCount++;
      else btnPressCount = 1;
      btnLastRelease = millis();
      if (btnPressCount >= 2) {
        btnPressCount = 0;
        pairingMode = true;
        pairStart   = millis();
        Serial.println("Pairing mode");
      }
    }
  }
  if (pairingMode && millis() - pairStart > PAIR_MODE_MS) {
    pairingMode = false;
    Serial.println("Pair timeout");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BTN_PIN, INPUT_PULLUP);
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { Serial.println("OLED failed"); while(1); }
  showStatus("  InclinoCar", "  " FW_VERSION, "  Starting...");
  if (!mpu.begin()) { showStatus("  IMU not found!", "  Check GPIO6/7"); while(1); }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  loadOffsets();
  loadSatelliteMAC();
  setupESPNow();
  showStatus("  InclinoCar", "  " FW_VERSION,
    (pitchOffset != 0.0 || rollOffset != 0.0) ? "  Cal loaded" : "  Hold btn: cal");
  delay(1500);
}

void loop() {
  handleButton();
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float pitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y*a.acceleration.y + a.acceleration.z*a.acceleration.z)) * 180.0/PI - pitchOffset;
  float roll  = atan2(a.acceleration.y, a.acceleration.z) * 180.0/PI - rollOffset;

  if (hasSatellite) {
    DataPacket pkt = { MSG_DATA, pitch, roll };
    esp_now_send(satelliteMAC, (uint8_t*)&pkt, sizeof(pkt));
  }

  if (pairingMode) {
    int rem = (PAIR_MODE_MS - (millis()-pairStart)) / 1000;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE); display.setTextSize(1);
    display.setCursor(0, 0);  display.println("  Pairing mode...");
    display.setCursor(0, 16); display.println("  Power up remote");
    display.setCursor(0, 28); display.println("  display now");
    display.setCursor(0, 44); display.printf("  Timeout: %ds", rem);
    display.display();
    delay(100);
    return;
  }

  Serial.printf("P:%+6.1f  R:%+6.1f\n", pitch, roll);
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.println("  InclinoCar " FW_VERSION);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 16); display.printf("P%+6.1f", pitch);
  display.setTextSize(1);   display.print((char)247);
  display.setTextSize(2);
  display.setCursor(0, 38); display.printf("R%+6.1f", roll);
  display.setTextSize(1);   display.print((char)247);
  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  display.setCursor(0, 57); display.setTextSize(1);
  display.print(abs(pitch)<1.0&&abs(roll)<1.0 ? "  ** LEVEL **" : "  Adjust...");
  display.display();
  delay(100);
}
