#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>  // ← OTA-Updates über WLAN
#include <time.h>        // ← Für ISO 8601 Timestamps
#include "connectWiFi_hochschule.h"  // WPA2-Enterprise (eduroam), Credentials in password_hochschule.h

// Live-Dashboard (Flat-File - für Echtzeit-Anzeige)
const char* serverURL = "https://corner.klaus-klebband.ch/update_count.php";

// API-Endpoints (Datenbank-Speicherung)
const char* apiBaseURL = "https://corner.klaus-klebband.ch/api/v1";
const char* apiFlowEndpoint = "https://corner.klaus-klebband.ch/api/v1/gate/flow";
const char* apiHeartbeatEndpoint = "https://corner.klaus-klebband.ch/api/v1/device/heartbeat";

// API-Credentials
const char* apiKey = "test_key_gate_123456";
const char* deviceID = "770e8400-e29b-41d4-a716-446655440001";
const char* gateID = "660e8400-e29b-41d4-a716-446655440001";

// ==== Sensor Konfiguration ====
#define XSHUT_VL53L0X_1 2   // GPIO2 für Sensor 1
#define XSHUT_VL53L0X_2 3   // GPIO3 für Sensor 2
#define VL53L0X_ADDRESS_1 0x30
#define VL53L0X_ADDRESS_2 0x29

Adafruit_VL53L0X sensor1 = Adafruit_VL53L0X();
Adafruit_VL53L0X sensor2 = Adafruit_VL53L0X();

// ==== Zähler-Variablen ====
int count = 0;
unsigned long lastTriggerTime = 0;
unsigned long lastUploadTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long detectionStartTime = 0;

// ==== Bewegungserkennung ====
uint16_t lastRange1 = 8000;
uint16_t lastRange2 = 8000;
const int MOVEMENT_THRESHOLD = 50; // mm

// ==== Erkennungsbereich (Min/Max-Filter) ====
const int MIN_DETECTION_DISTANCE = 150;  // mm - Mindestabstand
const int MAX_DETECTION_DISTANCE = 700;  // mm - Maximalabstand

// ==== Timing-Parameter (bewährt) ====
const int debounceTime = 800;      // ms - Pause nach Erkennung
const int uploadInterval = 3000;    // ms - Periodisches Senden (Live-Dashboard)
const int heartbeatInterval = 300000; // ms - Heartbeat alle 5 Minuten
const int triggerThreshold1 = 400;  // mm - Sensor A (eingangsnah)
const int triggerThreshold2 = 400;  // mm - Sensor B (weiter gefasst)
const unsigned long maxSequenceTime = 600; // ms - Zeitfenster für Sequenz, RAUSNEHMEN/ANPASSEN

// ==== State Machine ====
enum DirectionState { IDLE, POSSIBLE_A, POSSIBLE_B };
DirectionState state = IDLE;

// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("ESP32-C6 Personenzähler");
  Serial.println("========================");

  // Sensor-Pins initialisieren
  pinMode(XSHUT_VL53L0X_1, OUTPUT);
  pinMode(XSHUT_VL53L0X_2, OUTPUT);
  
  // Beide Sensoren ausschalten
  digitalWrite(XSHUT_VL53L0X_1, LOW);
  digitalWrite(XSHUT_VL53L0X_2, LOW);
  delay(10);

  // I2C initialisieren (ESP32-C6: SDA=GPIO7, SCL=GPIO6)
  Wire.begin(7, 6);
  
  // WiFi verbinden (eduroam via WPA2-Enterprise)
  connectWiFi();

  // NTP Zeit synchronisieren (für ISO 8601 Timestamps)
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");  // GMT+1 (Schweiz)
  Serial.println("Warte auf NTP-Zeitsynchronisation...");
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 8 * 3600 * 24 && attempts < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  Serial.println("");
  if (now > 8 * 3600 * 24) {
    Serial.println("✅ NTP Zeit synchronisiert");
  } else {
    Serial.println("⚠️ NTP Timeout - Timestamps könnten ungenau sein");
  }

  // ========== OTA-Funktionalität aktivieren ==========
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname("lichtschranke_personen");  // Eindeutiger Hostname
    ArduinoOTA.setPassword("lichtschranke2025");       // Sicheres Passwort
    
    ArduinoOTA.onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("🔄 OTA Update startet: " + type);
    });
    
    ArduinoOTA.onEnd([]() {
      Serial.println("\n✅ OTA Update abgeschlossen");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Fortschritt: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("❌ OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    
    ArduinoOTA.begin();
    Serial.println("📡 OTA bereit - Hostname: lichtschranke_personen");
  }

  // Sensor 1 initialisieren
  Serial.print("Initialisiere VL53L0X Sensor 1... ");
  digitalWrite(XSHUT_VL53L0X_1, HIGH);
  delay(10);
  if (!sensor1.begin(VL53L0X_ADDRESS_1)) {
    Serial.println("FEHLER!");
    while (1);
  }
  Serial.println("OK (Adresse 0x30)");

  // Sensor 2 initialisieren  
  Serial.print("Initialisiere VL53L0X Sensor 2... ");
  digitalWrite(XSHUT_VL53L0X_2, HIGH);
  delay(10);
  if (!sensor2.begin(VL53L0X_ADDRESS_2)) {
    Serial.println("FEHLER!");
    while (1);
  }
  Serial.println("OK (Adresse 0x29)");

  Serial.println("\n✅ Personenzähler bereit!");
  Serial.println("Konfiguration:");
  Serial.println("  - Schwellenwerte: Sensor1=" + String(triggerThreshold1) + "mm, Sensor2=" + String(triggerThreshold2) + "mm");
  Serial.println("  - Bewegungsschwelle: " + String(MOVEMENT_THRESHOLD) + "mm");
  Serial.println("  - Erkennungsbereich: " + String(MIN_DETECTION_DISTANCE) + "-" + String(MAX_DETECTION_DISTANCE) + "mm");
  Serial.println("  - Max. Sequenzzeit: " + String(maxSequenceTime) + "ms");
  Serial.println("Aktuelle Personenzahl: " + String(count));
}

// ==== HAUPTSCHLEIFE ====
void loop() {
  // OTA-Updates verarbeiten (muss regelmäßig aufgerufen werden)
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
  
  // WiFi-Verbindung prüfen
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // Sensormessungen durchführen
  VL53L0X_RangingMeasurementData_t measure1, measure2;
  sensor1.rangingTest(&measure1, false);
  sensor2.rangingTest(&measure2, false);
  
  uint16_t range1 = measure1.RangeMilliMeter;
  uint16_t range2 = measure2.RangeMilliMeter;

  // Bewegungserkennung mit Min/Max-Filter
  bool range1_valid = (measure1.RangeStatus != 4 && 
                       range1 >= MIN_DETECTION_DISTANCE && 
                       range1 <= MAX_DETECTION_DISTANCE);
  
  bool range2_valid = (measure2.RangeStatus != 4 && 
                       range2 >= MIN_DETECTION_DISTANCE && 
                       range2 <= MAX_DETECTION_DISTANCE);

  bool movement1 = range1_valid && (abs((int)range1 - (int)lastRange1) > MOVEMENT_THRESHOLD);
  bool movement2 = range2_valid && (abs((int)range2 - (int)lastRange2) > MOVEMENT_THRESHOLD);

  // Trigger-Bedingungen prüfen (mit Bewegungserkennung)
  bool A_trigger = movement1 && (range1 < triggerThreshold1);
  bool B_trigger = movement2 && (range2 < triggerThreshold2);

  // Letzte Werte aktualisieren
  if (range1_valid) lastRange1 = range1;
  if (range2_valid) lastRange2 = range2;

  unsigned long now = millis();

  // State Machine (bewährte Logik)
  switch (state) {
    case IDLE:
      if (A_trigger) {
        state = POSSIBLE_A;
        lastTriggerTime = now;
        Serial.println("→ Sensor A ausgelöst (" + String(range1) + "mm | Δ=" + String(abs((int)range1 - (int)lastRange1)) + "mm)");
      } else if (B_trigger) {
        state = POSSIBLE_B;
        lastTriggerTime = now;
        Serial.println("→ Sensor B ausgelöst (" + String(range2) + "mm | Δ=" + String(abs((int)range2 - (int)lastRange2)) + "mm)");
      }
      break;

    case POSSIBLE_A:
      if (B_trigger && (now - lastTriggerTime) < maxSequenceTime) {
        // A → B Sequenz = EINTRITT
        count++;
        unsigned long durationMs = now - lastTriggerTime;
        Serial.println(">>> EINTRITT erkannt! Neue Anzahl: " + String(count));
        sendDataToServer("IN");
        sendFlowEventToAPI("IN", durationMs);  // NEU: In Datenbank speichern
        resetState();
      } else if (now - lastTriggerTime > maxSequenceTime) {
        Serial.println("→ Timeout, zurück zu IDLE");
        resetState();
      }
      break;

    case POSSIBLE_B:
      if (A_trigger && (now - lastTriggerTime) < maxSequenceTime) {
        // B → A Sequenz = AUSTRITT
        count--;
        if (count < 0) count = 0;
        unsigned long durationMs = now - lastTriggerTime;
        Serial.println(">>> AUSTRITT erkannt! Neue Anzahl: " + String(count));
        sendDataToServer("OUT");
        sendFlowEventToAPI("OUT", durationMs);  // NEU: In Datenbank speichern
        resetState();
      } else if (now - lastTriggerTime > maxSequenceTime) {
        Serial.println("→ Timeout, zurück zu IDLE");
        resetState();
      }
      break;
  }

  // Periodisches Senden (Status-Update für Live-Dashboard)
  if (now - lastUploadTime > uploadInterval) {
    sendDataToServer("IDLE");
    lastUploadTime = now;
  }

  // Heartbeat alle 5 Minuten an API senden
  if (now - lastHeartbeatTime > heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeatTime = now;
  }

  delay(50);
}

// ==== HILFSFUNKTIONEN ====

void resetState() {
  state = IDLE;
  lastRange1 = 8000;
  lastRange2 = 8000;
  delay(debounceTime);
}

// Live-Dashboard Update (Flat-File - wie bisher)
void sendDataToServer(String direction) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Keine WiFi-Verbindung - Senden übersprungen");
    return;
  }

  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Serial.println("❌ Konnte WiFiClientSecure nicht erstellen");
    return;
  }
  
  client->setInsecure();
  
  HTTPClient http;
  http.begin(*client, serverURL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setTimeout(5000);
  
  String postData = "count=" + String(count) + "&direction=" + direction;
  
  Serial.print("📤 Live-Dashboard: " + postData + " ... ");
  
  int httpResponseCode = http.POST(postData);
  
  if (httpResponseCode > 0) {
    Serial.println("✅ (" + String(httpResponseCode) + ")");
  } else {
    Serial.println("❌ (" + String(httpResponseCode) + ")");
  }
  
  http.end();
  delete client;
}

// Hilfsfunktion: ISO 8601 Timestamp generieren
String getISO8601Timestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (!gmtime_r(&now, &timeinfo)) {
    return "";  // Fehler
  }
  
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", &timeinfo);
  return String(buffer);
}

// NEU: Flow-Event in Datenbank speichern
void sendFlowEventToAPI(String direction, unsigned long durationMs) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Keine WiFi - API-Event übersprungen");
    return;
  }

  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) return;
  
  client->setInsecure();
  
  HTTPClient http;
  http.begin(*client, apiFlowEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", apiKey);
  http.setTimeout(5000);
  
  // ISO 8601 Timestamp generieren
  String timestamp = getISO8601Timestamp();
  if (timestamp == "") {
    Serial.println("❌ Timestamp-Fehler - Event übersprungen");
    delete client;
    return;
  }
  
  // JSON-Payload erstellen
  String jsonPayload = "{";
  jsonPayload += "\"gate_id\":\"" + String(gateID) + "\",";
  jsonPayload += "\"timestamp\":\"" + timestamp + "\",";
  jsonPayload += "\"direction\":\"" + direction + "\",";
  jsonPayload += "\"confidence\":0.95,";
  jsonPayload += "\"duration_ms\":" + String(durationMs);
  jsonPayload += "}";
  
  Serial.print("💾 API Flow-Event: " + direction + " ... ");
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.println("✅ DB gespeichert (" + String(httpResponseCode) + ")");
    if (httpResponseCode == 201 || httpResponseCode == 200) {
      String response = http.getString();
      Serial.println("📥 API: " + response);
    }
  } else {
    Serial.println("❌ DB-Fehler (" + String(httpResponseCode) + ")");
  }
  
  http.end();
  delete client;
}

// NEU: Heartbeat an API senden
void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Keine WiFi - Heartbeat übersprungen");
    return;
  }

  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) return;
  
  client->setInsecure();
  
  HTTPClient http;
  http.begin(*client, apiHeartbeatEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", apiKey);
  http.setTimeout(5000);
  
  // ISO 8601 Timestamp generieren
  String timestamp = getISO8601Timestamp();
  if (timestamp == "") {
    Serial.println("❌ Timestamp-Fehler - Heartbeat übersprungen");
    delete client;
    return;
  }
  
  // JSON-Payload erstellen
  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"" + String(deviceID) + "\",";
  jsonPayload += "\"timestamp\":\"" + timestamp + "\",";
  jsonPayload += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  jsonPayload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  jsonPayload += "\"metrics\":{";
  jsonPayload += "\"uptime_seconds\":" + String(millis() / 1000) + ",";
  jsonPayload += "\"free_memory_kb\":" + String(ESP.getFreeHeap() / 1024);
  jsonPayload += "}}";
  
  Serial.print("💓 Heartbeat ... ");
  
  int httpResponseCode = http.POST(jsonPayload);
  
  if (httpResponseCode > 0) {
    Serial.println("✅ (" + String(httpResponseCode) + ")");
  } else {
    Serial.println("❌ (" + String(httpResponseCode) + ")");
  }
  
  http.end();
  delete client;
}