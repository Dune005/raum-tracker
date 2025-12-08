#include <GxEPD2_4C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Display Library selection
#include "GxEPD2_display_selection_new_style.h"

// ===== WiFi Konfiguration =====
const char* SSID = "HLY-77900";
const char* PASSWORD = "0jgp-42ej-ah8y-hnwz";

// ===== API Konfiguration =====
// WICHTIG: /occupancy/current ist der korrekte Endpoint (nicht /occupancy)
const char* apiOccupancyEndpoint = "https://corner.klaus-klebband.ch/api/v1/occupancy/current";
const char* heartbeatUrl = "https://corner.klaus-klebband.ch/update_count.php";

// space_id aus generate_occupancy_snapshot.php
const char* space_id = "880e8400-e29b-41d4-a716-446655440001";
const char* device_id = "990e8400-e29b-41d4-a716-44665544000";

// Daten-Struktur für Live-Daten vom Server
struct LiveData {
  int personenanzahl;           // Feld: people_estimate
  int lautstaerke_db;           // Feld: noise_db
  String stosszeit;             // Feld: später von statistics/today
  String level;                 // Feld: level (LOW/MEDIUM/HIGH)
  String timestamp;             // Feld: ts
};

LiveData currentData = {
  .personenanzahl = 0,
  .lautstaerke_db = 0,
  .stosszeit = "--:--",
  .level = "LOW",
  .timestamp = ""
};

// Zeit-Variablen (für Footer)
uint16_t currentYear = 2025;
uint8_t currentMonth = 11;
uint8_t currentDay = 12;
uint8_t currentHour = 13;
uint8_t currentMinute = 45;

// Layout-Konstanten
const int DISPLAY_WIDTH = 400;
const int DISPLAY_HEIGHT = 300;
const int FOOTER_HEIGHT = 40;
const int CONTENT_HEIGHT = DISPLAY_HEIGHT - FOOTER_HEIGHT;
const int QUARTER_WIDTH = DISPLAY_WIDTH / 2;
const int QUARTER_HEIGHT = CONTENT_HEIGHT / 2;

// Update-Variablen
unsigned long lastUpdateTime = 0;
const unsigned long UPDATE_INTERVAL = 90000;  // 90 Sekunden
const unsigned long DISPLAY_HEARTBEAT_INTERVAL = 60000;  // 60 Sekunden
unsigned long lastDisplayHeartbeat = 0;

void sendDisplayHeartbeat();

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== Raumtracker Display Start ===");

  display.init(115200, true, 2, false);

  connectToWiFi();
  syncTime();

  // Erstes Laden und Anzeigen
  if (fetchLiveData()) {
    updateDisplay();
  } else {
    Serial.println("Erste Abfrage fehlgeschlagen");
  }

  // Heartbeat direkt nach dem Start senden, damit der Server sofort einen Timestamp hat
  sendDisplayHeartbeat();
}

void loop() {
  unsigned long now = millis();

  incrementTime();

  if (now - lastUpdateTime >= UPDATE_INTERVAL) {
    lastUpdateTime = now;
    Serial.println("\n--- Minute Update: fetching live data ---");
    if (fetchLiveData()) {
      updateDisplay();
    } else {
      Serial.println("ERROR: Fehler beim Abrufen der Live-Daten");
    }
  }

  if (now - lastDisplayHeartbeat >= DISPLAY_HEARTBEAT_INTERVAL) {
    sendDisplayHeartbeat();
  }

  delay(200);
}

// ===== WiFi Funktionen =====
void connectToWiFi() {
  Serial.print("Verbinde mit WiFi: ");
  Serial.println(SSID);

  WiFi.begin(SSID, PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(250);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi verbunden");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ WiFi Verbindung fehlgeschlagen");
  }
}

// ===== NTP Zeit Synchronisierung =====
void syncTime() {
  configTime(1 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 24 * 3600 && attempts < 20) {
    delay(250);
    now = time(nullptr);
    attempts++;
  }
  if (now >= 24 * 3600) updateTimeFromSystem();
}

void updateTimeFromSystem() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  currentYear = 1900 + t->tm_year;
  currentMonth = 1 + t->tm_mon;
  currentDay = t->tm_mday;
  currentHour = t->tm_hour;
  currentMinute = t->tm_min;
}

void incrementTime() {
  static unsigned long lastInc = 0;
  unsigned long m = millis();
  if (m - lastInc >= 60000) {
    lastInc = m;
    currentMinute++;
    if (currentMinute >= 60) { currentMinute = 0; currentHour++; }
    if (currentHour >= 24) { currentHour = 0; currentDay++; }
  }
}

// ===== API Anfragen =====
// Holt Daten von /occupancy/current?space_id=...
// Response-Felder (aus current.php):
//   - people_estimate   -> personenanzahl
//   - level             -> level (LOW/MEDIUM/HIGH)
//   - noise_db          -> lautstaerke_db
//   - ts                -> timestamp (YYYY-MM-DD HH:MM:SS)
bool fetchLiveData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi nicht verbunden");
    return false;
  }

  HTTPClient http;
  String url = String(apiOccupancyEndpoint) + "?space_id=" + space_id;
  
  Serial.print("Anfrage URL: ");
  Serial.println(url);

  http.begin(url);
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  int httpCode = http.GET();
  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("ERROR: HTTP Fehler ");
    Serial.println(httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  Serial.print("Payload erhalten, Größe: ");
  Serial.print(payload.length());
  Serial.println(" Bytes");

  http.end();

  // JSON Parse
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    Serial.print("ERROR: JSON Parse fehler: ");
    Serial.println(err.c_str());
    Serial.print("Raw Payload: ");
    Serial.println(payload);
    return false;
  }

  // Response-Struktur: { success: true, data: { ... } } oder direkt { people_estimate: ..., ... }
  JsonVariant data = doc;
  
  // Falls Daten in "data"-Feld verpackt sind
  if (doc.containsKey("data")) {
    data = doc["data"];
  }

  // Extrahiere Felder (Namen EXAKT wie in current.php)
  if (data.containsKey("people_estimate") && !data["people_estimate"].isNull()) {
    currentData.personenanzahl = data["people_estimate"].as<int>();
    Serial.print("✓ people_estimate: ");
    Serial.println(currentData.personenanzahl);
  } else {
    Serial.println("WARN: people_estimate nicht gefunden");
    currentData.personenanzahl = 0;
  }

  if (data.containsKey("noise_db") && !data["noise_db"].isNull()) {
    currentData.lautstaerke_db = (int)data["noise_db"].as<float>();
    Serial.print("✓ noise_db: ");
    Serial.println(currentData.lautstaerke_db);
  } else {
    Serial.println("WARN: noise_db nicht gefunden");
    currentData.lautstaerke_db = 0;
  }

  if (data.containsKey("level") && !data["level"].isNull()) {
    currentData.level = String((const char*)data["level"]);
    Serial.print("✓ level: ");
    Serial.println(currentData.level);
  } else {
    Serial.println("WARN: level nicht gefunden");
    currentData.level = "LOW";
  }

  if (data.containsKey("timestamp") && !data["timestamp"].isNull()) {
    currentData.timestamp = String((const char*)data["timestamp"]);
    Serial.print("✓ timestamp: ");
    Serial.println(currentData.timestamp);
  } else if (data.containsKey("ts") && !data["ts"].isNull()) {
    currentData.timestamp = String((const char*)data["ts"]);
    Serial.print("✓ ts: ");
    Serial.println(currentData.timestamp);
  }

  // Aktualisiere Footer-Zeit aus Timestamp
  if (currentData.timestamp.length() >= 16) {
    int y = currentData.timestamp.substring(0, 4).toInt();
    int mo = currentData.timestamp.substring(5, 7).toInt();
    int d = currentData.timestamp.substring(8, 10).toInt();
    int hh = currentData.timestamp.substring(11, 13).toInt();
    int mm = currentData.timestamp.substring(14, 16).toInt();
    if (y > 2000) {
      currentYear = y;
      currentMonth = mo;
      currentDay = d;
      currentHour = hh;
      currentMinute = mm;
    }
  }

  Serial.println("✓ Daten erfolgreich geparst");
  return true;
}

void sendDisplayHeartbeat() {
  lastDisplayHeartbeat = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Display heartbeat übersprungen: kein WiFi");
    return;
  }

  HTTPClient http;
  http.begin(heartbeatUrl);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String payload = "display_ping=1";
  if (device_id && device_id[0] != '\0') {
    payload += "&device_id=";
    payload += device_id;
  }

  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.print("Display heartbeat gesendet (HTTP ");
    Serial.print(httpCode);
    Serial.println(")");
  } else {
    Serial.print("Display heartbeat fehlgeschlagen: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ===== Display Funktionen =====
void updateDisplay() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawQuadrants();
    drawFooter();
  } while (display.nextPage());
  Serial.println("Display aktualisiert");
}

void drawQuadrants() {
  drawPersonenQuadrant(0, 0, QUARTER_WIDTH, QUARTER_HEIGHT);
  drawAuslastungQuadrant(QUARTER_WIDTH, 0, QUARTER_WIDTH, QUARTER_HEIGHT);
  drawLautstaerkeQuadrant(0, QUARTER_HEIGHT, QUARTER_WIDTH, QUARTER_HEIGHT);
  drawStosszeitQuadrant(QUARTER_WIDTH, QUARTER_HEIGHT, QUARTER_WIDTH, QUARTER_HEIGHT);

  display.drawLine(QUARTER_WIDTH, 0, QUARTER_WIDTH, CONTENT_HEIGHT, GxEPD_BLACK);
  display.drawLine(0, QUARTER_HEIGHT, DISPLAY_WIDTH, QUARTER_HEIGHT, GxEPD_BLACK);
  display.drawLine(0, CONTENT_HEIGHT, DISPLAY_WIDTH, CONTENT_HEIGHT, GxEPD_BLACK);
}

void drawPersonenQuadrant(int x, int y, int width, int height) {
  int persons = currentData.personenanzahl;

  uint16_t bgColor = GxEPD_WHITE;
  uint16_t textColor = GxEPD_BLACK;

  if (persons < 6) {
    bgColor = GxEPD_WHITE;
    textColor = GxEPD_BLACK;
  } else if (persons < 20) {
    bgColor = GxEPD_YELLOW;
    textColor = GxEPD_BLACK;
  } else {
    bgColor = GxEPD_RED;
    textColor = GxEPD_WHITE;
  }

  display.fillRect(x, y, width, height, bgColor);
  display.drawRect(x, y, width, height, GxEPD_BLACK);

  display.setFont(&FreeSans9pt7b);
  display.setTextColor(textColor);
  display.setCursor(x + 10, y + 25);
  display.println("PERSONEN");

  display.setFont(&FreeSansBold24pt7b);
  String value = String(persons);

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(value, 0, 0, &tbx, &tby, &tbw, &tbh);

  int textX = x + (width - tbw) / 2;
  int textY = y + height - 40;

  display.setCursor(textX, textY);
  display.println(value);
}

void drawAuslastungQuadrant(int x, int y, int width, int height) {
  String category = "niedrig";
  uint16_t bgColor = GxEPD_WHITE;
  uint16_t textColor = GxEPD_BLACK;

  // Verwende serverseitiges level
  if (currentData.level.equalsIgnoreCase("LOW")) {
    category = "niedrig";
    bgColor = GxEPD_WHITE;
    textColor = GxEPD_BLACK;
  } else if (currentData.level.equalsIgnoreCase("MEDIUM")) {
    category = "mittel";
    bgColor = GxEPD_YELLOW;
    textColor = GxEPD_BLACK;
  } else if (currentData.level.equalsIgnoreCase("HIGH")) {
    category = "hoch";
    bgColor = GxEPD_RED;
    textColor = GxEPD_WHITE;
  }

  display.fillRect(x, y, width, height, bgColor);
  display.drawRect(x, y, width, height, GxEPD_BLACK);

  display.setFont(&FreeSans9pt7b);
  display.setTextColor(textColor);
  display.setCursor(x + 10, y + 25);
  display.println("AUSLASTUNG");

  display.setFont(&FreeSansBold24pt7b);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(category, 0, 0, &tbx, &tby, &tbw, &tbh);

  int textX = x + (width - tbw) / 2;
  int textY = y + height - 40;

  display.setCursor(textX, textY);
  display.println(category);
}

void drawLautstaerkeQuadrant(int x, int y, int width, int height) {
  display.drawRect(x, y, width, height, GxEPD_BLACK);

  display.setFont(&FreeSans9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(x + 10, y + 25);
  display.println("LAUTSTÄRKE");

  String category = getLautstaerkeCategory(currentData.lautstaerke_db);

  display.setFont(&FreeSansBold18pt7b);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(category, 0, 0, &tbx, &tby, &tbw, &tbh);

  int textX = x + (width - tbw) / 2;
  int textY = y + height / 2 + 10;

  display.setCursor(textX, textY);
  display.println(category);

  display.setFont(&FreeSans9pt7b);
  String dbStr = "(" + String(currentData.lautstaerke_db) + " dB)";
  display.getTextBounds(dbStr, 0, 0, &tbx, &tby, &tbw, &tbh);
  display.setCursor(x + (width - tbw) / 2, textY + 20);
  display.println(dbStr);
}

void drawStosszeitQuadrant(int x, int y, int width, int height) {
  display.drawRect(x, y, width, height, GxEPD_BLACK);

  display.setFont(&FreeSans9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(x + 10, y + 25);
  display.println("STOSSZEIT");

  display.setFont(&FreeSansBold24pt7b);
  String value = currentData.stosszeit;
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(value, 0, 0, &tbx, &tby, &tbw, &tbh);

  int textX = x + (width - tbw) / 2;
  int textY = y + height - 40;

  display.setCursor(textX, textY);
  display.println(value);
}

void drawFooter() {
  int footerY = CONTENT_HEIGHT;

  display.drawRect(0, footerY, DISPLAY_WIDTH, FOOTER_HEIGHT, GxEPD_BLACK);

  display.setFont(&FreeSans9pt7b);
  display.setTextColor(GxEPD_BLACK);

  String dateStr = formatDate();
  display.setCursor(10, footerY + 25);
  display.println(dateStr);

  String timeStr = formatTime();
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(timeStr, 0, 0, &tbx, &tby, &tbw, &tbh);
  display.setCursor(DISPLAY_WIDTH - tbw - 10, footerY + 25);
  display.println(timeStr);
}

String getLautstaerkeCategory(int db) {
  if (db <= 0) return "n/a";
  if (db < 50) return "leise";
  if (db < 65) return "mittel";
  if (db < 80) return "laut";
  return "sehr laut";
}

String formatDate() {
  String day = (currentDay < 10) ? "0" + String(currentDay) : String(currentDay);
  String month = (currentMonth < 10) ? "0" + String(currentMonth) : String(currentMonth);
  return day + "/" + month + "/" + String(currentYear);
}

String formatTime() {
  String hour = (currentHour < 10) ? "0" + String(currentHour) : String(currentHour);
  String minute = (currentMinute < 10) ? "0" + String(currentMinute) : String(currentMinute);
  return hour + ":" + minute;
}
