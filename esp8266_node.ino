/*
 * Smart Water Weighing System - ESP8266 node
 * Bridges the Arduino Uno (RS485) and ThingSpeak.
 *   Field 1: weight in grams (written by this node)
 *   Field 2: relay, 1 = on, 0 = off
 *   Field 3: pump target in ml, cleared back to 0 once the pump starts
 *
 * Board: NodeMCU 1.0 (ESP-12E) or any ESP8266 board.
 * Fill in WiFi and ThingSpeak details in secrets.h before uploading.
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <SoftwareSerial.h>
#include <stdio.h>
#include <string.h>
#include "secrets.h"

#define RS485_RX_PIN      14
#define RS485_TX_PIN      12
#define RS485_DE_RE_PIN   2

#define BUS_BAUD          9600
#define REPLY_TIMEOUT_MS  800UL
#define HTTP_TIMEOUT_MS   6000
#define CYCLE_MS          16000UL
#define WIFI_TIMEOUT_MS   15000UL

SoftwareSerial rs485(RS485_RX_PIN, RS485_TX_PIN);

typedef struct {
  int weight;
  int pump;
  int relay;
} Status;

static bool clearPumpField = false;
static unsigned long lastCycle = 0;

static bool busExchange(const char *cmd, Status *st) {
  char line[48];
  size_t n = 0;

  while (rs485.available() > 0) rs485.read();

  digitalWrite(RS485_DE_RE_PIN, HIGH);
  delay(2);
  rs485.print(cmd);
  rs485.print('\n');
  rs485.flush();
  delay(3);
  digitalWrite(RS485_DE_RE_PIN, LOW);

  unsigned long started = millis();
  while (millis() - started < REPLY_TIMEOUT_MS) {
    while (rs485.available() > 0) {
      char c = (char)rs485.read();
      if (c == '\n') {
        line[n] = '\0';
        char *p = strstr(line, "W:");
        if (p != NULL && sscanf(p, "W:%d,P:%d,R:%d", &st->weight, &st->pump, &st->relay) == 3) {
          return true;
        }
        n = 0;
      } else if (c >= 32 && c <= 126 && n < sizeof(line) - 1) {
        line[n++] = c;
      }
    }
    delay(1);
  }
  return false;
}

static bool httpGet(const char *url, String &body) {
  WiFiClient client;
  HTTPClient http;

  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code == HTTP_CODE_OK) body = http.getString();
  http.end();
  return code == HTTP_CODE_OK;
}

static bool uploadWeight(int grams) {
  char url[200];
  String body;

  if (clearPumpField) {
    snprintf(url, sizeof(url),
             "http://api.thingspeak.com/update?api_key=%s&field1=%d&field3=0",
             TS_WRITE_KEY, grams);
  } else {
    snprintf(url, sizeof(url),
             "http://api.thingspeak.com/update?api_key=%s&field1=%d",
             TS_WRITE_KEY, grams);
  }

  if (!httpGet(url, body)) return false;
  body.trim();
  if (body == "0") return false;

  clearPumpField = false;
  return true;
}

static bool readField(int field, int *value) {
  char url[200];
  String body;

  snprintf(url, sizeof(url),
           "http://api.thingspeak.com/channels/%s/fields/%d/last.txt?api_key=%s",
           TS_CHANNEL_ID, field, TS_READ_KEY);

  if (!httpGet(url, body)) return false;
  *value = body.toInt();
  return true;
}

static void connectWiFi(void) {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" connected, IP ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" failed, retrying");
  }
}

static void runCycle(void) {
  Status st = {0, 0, 0};

  if (!busExchange("R", &st) && !busExchange("R", &st)) {
    Serial.println("No reply from Arduino");
    return;
  }
  Serial.printf("Weight %d g, pump %d, relay %d\n", st.weight, st.pump, st.relay);

  if (st.weight >= 0) {
    if (uploadWeight(st.weight)) Serial.println("Uploaded to ThingSpeak");
    else Serial.println("Upload failed");
  }

  int relayField = 0;
  if (readField(2, &relayField)) {
    int wanted = (relayField == 1) ? 1 : 0;
    if (wanted != st.relay) busExchange(wanted ? "ON" : "OFF", &st);
  }

  int targetMl = 0;
  if (readField(3, &targetMl) && targetMl > 0 && !st.pump) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "ML:%d", targetMl);
    if (busExchange(cmd, &st) && st.pump) {
      clearPumpField = true;
      Serial.printf("Pump started for %d ml\n", targetMl);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  pinMode(RS485_DE_RE_PIN, OUTPUT);
  digitalWrite(RS485_DE_RE_PIN, LOW);
  rs485.begin(BUS_BAUD);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connectWiFi();

  lastCycle = millis() - CYCLE_MS;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return;
  }

  if (millis() - lastCycle >= CYCLE_MS) {
    lastCycle = millis();
    runCycle();
  }
  delay(10);
}
