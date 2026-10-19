/*
 * Smart Water Weighing System - Arduino Uno node
 * HX711 load cell, FC16 dot matrix, L298N pump driver, optional relay,
 * RS485 link to the ESP8266 node (the ESP8266 is the bus master).
 *
 * Libraries: HX711 (Bogdan Necula), MD_Parola, MD_MAX72XX
 * Disconnect the RS485 module from pins 0 and 1 while uploading.
 */

#include <SPI.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <HX711.h>
#include <MD_Parola.h>
#include <MD_MAX72XX.h>

#define HX_DT_PIN        3
#define HX_SCK_PIN       2
#define MATRIX_CS_PIN    10
#define MATRIX_MODULES   4
#define PUMP_EN_PIN      6
#define PUMP_IN1_PIN     5
#define PUMP_IN2_PIN     8
#define RELAY_PIN        7
#define RS485_DE_RE_PIN  4

#define CAL_FACTOR       2280.0f
#define BUS_BAUD         9600
#define SAMPLE_MS        200UL
#define SENSOR_STALE_MS  2000UL
#define NOTICE_MS        2000UL
#define PUMP_TIMEOUT_MS  120000UL
#define MAX_TARGET_ML    5000

MD_Parola display(MD_MAX72XX::FC16_HW, MATRIX_CS_PIN, MATRIX_MODULES);
HX711 scale;

static float weight = 0;
static bool haveReading = false;
static bool tared = false;
static unsigned long lastReadingAt = 0;

static bool pumpRunning = false;
static float pumpStartWeight = 0;
static int targetMl = 0;
static unsigned long pumpStartedAt = 0;

static bool relayOn = false;

static char noticeText[8];
static unsigned long noticeAt = 0;
static bool noticeActive = false;

static char shown[12] = "";
static char busLine[24];
static uint8_t busLen = 0;

static void show(const char *text) {
  if (strcmp(text, shown) == 0) return;
  strncpy(shown, text, sizeof(shown) - 1);
  shown[sizeof(shown) - 1] = '\0';
  display.setTextAlignment(PA_CENTER);
  display.print(text);
}

static void setNotice(const char *text) {
  strncpy(noticeText, text, sizeof(noticeText) - 1);
  noticeText[sizeof(noticeText) - 1] = '\0';
  noticeAt = millis();
  noticeActive = true;
}

static bool sensorOk(void) {
  return haveReading && (millis() - lastReadingAt < SENSOR_STALE_MS);
}

static void startPump(int ml) {
  targetMl = ml;
  pumpStartWeight = weight;
  pumpStartedAt = millis();
  pumpRunning = true;
  digitalWrite(PUMP_IN1_PIN, HIGH);
  digitalWrite(PUMP_IN2_PIN, LOW);
  digitalWrite(PUMP_EN_PIN, HIGH);
}

static void stopPump(bool finished, const char *message) {
  digitalWrite(PUMP_EN_PIN, LOW);
  digitalWrite(PUMP_IN1_PIN, LOW);
  digitalWrite(PUMP_IN2_PIN, LOW);
  if (pumpRunning && message != NULL) setNotice(message);
  pumpRunning = false;
  if (finished) targetMl = 0;
}

static void readScale(void) {
  if (haveReading && millis() - lastReadingAt < SAMPLE_MS) return;
  if (!scale.is_ready()) return;

  if (!tared) {
    scale.tare(10);
    tared = true;
  }

  float w = scale.get_units(1);
  if (!haveReading) {
    weight = w;
    haveReading = true;
  } else {
    weight += (w - weight) * 0.5f;
  }
  lastReadingAt = millis();
}

static void checkPump(void) {
  if (!pumpRunning) return;

  if (!sensorOk()) {
    stopPump(false, "FAIL");
  } else if (weight - pumpStartWeight >= targetMl) {
    stopPump(true, "DONE");
  } else if (millis() - pumpStartedAt > PUMP_TIMEOUT_MS) {
    stopPump(false, "FAIL");
  }
}

static void updateDisplay(void) {
  char buf[12];

  if (noticeActive) {
    if (millis() - noticeAt < NOTICE_MS) {
      show(noticeText);
      return;
    }
    noticeActive = false;
  }

  if (!sensorOk()) {
    show("ERR");
    return;
  }

  if (pumpRunning) {
    int left = targetMl - (int)(weight - pumpStartWeight);
    if (left < 0) left = 0;
    snprintf(buf, sizeof(buf), "%dml", left);
  } else {
    int grams = (int)(weight + 0.5f);
    if (grams < 0) grams = 0;
    if (grams > 9999) grams = 9999;
    snprintf(buf, sizeof(buf), "%dg", grams);
  }
  show(buf);
}

static void sendStatus(void) {
  char out[32];
  int grams = -1;

  if (sensorOk()) {
    grams = (int)(weight + 0.5f);
    if (grams < 0) grams = 0;
  }

  snprintf(out, sizeof(out), "W:%d,P:%d,R:%d\n", grams, pumpRunning ? 1 : 0, relayOn ? 1 : 0);

  digitalWrite(RS485_DE_RE_PIN, HIGH);
  delayMicroseconds(200);
  Serial.print(out);
  Serial.flush();
  delay(1);
  digitalWrite(RS485_DE_RE_PIN, LOW);
}

static void handleCommand(const char *cmd) {
  if (strcmp(cmd, "ON") == 0) {
    relayOn = true;
    digitalWrite(RELAY_PIN, HIGH);
  } else if (strcmp(cmd, "OFF") == 0) {
    relayOn = false;
    digitalWrite(RELAY_PIN, LOW);
  } else if (strcmp(cmd, "STOP") == 0) {
    stopPump(false, NULL);
  } else if (strncmp(cmd, "ML:", 3) == 0) {
    int ml = atoi(cmd + 3);
    if (!pumpRunning && sensorOk() && ml > 0 && ml <= MAX_TARGET_ML) startPump(ml);
  } else if (strcmp(cmd, "R") != 0) {
    return;
  }
  sendStatus();
}

static void pollBus(void) {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      busLine[busLen] = '\0';
      if (busLen > 0) handleCommand(busLine);
      busLen = 0;
    } else if (c >= 32 && c <= 126) {
      if (busLen < sizeof(busLine) - 1) busLine[busLen++] = c;
      else busLen = 0;
    }
  }
}

void setup() {
  pinMode(RS485_DE_RE_PIN, OUTPUT);
  digitalWrite(RS485_DE_RE_PIN, LOW);

  pinMode(PUMP_EN_PIN, OUTPUT);
  pinMode(PUMP_IN1_PIN, OUTPUT);
  pinMode(PUMP_IN2_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  stopPump(false, NULL);

  Serial.begin(BUS_BAUD);

  display.begin();
  display.setIntensity(5);
  display.displayClear();
  show("READY");

  scale.begin(HX_DT_PIN, HX_SCK_PIN);
  scale.set_scale(CAL_FACTOR);
  lastReadingAt = millis();
  delay(1500);
}

void loop() {
  readScale();
  checkPump();
  updateDisplay();
  pollBus();
}
