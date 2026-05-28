#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "config.h"

// ─── Globals ──────────────────────────────────────────────────
Adafruit_MPU6050 mpu;

float filteredPitch = 0.0f;
float filteredRoll  = 0.0f;
unsigned long lastSensorTime = 0;
unsigned long lastUpdate     = 0;

uint8_t coreMac[] = CORE_MAC_ADDR;

struct SatelliteData {
  float pitch;
  float roll;
};

// ─── ESP-NOW Send Callback ─────────────────────────────────────
void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  Serial.print("[ESP-NOW] Send: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

// ─── Sensor Fusion ─────────────────────────────────────────────
void updateIMU(float& pitch, float& roll) {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  unsigned long now = millis();
  float dt = (now - lastSensorTime) / 1000.0f;
  lastSensorTime = now;

  float accelPitch = atan2(accel.acceleration.y, accel.acceleration.z) * RAD_TO_DEG;
  float accelRoll  = atan2(-accel.acceleration.x, accel.acceleration.z) * RAD_TO_DEG;

  filteredPitch = COMP_FILTER_ALPHA * (filteredPitch + gyro.gyro.x * dt * RAD_TO_DEG)
                + (1.0f - COMP_FILTER_ALPHA) * accelPitch;
  filteredRoll  = COMP_FILTER_ALPHA * (filteredRoll  + gyro.gyro.y * dt * RAD_TO_DEG)
                + (1.0f - COMP_FILTER_ALPHA) * accelRoll;

  pitch = filteredPitch;
  roll  = filteredRoll;
}

// ─── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== InclinoCar Satellite Unit ===");

  // Print own MAC so user can configure core unit
  WiFi.mode(WIFI_STA);
  Serial.print("[INFO] Satellite MAC: ");
  Serial.println(WiFi.macAddress());

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!mpu.begin(MPU_I2C_ADDR)) {
    Serial.println("[ERROR] MPU-6050 not found! Check wiring.");
    while (1) delay(100);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("[OK] MPU-6050 initialized");

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed");
    while (1) delay(100);
  }
  esp_now_register_send_cb(onDataSent);

  // Register core unit as peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, coreMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ERROR] Failed to add ESP-NOW peer");
    Serial.println("[INFO] Update CORE_MAC_ADDR in config.h with core unit MAC");
  }

  lastSensorTime = millis();
  Serial.println("[OK] Setup complete — transmitting");
}

// ─── Loop ──────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
    lastUpdate = now;

    float pitch, roll;
    updateIMU(pitch, roll);

    SatelliteData data = { pitch, roll };
    esp_now_send(coreMac, (uint8_t*)&data, sizeof(data));

    Serial.printf("[IMU] Pitch: %+6.2f  Roll: %+6.2f\n", pitch, roll);
  }
}
