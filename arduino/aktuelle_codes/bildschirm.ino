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
const char* PASSWORD = "0jgp-42ej-ah8y-hnwz";  // Öffentliches Netz

// ===== API Konfiguration =====
const char* apiBaseURL = "https://corner.klaus-klebband.ch/api/v1";
const char* apiFlowEndpoint = "https://corner.klaus-klebband.ch/api/v1/gate/flow";
const char* apiKey = "test_key_gate_123456";
const char* deviceID = "770e8400-e29b-41d4-a716-446655440001";
const char* gateID = "660e8400-e29b-41d4-a716-446655440001";

// Daten-Struktur für Live-Daten vom Server
struct LiveData {
  int personenanzahl;
  int lautstaerke_db;
  String stosszeit;
};

LiveData currentData = {
  .personenanzahl = 0,
  .lautstaerke_db = 0,
  .stosszeit = "--:--"
};

// Zeit-Variablen
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
const unsigned long UPDATE_INTERVAL = 30000;  // 30 Sekunden

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== Raumtracker Display Start ===");
  
  display.init(115200, true, 2, false);
  
  // WiFi verbinden
  connectToWiFi();
  
  // NTP-Zeit synchronisieren
  syncTime();
  
  // Erste Daten abrufen
  fetchLiveData();
  updateDisplay();
}

void loop() {
  unsigned long currentTime = millis();
  
  // Update Zeit
  incrementTime();
  
  // Regelmäßig Live-Daten abrufen
  if (currentTime - lastUpdateTime >= UPDATE_INTERVAL) {
    lastUpdateTime = currentTime;
    
    Serial.println("Fetching live data...");
    if (fetchLiveData()) {
      updateDisplay();
    } else {
      Serial.println("Fehler beim Abrufen der Daten");
    }
  }
  
  delay(5000);  // 5 Sekunden Check-Intervall
}

// ===== WiFi Funktionen =====
void connectToWiFi() {
  Serial.print("Verbinde mit WiFi: ");
  Serial.println(SSID);
  
  WiFi.begin(SSID, PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi verbunden!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ WiFi Verbindung fehlgeschlagen!");
  }
}

// ===== NTP Zeit Synchronisierung =====
void syncTime() {
  configTime(1 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  
  Serial.print("Synchronisiere Zeit mit NTP...");
  time_t now = time(nullptr);
  int attempts = 0;
  
  while (now < 24 * 3600 && attempts < 20) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  
  if (now > 24 * 3600) {
    Serial.println(" ✓");
    updateTimeFromSystem();
  } else {
    Serial.println(" ✗ (verwendet Fallback-Zeit)");
  }
}

void updateTimeFromSystem() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  
  currentYear = 1900 + timeinfo->tm_year;
  currentMonth = 1 + timeinfo->tm_mon;
  currentDay = timeinfo->tm_mday;
  currentHour = timeinfo->tm_hour;
  currentMinute = timeinfo->tm_min;
}

void incrementTime() {
  static unsigned long lastIncrement = 0;
  unsigned long now = millis();
  
  if (now - lastIncrement >= 60000) {  // Jede Minute
    lastIncrement = now;
    currentMinute++;
    
    if (currentMinute >= 60) {
      currentMinute = 0;
      currentHour++;
      
      if (currentHour >= 24) {
        currentHour = 0;
        currentDay++;
        
        if (currentDay > 31) {  // Vereinfachte Prüfung
          currentDay = 1;
          currentMonth++;
          
          if (currentMonth > 12) {
            currentMonth = 1;
            currentYear++;
          }
        }
      }
    }
  }
}

// ===== API Anfragen =====
bool fetchLiveData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi nicht verbunden");
    return false;
  }
  
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  
  // Header vorbereiten
  String url = String(apiFlowEndpoint) + "?gateID=" + gateID;
  
  http.begin(url);
  http.addHeader("Authorization", String("Bearer ") + apiKey);
  http.addHeader("Content-Type", "application/json");
  
  Serial.print("GET: ");
  Serial.println(url);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("Response erhalten, Größe: " + String(payload.length()));
    
    if (parseFlowData(payload)) {
      http.end();
      return true;
    }
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
    
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("Response: " + payload);
    }
  }
  
  http.end();
  return false;
}

bool parseFlowData(String jsonString) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, jsonString);
  
  if (error) {
    Serial.print("JSON Parse Error: ");
    Serial.println(error.c_str());
    return false;
  }
  
  // Beispiel-Struktur: anpassen je nach API-Antwort
  if (doc.containsKey("data")) {
    JsonObject data = doc["data"];
    
    if (data.containsKey("current_occupancy")) {
      currentData.personenanzahl = data["current_occupancy"];
      Serial.print("✓ Personen: ");
      Serial.println(currentData.personenanzahl);
    }
    
    if (data.containsKey("noise_level")) {
      currentData.lautstaerke_db = data["noise_level"];
      Serial.print("✓ Lautstärke: ");
      Serial.print(currentData.lautstaerke_db);
      Serial.println(" dB");
    }
    
    if (data.containsKey("peak_time")) {
      currentData.stosszeit = data["peak_time"].as<String>();
      Serial.print("✓ Stoßzeit: ");
      Serial.println(currentData.stosszeit);
    }
  }
  
  return true;
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
  String value = "~" + String(persons);
  
  int16_t tbx, tby;
  uint16_t tbw, tbh;
  display.getTextBounds(value, 0, 0, &tbx, &tby, &tbw, &tbh);
  
  int textX = x + (width - tbw) / 2;
  int textY = y + height - 40;
  
  display.setCursor(textX, textY);
  display.println(value);
}

void drawAuslastungQuadrant(int x, int y, int width, int height) {
  int persons = currentData.personenanzahl;
  
  String category;
  uint16_t bgColor;
  uint16_t textColor;
  
  if (persons < 6) {
    category = "niedrig";
    bgColor = GxEPD_WHITE;
    textColor = GxEPD_BLACK;
  } else if (persons < 20) {
    category = "mittel";
    bgColor = GxEPD_YELLOW;
    textColor = GxEPD_BLACK;
  } else {
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
  display.println("LAUTSTARKE");
  
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
  if (db < 50) {
    return "leise";
  } else if (db < 65) {
    return "mittel";
  } else if (db < 80) {
    return "laut";
  } else {
    return "sehr laut";
  }
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