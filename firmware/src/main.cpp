#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>

// ─── Pin config ──────────────────────────────────────────────
// OLED: SDA=GPIO3, SCL=GPIO4  (Bus 0)
// IMU:  SDA=GPIO6, SCL=GPIO7  (Bus 1)
#define OLED_SDA  3
#define OLED_SCL  4
#define IMU_SDA   6
#define IMU_SCL   7

// ─── Objects ─────────────────────────────────────────────────
Adafruit_SSD1306 display(128, 64, &Wire,  -1);
Adafruit_MPU6050 mpu;

void setup() {
  Serial.begin(115200);
  Serial.println("InclinoCar " FW_VERSION " starting...");

  // Set OLED pins — let display.begin() init the bus itself
  Wire.setPins(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
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
  Wire1.begin(IMU_SDA, IMU_SCL);
  if (!mpu.begin(0x68, &Wire1)) {
    Serial.println("MPU-6050 not found");
    display.println("IMU not found!");
    display.println("Check GPIO6/7");
    display.display();
  } else {
    Serial.println("MPU-6050 OK");
    display.println("IMU OK");
    display.display();
    delay(1000);
  }
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Calculate pitch and roll from accelerometer
  float pitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  float roll  = atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / PI;

  Serial.printf("Pitch: %+6.1f  Roll: %+6.1f\n", pitch, roll);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Title
  display.setCursor(0, 0);
  display.println("  InclinoCar " FW_VERSION);
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // Pitch
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.printf("P%+6.1f", pitch);
  display.setTextSize(1);
  display.print((char)247);  // degree symbol

  // Roll
  display.setTextSize(2);
  display.setCursor(0, 38);
  display.printf("R%+6.1f", roll);
  display.setTextSize(1);
  display.print((char)247);

  // Level indicator
  bool level = abs(pitch) < 1.0 && abs(roll) < 1.0;
  display.setCursor(0, 57);
  display.setTextSize(1);
  display.print(level ? "  ** LEVEL **" : "  Adjust...");

  display.display();

  delay(100);  // 10Hz update
}
