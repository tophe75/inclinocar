#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>

// OLED: SDA=GPIO3, SCL=GPIO4
// IMU:  SDA=GPIO6, SCL=GPIO7

TwoWire i2c0 = TwoWire(0);   // OLED bus
TwoWire i2c1 = TwoWire(1);   // IMU bus

Adafruit_SSD1306 display(128, 64, &i2c0, -1);
Adafruit_MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("InclinoCar " FW_VERSION);

  // Init OLED bus first, then display — pass false to skip internal Wire.begin
  i2c0.begin(3, 4, 400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C, false, false)) {
    Serial.println("OLED not found");
  } else {
    Serial.println("OLED OK");
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("InclinoCar " FW_VERSION);
    display.println("Starting...");
    display.display();
  }

  // Init IMU bus then sensor
  i2c1.begin(6, 7, 400000);
  if (!mpu.begin(0x68, &i2c1)) {
    Serial.println("MPU-6050 not found - check GPIO6/7");
    display.println("IMU not found!");
    display.println("Check GPIO6/7");
    display.display();
    while (1) delay(1000);
  }
  Serial.println("MPU-6050 OK");
  display.println("IMU OK");
  display.display();
  delay(1000);
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float pitch = atan2(a.acceleration.y,  a.acceleration.z) * 180.0 / PI;
  float roll  = atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / PI;

  Serial.printf("Pitch: %+6.1f  Roll: %+6.1f\n", pitch, roll);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("  InclinoCar " FW_VERSION);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(0, 16);
  display.printf("P%+6.1f", pitch);
  display.setTextSize(1);
  display.print((char)247);

  display.setTextSize(2);
  display.setCursor(0, 38);
  display.printf("R%+6.1f", roll);
  display.setTextSize(1);
  display.print((char)247);

  display.setCursor(0, 57);
  display.setTextSize(1);
  bool level = abs(pitch) < 1.0 && abs(roll) < 1.0;
  display.print(level ? "  ** LEVEL **" : "  Adjust...");

  display.display();
  delay(100);
}
