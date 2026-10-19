/*
 * HX711 calibration helper (run on the Arduino Uno, USB serial at 9600 baud).
 * Disconnect the RS485 module from pins 0 and 1 first.
 *
 *   t           tare with an empty scale
 *   c <grams>   calibrate with a known weight on the scale, e.g. c 500
 *
 * Copy the printed factor into CAL_FACTOR in arduino_uno.ino.
 */

#include <stdio.h>
#include <HX711.h>

#define HX_DT_PIN   3
#define HX_SCK_PIN  2

HX711 scale;

static char line[24];
static uint8_t lineLen = 0;
static unsigned long lastPrint = 0;

static void waitForScale(void) {
  while (!scale.is_ready()) {
    Serial.println("HX711 not found, check wiring");
    delay(1000);
  }
}

static void runCommand(const char *cmd) {
  float grams = 0;

  waitForScale();
  if (cmd[0] == 't') {
    scale.tare(10);
    Serial.println("Tared");
  } else if (sscanf(cmd, "c %f", &grams) == 1 && grams > 0) {
    float factor = scale.get_value(10) / grams;
    scale.set_scale(factor);
    Serial.print("CAL_FACTOR = ");
    Serial.println(factor, 2);
  } else {
    Serial.println("Use: t  or  c <grams>");
  }
}

void setup() {
  Serial.begin(9600);
  scale.begin(HX_DT_PIN, HX_SCK_PIN);
  scale.set_scale();
  waitForScale();
  scale.tare(10);
  Serial.println("Empty scale tared. Place a known weight, then send: c <grams>");
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen > 0) {
        line[lineLen] = '\0';
        runCommand(line);
        lineLen = 0;
      }
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }

  if (millis() - lastPrint >= 500 && scale.is_ready()) {
    lastPrint = millis();
    Serial.print("Reading: ");
    Serial.print(scale.get_units(1), 1);
    Serial.println(" g");
  }
}
