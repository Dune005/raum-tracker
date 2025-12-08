#include <GxEPD2_4C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "connectWiFi_zuhause.h"

// Display Library selection
#include "GxEPD2_display_selection_new_style.h"

// ===== API Konfiguration =====
const char* apiOccupancyEndpoint    = "https://corner.klaus-klebband.ch/api/v1/occupancy/current";
const char* heartbeatUrl            = "https://corner.klaus-klebband.ch/update_count.php";
const char* space_id                = "880e8400-e29b-41d4-a716-446655440001";
const char* device_id               = "990e8400-e29b-41d4-a716-44665544000";

// Daten-Struktur für Live-Daten
struct LiveData {
  int    personenanzahl;   // people_estimate
  int    lautstaerke_db;   // noise_db
  String stosszeit;        // später von statistics/today
  String level;            // level (LOW/MEDIUM/HIGH)
  String timestamp;        // ts
};

LiveData currentData = {
  .personenanzahl = 0,
  .lautstaerke_db = 0,
  .stosszeit      = "--:--",
  .level          = "LOW",
  .timestamp      = ""
};

// Zeit-Variablen
uint16_t currentYear   = 2025;
uint8_t  currentMonth  = 11;
uint8_t  currentDay    = 12;
uint8_t  currentHour   = 13;
uint8_t  currentMinute = 45;

// Layout-Konstanten
const int DISPLAY_WIDTH  = 400;
const int DISPLAY_HEIGHT = 300;

const int ROWS   = 2;
const int COLS   = 2;
const int CELL_W = DISPLAY_WIDTH  / COLS; // 200
const int CELL_H = DISPLAY_HEIGHT / ROWS; // 150

// Update-Variablen
unsigned long lastUpdateTime                = 0;
const unsigned long UPDATE_INTERVAL         = 90000;  // 90 s
const unsigned long DISPLAY_HEARTBEAT_INTERVAL = 60000;
unsigned long lastDisplayHeartbeat          = 0;

// Vorwärtsdeklarationen
void sendDisplayHeartbeat();
void drawQuadrants();
void drawPersonenField(int x, int y, int w, int h);
void drawLautstaerkeField(int x, int y, int w, int h);
void drawAuslastungField(int x, int y, int w, int h);
void drawZeitDatumField(int x, int y, int w, int h);
String getLautstaerkeCategory(int db);
String formatDate();
String formatTime();

// ===== Setup =====
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n=== Raumtracker Display Start (4C) ===");

  display.init(115200, true, 2, false);

  // WiFi-Verbindung über externe Funktion
  connectWiFi();
  syncTime();

  if (fetchLiveData()) {
    updateDisplay();
  } else {
    Serial.println("Erste Abfrage fehlgeschlagen");
  }

  sendDisplayHeartbeat();
}

// ===== Loop =====
void loop() {
  unsigned long now = millis();

  incrementTime();

  if (now - lastUpdateTime >= UPDATE_INTERVAL) {
    lastUpdateTime = now;
    Serial.println("\n--- Periodisches Update: fetching live data ---");
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

// ===== Zeit / NTP =====
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
  currentYear   = 1900 + t->tm_year;
  currentMonth  = 1 + t->tm_mon;
  currentDay    = t->tm_mday;
  currentHour   = t->tm_hour;
  currentMinute = t->tm_min;
}

void incrementTime() {
  static unsigned long lastInc = 0;
  unsigned long m = millis();
  if (m - lastInc >= 60000) {
    lastInc = m;
    currentMinute++;
    if (currentMinute >= 60) { currentMinute = 0; currentHour++; }
    if (currentHour   >= 24) { currentHour   = 0; currentDay++; }
  }
}

// ===== API =====
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
  Serial.print("Payload Größe: ");
  Serial.print(payload.length());
  Serial.println(" Bytes");
  http.end();

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("ERROR: JSON Parse Fehler: ");
    Serial.println(err.c_str());
    return false;
  }

  JsonVariant data = doc;
  if (doc.containsKey("data")) {
    data = doc["data"];
  }

  if (data.containsKey("people_estimate") && !data["people_estimate"].isNull()) {
    currentData.personenanzahl = data["people_estimate"].as<int>();
  } else {
    currentData.personenanzahl = 0;
  }

  if (data.containsKey("noise_db") && !data["noise_db"].isNull()) {
    currentData.lautstaerke_db = (int)data["noise_db"].as<float>();
  } else {
    currentData.lautstaerke_db = 0;
  }

  if (data.containsKey("level") && !data["level"].isNull()) {
    currentData.level = String((const char*)data["level"]);
  } else {
    currentData.level = "LOW";
  }

  if (data.containsKey("timestamp") && !data["timestamp"].isNull()) {
    currentData.timestamp = String((const char*)data["timestamp"]);
  } else if (data.containsKey("ts") && !data["ts"].isNull()) {
    currentData.timestamp = String((const char*)data["ts"]);
  }

  if (currentData.timestamp.length() >= 16) {
    int y  = currentData.timestamp.substring(0, 4).toInt();
    int mo = currentData.timestamp.substring(5, 7).toInt();
    int d  = currentData.timestamp.substring(8, 10).toInt();
    int hh = currentData.timestamp.substring(11, 13).toInt();
    int mm = currentData.timestamp.substring(14, 16).toInt();
    if (y > 2000) {
      currentYear   = y;
      currentMonth  = mo;
      currentDay    = d;
      currentHour   = hh;
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

// ===== Display =====
void updateDisplay() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawQuadrants();
  } while (display.nextPage());
  Serial.println("Display aktualisiert");
}

void drawQuadrants() {
  // Layout:
  // Oben links: Personen
  // Oben rechts: Lautstärke
  // Unten links: Auslastung (einziges Feld mit Farbwechsel)
  // Unten rechts: Zeit & Datum

  drawPersonenField(0,       0,      CELL_W, CELL_H);
  drawLautstaerkeField(CELL_W, 0,    CELL_W, CELL_H);
  drawAuslastungField(0,     CELL_H, CELL_W, CELL_H);
  drawZeitDatumField(CELL_W, CELL_H, CELL_W, CELL_H);

  // Trennlinien
  display.drawLine(CELL_W, 0,        CELL_W, DISPLAY_HEIGHT, GxEPD_BLACK);
  display.drawLine(0,      CELL_H,   DISPLAY_WIDTH, CELL_H,  GxEPD_BLACK);
}

// Personen: zentriert, schwarz auf weiß, "~" vor Zahl
void drawPersonenField(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, GxEPD_WHITE);
  display.drawRect(x, y, w, h, GxEPD_BLACK);

  int persons = currentData.personenanzahl;

  // Überschrift
  display.setFont(&FreeSans9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(x + 10, y + 25);
  display.println("PERSONEN");

  // Wert mittig
  display.setFont(&FreeSansBold24pt7b);
  String value = "~" + String(persons);

  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(value, 0, 0, &tbx, &tby, &tbw, &tbh);

  int textX = x + (w - tbw) / 2;
  int textY = y + (h + tbh) / 2; // vertikal mittig

  display.setCursor(textX, textY);
  display.println(value);
}

// Lautstärke-Feld oben rechts, zentriert, schwarz auf weiß
String getLautstaerkeCategory(int db) {
  if (db <= 0) return "n/a";
  if (db < 50)  return "leise";
  if (db < 65)  return "mittel";
  if (db < 80)  return "laut";
  return "sehr laut";
}

void drawLautstaerkeField(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, GxEPD_WHITE);
  display.drawRect(x, y, w, h, GxEPD_BLACK);

  // Überschrift
  display.setFont(&FreeSans9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(x + 10, y + 25);
  display.println("LAUTSTARKE");

  String category = getLautstaerkeCategory(currentData.lautstaerke_db);

  // Kategorie mittig
  display.setFont(&FreeSansBold18pt7b);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(category, 0, 0, &tbx, &tby, &tbw, &tbh);

  int centerY = y + h / 2;
  int textX = x + (w - tbw) / 2;
  int textY = centerY - 5; // leicht nach oben

  display.setCursor(textX, textY);
  display.println(category);

  // dB-Wert mittig unterhalb
  display.setFont(&FreeSans9pt7b);
  String dbStr = "(" + String(currentData.lautstaerke_db) + " dB)";
  display.getTextBounds(dbStr, 0, 0, &tbx, &tby, &tbw, &tbh);
  int dbX = x + (w - tbw) / 2;
  int dbY = textY + tbh + 5;
  display.setCursor(dbX, dbY);
  display.println(dbStr);
}

// Auslastung unten links: Farbwechsel nur bei HIGH, mittel wieder schwarz/weiss
void drawAuslastungField(int x, int y, int w, int h) {
  String category = "niedrig";
  uint16_t bgColor   = GxEPD_WHITE;
  uint16_t textColor = GxEPD_BLACK;

  if (currentData.level.equalsIgnoreCase("LOW")) {
    category  = "niedrig";
    bgColor   = GxEPD_WHITE;
    textColor = GxEPD_BLACK;
  } else if (currentData.level.equalsIgnoreCase("MEDIUM")) {
    category  = "mittel";
    bgColor   = GxEPD_WHITE;      // Anpassung: wieder weiß
    textColor = GxEPD_BLACK;      // Anpassung: schwarze Schrift
  } else if (currentData.level.equalsIgnoreCase("HIGH")) {
    category  = "hoch";
    bgColor   = GxEPD_BLACK;
    textColor = GxEPD_WHITE;
  }

  display.fillRect(x, y, w, h, bgColor);
  display.drawRect(x, y, w, h, GxEPD_BLACK);

  // Überschrift
  display.setFont(&FreeSans9pt7b);
  display.setTextColor(textColor);
  display.setCursor(x + 10, y + 25);
  display.println("AUSLASTUNG");

  // Kategorie mittig
  display.setFont(&FreeSansBold24pt7b);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(category, 0, 0, &tbx, &tby, &tbw, &tbh);

  int textX = x + (w - tbw) / 2;
  int textY = y + (h + tbh) / 2;

  display.setCursor(textX, textY);
  display.println(category);
}

// Zeit & Datum unten rechts: Überschrift + kleinere Schrift, alles zentriert
String formatDate() {
  String day   = (currentDay   < 10) ? "0" + String(currentDay)   : String(currentDay);
  String month = (currentMonth < 10) ? "0" + String(currentMonth) : String(currentMonth);
  return day + "/" + month + "/" + String(currentYear);
}

String formatTime() {
  String hour   = (currentHour   < 10) ? "0" + String(currentHour)   : String(currentHour);
  String minute = (currentMinute < 10) ? "0" + String(currentMinute) : String(currentMinute);
  return hour + ":" + minute;
}

void drawZeitDatumField(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, GxEPD_WHITE);
  display.drawRect(x, y, w, h, GxEPD_BLACK);

  display.setTextColor(GxEPD_BLACK);

  // Überschrift wie bei den anderen Feldern
  display.setFont(&FreeSans9pt7b);
  display.setCursor(x + 10, y + 25);
  display.println("LETZTE AUSWERTUNG");

  String timeStr = formatTime();
  String dateStr = formatDate();

  // Zeit etwas kleiner (z.B. 18pt statt 24pt)
  display.setFont(&FreeSansBold18pt7b);
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(timeStr, 0, 0, &tbx, &tby, &tbw, &tbh);
  int timeX = x + (w - tbw) / 2;

  // Datum ebenfalls kleiner (12pt) – beide mittig im freien Bereich unter der Überschrift
  display.setFont(&FreeSans12pt7b);
  int16_t dbx, dby;
  uint16_t dbw, dbh;
  display.getTextBounds(dateStr, 0, 0, &dbx, &dby, &dbw, &dbh);

  // vertikale Zentrierung des Blocks (Zeit + Datum) im Bereich unter der Überschrift (~ab y+40)
  int freeTop    = y + 40;
  int blockHeight = tbh + 6 + dbh; // Zeit + Abstand + Datum
  int blockTop    = freeTop + (h - (freeTop - y) - blockHeight) / 2;

  // Zeit
  display.setFont(&FreeSansBold18pt7b);
  display.getTextBounds(timeStr, 0, 0, &tbx, &tby, &tbw, &tbh);
  timeX = x + (w - tbw) / 2;
  int timeY = blockTop + tbh;
  display.setCursor(timeX, timeY);
  display.println(timeStr);

  // Datum
  display.setFont(&FreeSans12pt7b);
  display.getTextBounds(dateStr, 0, 0, &dbx, &dby, &dbw, &dbh);
  int dateX = x + (w - dbw) / 2;
  int dateY = timeY + 6 + dbh;
  display.setCursor(dateX, dateY);
  display.println(dateStr);
}
