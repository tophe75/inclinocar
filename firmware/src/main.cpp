#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// I2C bus: SDA=GPIO6, SCL=GPIO7 (OLED + MPU-6050 shared)
// Calibration button: GPIO5 → GND (hold 1 second)
#define SDA_PIN       6
#define SCL_PIN       7
#define CAL_BTN_PIN   5
#define CAL_HOLD_MS   1000

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_MPU6050 mpu;

float pitchOffset = 0.0;
float rollOffset  = 0.0;

bool          btnWasPressed = false;
unsigned long btnPressTime  = 0;

void calibrate() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("  Calibrating...");
  display.println("  Keep still!");
  display.display();
  delay(500);

  float pSum = 0, rSum = 0;
  for (int i = 0; i < 50; i++) {
    sensors_event_t a, g, t;
    mpu.getEvent(&a, &g, &t);
    pSum += atan2(-a.acceleration.x,
                  sqrt(a.acceleration.y * a.acceleration.y +
                       a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
    rSum += atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
    delay(20);
  }
  pitchOffset = pSum / 50.0;
  rollOffset  = rSum / 50.0;

  Serial.printf("Cal done. pitch_off=%.2f roll_off=%.2f\n", pitchOffset, rollOffset);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("  Cal done!");
  display.printf("  P off: %.1f\n", pitchOffset);
  display.printf("  R off: %.1f\n", rollOffset);
  display.display();
  delay(1500);
}

void handleButton() {
  bool pressed = (digitalRead(CAL_BTN_PIN) == LOW);
  if (pressed && !btnWasPressed) {
    btnPressTime  = millis();
    btnWasPressed = true;
  }
  if (!pressed && btnWasPressed) {
    btnWasPressed = false;
  }
  if (pressed && btnWasPressed && (millis() - btnPressTime >= CAL_HOLD_MS)) {
    btnWasPressed = false;
    calibrate();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(CAL_BTN_PIN, INPUT_PULLUP);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("  InclinoCar " FW_VERSION);
  display.println("  Starting...");
  display.display();

  if (!mpu.begin()) {
    Serial.println("MPU-6050 failed");
    display.println("  IMU not found!");
    display.display();
    while (1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("Ready");
  display.println("  IMU OK");
  display.println("  Hold btn to cal");
  display.display();
  delay(1500);
}

void loop() {
  handleButton();

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float pitch = atan2(-a.acceleration.x,
                sqrt(a.acceleration.y * a.acceleration.y +
                     a.acceleration.z * a.acceleration.z)) * 180.0 / PI - pitchOffset;
  float roll  = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI - rollOffset;

  Serial.printf("P:%+6.1f  R:%+6.1f\n", pitch, roll);

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

  display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
  display.setCursor(0, 57);
  display.setTextSize(1);
  bool level = abs(pitch) < 1.0 && abs(roll) < 1.0;
  display.print(level ? "  ** LEVEL **" : "  Adjust...");

  display.display();
  delay(100);
}
