/**
 * @file    main.cpp
 * @brief   ESP8266 Water Tank Level Monitor
 *
 * Hardware
 * --------
 *   Microcontroller : ESP8266 (NodeMCU / Wemos D1 Mini)
 *   Sensor          : JSN-SR04M ultrasonic distance sensor
 *   TRIG_PIN        : GPIO 12 (D6)
 *   ECHO_PIN        : GPIO 13 (D7)
 *
 * Features
 * --------
 *   - Real-time WebSocket dashboard (HTML/CSS/JS served from PROGMEM)
 *   - IQR-filtered median distance reading for noise rejection
 *   - Live sensor calibration via the web UI (no reflash needed)
 *   - Exponential back-off reconnection on the client side
 *
 * Dependencies (install via PlatformIO / Arduino Library Manager)
 * ---------------------------------------------------------------
 *   - ArduinoJson   >= 6.x
 *   - ESPAsyncTCP
 *   - ESPAsyncWebServer
 */

#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <pgmspace.h>

// ── Web assets (served from flash via PROGMEM) ────────────────────
// The canonical sources live in data/ — edit those, then paste the
// updated text back into these string literals before flashing.

#include "web_assets.h"

// ── Wi-Fi credentials ─────────────────────────────────────────────
// TODO: move to a separate credentials.h (already in .gitignore)
const char *ssid     = "MANI";
const char *password = "homies2659";

// ── Sensor pins (JSN-SR04M) ───────────────────────────────────────
#define TRIG_PIN 12  // D6
#define ECHO_PIN 13  // D7

// ── Calibration / runtime state ───────────────────────────────────
float empty_distance_cm = 100.0; // Sensor → tank bottom (empty)
float full_distance_cm  =  20.0; // Sensor → max water level (full)
int   capacity_l        = 1000;  // Tank total capacity in litres

float current_distance  = -1.0;
int   current_pct       = 0;
int   current_vol       = 0;
bool  sensor_error      = false;

// ── Servers ───────────────────────────────────────────────────────
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ── Timing ───────────────────────────────────────────────────────
unsigned long lastReadTime      = 0;
unsigned long lastBroadcastTime = 0;
const unsigned long READ_INTERVAL      = 2000; // ms — sensor poll rate
const unsigned long BROADCAST_INTERVAL = 5000; // ms — WebSocket push rate

// ── Sensor: IQR-filtered median ───────────────────────────────────
#define SENSOR_SAMPLES        5
#define SENSOR_SAMPLE_DELAY_MS 50

/** Insertion-sort an array of floats in-place. */
static void sort_floats(float *arr, int n) {
  for (int i = 1; i < n; i++) {
    float key = arr[i];
    int j = i - 1;
    while (j >= 0 && arr[j] > key) {
      arr[j + 1] = arr[j];
      j--;
    }
    arr[j + 1] = key;
  }
}

/** Fire one ultrasonic pulse and return distance in cm, or -1 on timeout. */
static float readSingleSample() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 60 ms timeout → covers up to ~400 cm echo path + JSN settling time
  long duration = pulseIn(ECHO_PIN, HIGH, 60000);
  if (duration == 0) return -1.0f;
  return (duration * 0.0343f) / 2.0f;
}

/**
 * Poll the sensor SENSOR_SAMPLES times, reject outliers via IQR,
 * then store the median as current_distance and recompute level %.
 */
void readSensor() {
  float samples[SENSOR_SAMPLES];
  int valid_count = 0;

  for (int i = 0; i < SENSOR_SAMPLES; i++) {
    float d = readSingleSample();
    if (d >= 5.0f && d <= 400.0f) {   // Valid range for JSN-SR04M
      samples[valid_count++] = d;
    }
    delay(SENSOR_SAMPLE_DELAY_MS);
  }

  if (valid_count < 2) {
    sensor_error = true;
    current_distance = -1.0f;
    return;
  }

  sort_floats(samples, valid_count);

  // Interquartile range filter
  float q1  = samples[valid_count / 4];
  float q3  = samples[(valid_count * 3) / 4];
  float iqr = q3 - q1;

  // Widen bounds when variance is very small to avoid over-rejection
  float lower = (iqr < 2.0f) ? q1 - 5.0f : q1 - (iqr * 1.5f);
  float upper = (iqr < 2.0f) ? q3 + 5.0f : q3 + (iqr * 1.5f);

  float filtered[SENSOR_SAMPLES];
  int fcount = 0;
  for (int i = 0; i < valid_count; i++) {
    if (samples[i] >= lower && samples[i] <= upper) {
      filtered[fcount++] = samples[i];
    }
  }

  current_distance = (fcount == 0)
    ? samples[valid_count / 2]          // fallback: raw median
    : filtered[fcount / 2];             // preferred: filtered median

  sensor_error = false;

  // Compute level percentage and volume
  float range = empty_distance_cm - full_distance_cm;
  if (range <= 0.0f) {
    current_pct = 0;
    current_vol = 0;
  } else {
    float level = empty_distance_cm - current_distance;
    level = constrain(level, 0.0f, range);

    // round() avoids the integer-truncation bias in a plain cast
    current_pct = (int)round((level * 100.0f) / range);
    current_vol = (int)round((capacity_l * level) / range);
  }
}

// ── WebSocket ─────────────────────────────────────────────────────

/** Serialise current state and push to all connected clients. */
void notifyClients() {
  StaticJsonDocument<512> doc;
  doc["type"] = "state";

  JsonArray devices = doc.createNestedArray("devices");
  JsonObject tank   = devices.createNestedObject();
  tank["tank"]       = 1;
  tank["name"]       = "My Water Tank";
  tank["capacity_l"] = capacity_l;

  JsonObject state  = tank.createNestedObject("state");
  // On sensor error we keep current_pct (last good value) so the UI
  // freezes at the last known level rather than snapping to 0 %.
  state["level_pct"]    = current_pct;
  state["sensor_error"] = sensor_error;
  state["rssi"]         = WiFi.RSSI();
  state["conn_state"]   = "online";
  state["consumed_l"]   = 0;

  JsonObject settings = doc.createNestedObject("settings");
  settings["empty_distance_cm"] = empty_distance_cm;
  settings["full_distance_cm"]  = full_distance_cm;

  String output;
  serializeJson(doc, output);
  ws.textAll(output);
}

/** Parse and apply a settings-update message from the browser. */
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT)) return;

  data[len] = 0; // null-terminate for deserialisation

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, (char *)data)) return; // parse error → ignore

  if (doc["type"] == "update_settings") {
    if (doc.containsKey("empty_distance_cm"))
      empty_distance_cm = doc["empty_distance_cm"].as<float>();
    if (doc.containsKey("full_distance_cm"))
      full_distance_cm = doc["full_distance_cm"].as<float>();
    if (doc.containsKey("capacity_l"))
      capacity_l = doc["capacity_l"].as<int>();

    // Re-read sensor with new calibration and push update immediately
    readSensor();
    notifyClients();
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
             AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] Client #%u connected from %s\n",
                    client->id(), client->remoteIP().toString().c_str());
      notifyClients(); // Push current state to the new client
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] Client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

// ── Setup ─────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();      // clear stale cached credentials
  delay(100);
  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  Serial.print("[WiFi] Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print('.');
  }
  Serial.printf("\n[WiFi] Connected — IP: %s\n",
                WiFi.localIP().toString().c_str());

  // WebSocket handler
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Serve static assets from PROGMEM
  server.on("/",          HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/html",               index_html); });
  server.on("/index.html",HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/html",               index_html); });
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "text/css",                style_css);  });
  server.on("/app.js",    HTTP_GET, [](AsyncWebServerRequest *r){ r->send_P(200, "application/javascript",  app_js);     });

  server.begin();
  Serial.println("[HTTP] Server started");
}

// ── Loop ──────────────────────────────────────────────────────────
void loop() {
  ws.cleanupClients();

  unsigned long now = millis();

  if (now - lastReadTime >= READ_INTERVAL) {
    lastReadTime = now;
    readSensor();
  }

  if (now - lastBroadcastTime >= BROADCAST_INTERVAL) {
    lastBroadcastTime = now;
    notifyClients();
  }
}
