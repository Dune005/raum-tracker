/*
 * ESP32-C6 Personenzähler mit Drift-Korrektur (HYBRID-VERSION v2.2)
 * 
 * Basiert auf funktionierendem Code vom 27.11.2025
 * + JSON-Parsing für Drift-Korrektur vom 06.12.2025
 * + Korrekturen für WiFi-Stabilität und Online-Status
 * 
 * Version: 2.2 Hybrid - STABLE
 * Datum: 9. Dezember 2025
 * 
 * Hardware: ESP32-C6-N8 mit 3x Lichtschranken (VL53L0X + VL6180X)
 * 
 * Änderungen gegenüber v2.1:
 * - resetState() überall korrekt verwendet (WiFi-Stack braucht Pausen!)
 * - Regelmäßige IDLE-Updates alle 500ms (damit Dashboard Online-Status sieht)
 * - State Machine robuster gegen schnelle Durchläufe
 */

#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_VL6180X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <ArduinoJson.h>
#include "connectWiFi_hochschule.h"

const char* serverURL = "https://corner.klaus-klebband.ch/update_count.php";
const char* apiBaseURL = "https://corner.klaus-klebband.ch/api/v1";
const char* apiFlowEndpoint = "https://corner.klaus-klebband.ch/api/v1/gate/flow";
const char* apiHeartbeatEndpoint = "https://corner.klaus-klebband.ch/api/v1/device/heartbeat";

const char* apiKey = "test_key_gate_123456";
const char* deviceID = "770e8400-e29b-41d4-a716-446655440001";
const char* gateID = "660e8400-e29b-41d4-a716-446655440001";

#define XSHUT_VL53L0X_1 2
#define XSHUT_VL53L0X_2 3
#define XSHUT_VL6180X_MIDDLE 11

#define VL53L0X_ADDRESS_1 0x30
#define VL53L0X_ADDRESS_2 0x31

Adafruit_VL53L0X sensor1 = Adafruit_VL53L0X();
Adafruit_VL53L0X sensor2 = Adafruit_VL53L0X();
Adafruit_VL6180X sensorMiddle = Adafruit_VL6180X();

int count = 0;
unsigned long lastTriggerTime = 0;
unsigned long lastUploadTime = 0;
unsigned long lastHeartbeatTime = 0;
int lastResetDay = -1;  // Track letzter Reset-Tag (tm_mday)

const int MIN_DETECTION_DISTANCE = 50;
const int MAX_DETECTION_DISTANCE = 1200;

const int debounceTime = 800;
const int uploadInterval = 500;
const int heartbeatInterval = 300000;
const int triggerThreshold1 = 950;
const int triggerThreshold2 = 950;
const int triggerThresholdMiddle = 150;  // VL6180X max. 150mm!
const unsigned long maxSequenceTime = 1000;  // Reduziert für weniger Fehlauslösungen

enum DirectionState { IDLE, POSSIBLE_A, MIDDLE_CONFIRM, POSSIBLE_B };
DirectionState state = IDLE;

bool pendingSend = false;
String pendingDirection = "";
unsigned long pendingDuration = 0;

bool resetSent = false;  // Flag: Reset-Signal wurde gesendet

// ===== RESET STATE MIT DEBOUNCE (KRITISCH FÜR WIFI-STACK!) =====
void resetState() {
  state = IDLE;
  delay(debounceTime);  // 800ms Pause - gibt WiFi-Stack Zeit für Hintergrundaufgaben!
}

// ===== QUEUE SEND (wie im 27.11er Code) =====
void queueSend(String direction, unsigned long durationMs) {
  pendingSend = true;
  pendingDirection = direction;
  pendingDuration = durationMs;
}

// ===== JSON-PARSING FUNKTION (MIT DRIFT-KORREKTUR) =====
void sendToUpdate(String direction, unsigned long durationMs) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Update: Keine WiFi-Verbindung");
    return;
  }
  
  Serial.print("📤 Sende an Dashboard... count=" + String(count) + " direction=" + direction + " ... ");
  
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Serial.println("❌ Client-Erstellung fehlgeschlagen");
    return;
  }
  
  client->setInsecure();
  
  HTTPClient http;
  http.begin(*client, serverURL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setTimeout(2000);
  
  String postData = "count=" + String(count) + "&direction=" + direction;
  
  unsigned long startTime = millis();
  int httpResponseCode = http.POST(postData);
  unsigned long duration = millis() - startTime;
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("✅ " + String(httpResponseCode) + " (" + String(duration) + "ms)");
    
    // JSON-Response parsen (optional - fällt nicht ab wenn es nicht funktioniert)
    if (response.length() > 0 && response.length() < 1000) {
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, response);
      
      if (!error) {
        // Prüfe drift_corrected Flag
        if (doc.containsKey("drift_corrected")) {
          bool driftCorrected = doc["drift_corrected"];
          
          if (driftCorrected) {
            int oldCount = count;
            count = 0;
            Serial.println("   🔄 DRIFT-KORREKTUR: Counter " + String(oldCount) + " → 0");
          } else {
            Serial.println("   ✓ Server-Count: " + String(doc["count"].as<int>()));
          }
        }
        
        // Optional: Display-Count
        if (doc.containsKey("display_count")) {
          Serial.println("   ℹ️ Display-Count: " + String(doc["display_count"].as<int>()));
        }
      } else {
        // Kein JSON oder Parse-Fehler - nicht kritisch!
        Serial.println("   ⚠️ Keine JSON-Response (Fallback-Modus)");
      }
    }
  } else {
    Serial.println("❌ Fehler: " + String(httpResponseCode) + " (" + String(duration) + "ms)");
  }
  
  http.end();
  delete client;
}

String getISO8601Timestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (!gmtime_r(&now, &timeinfo)) return "";
  
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", &timeinfo);
  return String(buffer);
}

void sendFlowEventToAPIAsync(String direction, unsigned long durationMs) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ API: Keine WiFi-Verbindung");
    return;
  }
  
  Serial.print("💾 Sende an API... direction=" + direction + " duration=" + String(durationMs) + "ms ... ");
  
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Serial.println("❌ Client-Erstellung fehlgeschlagen");
    return;
  }
  
  client->setInsecure();
  
  HTTPClient http;
  http.begin(*client, apiFlowEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", apiKey);
  http.setTimeout(2000);
  
  String timestamp = getISO8601Timestamp();
  if (timestamp == "") {
    Serial.println("❌ Timestamp konnte nicht generiert werden");
    delete client;
    return;
  }
  
  String jsonPayload = "{";
  jsonPayload += "\"gate_id\":\"" + String(gateID) + "\",";
  jsonPayload += "\"timestamp\":\"" + timestamp + "\",";
  jsonPayload += "\"direction\":\"" + direction + "\",";
  jsonPayload += "\"confidence\":0.95,";
  jsonPayload += "\"duration_ms\":" + String(durationMs);
  jsonPayload += "}";
  
  unsigned long startTime = millis();
  int httpResponseCode = http.POST(jsonPayload);
  unsigned long duration = millis() - startTime;
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("✅ " + String(httpResponseCode) + " (" + String(duration) + "ms)");
  } else {
    Serial.println("❌ Fehler: " + String(httpResponseCode));
  }
  
  http.end();
  delete client;
}

void sendHeartbeatAsync() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Heartbeat: Keine WiFi-Verbindung");
    return;
  }
  
  Serial.print("💓 Sende Heartbeat... ");
  
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Serial.println("❌ Client-Erstellung fehlgeschlagen");
    return;
  }
  
  client->setInsecure();
  
  HTTPClient http;
  http.begin(*client, apiHeartbeatEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", apiKey);
  http.setTimeout(2000);
  
  String timestamp = getISO8601Timestamp();
  if (timestamp == "") {
    Serial.println("❌ Timestamp konnte nicht generiert werden");
    delete client;
    return;
  }
  
  String jsonPayload = "{";
  jsonPayload += "\"device_id\":\"" + String(deviceID) + "\",";
  jsonPayload += "\"timestamp\":\"" + timestamp + "\",";
  jsonPayload += "\"ip_address\":\"" + WiFi.localIP().toString() + "\",";
  jsonPayload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  jsonPayload += "\"metrics\":{";
  jsonPayload += "\"uptime_seconds\":" + String(millis() / 1000) + ",";
  jsonPayload += "\"free_heap\":" + String(ESP.getFreeHeap());
  jsonPayload += "}}";
  
  unsigned long startTime = millis();
  int httpResponseCode = http.POST(jsonPayload);
  unsigned long duration = millis() - startTime;
  
  if (httpResponseCode > 0) {
    Serial.println("✅ " + String(httpResponseCode) + " (" + String(duration) + "ms)");
  } else {
    Serial.println("❌ Fehler: " + String(httpResponseCode));
  }
  
  http.end();
  delete client;
}

void sendResetSignal() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Reset: Keine WiFi-Verbindung");
    return;
  }
  
  Serial.print("🔄 Sende Reset-Signal an Server... ");
  
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) {
    Serial.println("❌ Client-Erstellung fehlgeschlagen");
    return;
  }
  
  client->setInsecure();
  
  HTTPClient http;
  http.begin(*client, serverURL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setTimeout(2000);
  
  String postData = "count=0&direction=RESET&device_reset=true";
  
  unsigned long startTime = millis();
  int httpResponseCode = http.POST(postData);
  unsigned long duration = millis() - startTime;
  
  if (httpResponseCode > 0) {
    Serial.println("✅ " + String(httpResponseCode) + " (" + String(duration) + "ms)");
  } else {
    Serial.println("❌ Fehler: " + String(httpResponseCode));
  }
  
  http.end();
  delete client;
}

void performDailyReset() {
  Serial.println("\n⏰ 8:00 Uhr erreicht - Starte täglichen Reset...");
  
  // Lokalen Counter zurücksetzen
  int oldCount = count;
  count = 0;
  Serial.println("  📊 Arduino Counter: " + String(oldCount) + " → 0");
  
  // Reset-Signal an Server senden (3x für Sicherheit)
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.print("  Versuch " + String(attempt) + "/3: ");
    sendResetSignal();
    if (attempt < 3) delay(1000);
  }
  
  Serial.println("✅ Täglicher Reset abgeschlossen\n");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("ESP32-C6 Personenzähler mit Drift-Korrektur (Hybrid v2.2 STABLE)");
  Serial.println("===================================================================");
  
  pinMode(XSHUT_VL53L0X_1, OUTPUT);
  pinMode(XSHUT_VL53L0X_2, OUTPUT);
  pinMode(XSHUT_VL6180X_MIDDLE, OUTPUT);
  
  digitalWrite(XSHUT_VL53L0X_1, LOW);
  digitalWrite(XSHUT_VL53L0X_2, LOW);
  digitalWrite(XSHUT_VL6180X_MIDDLE, LOW);
  delay(10);
  
  Wire.begin(7, 6);
  Wire.setClock(400000);
  
  connectWiFi();
  
  Serial.println("ℹ️ Reset-Signal wird in loop() gesendet (wenn WiFi stabil)");
  
  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
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
  
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname("lichtschranke_personen");
    ArduinoOTA.setPassword("lichtschranke2025");
    
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
  
  Serial.print("Initialisiere VL53L0X Sensor 1 (Eingang)... ");
  digitalWrite(XSHUT_VL53L0X_1, HIGH);
  delay(10);
  if (!sensor1.begin(VL53L0X_ADDRESS_1)) {
    Serial.println("FEHLER!");
    while (1);
  }
  sensor1.configSensor(Adafruit_VL53L0X::VL53L0X_SENSE_HIGH_SPEED);
  sensor1.setMeasurementTimingBudgetMicroSeconds(33000);
  sensor1.startRangeContinuous(33);
  Serial.println("OK (Adresse 0x30)");
  
  Serial.print("Initialisiere VL53L0X Sensor 2 (Ausgang)... ");
  digitalWrite(XSHUT_VL53L0X_2, HIGH);
  delay(10);
  if (!sensor2.begin(VL53L0X_ADDRESS_2)) {
    Serial.println("FEHLER!");
    while (1);
  }
  sensor2.configSensor(Adafruit_VL53L0X::VL53L0X_SENSE_HIGH_SPEED);
  sensor2.setMeasurementTimingBudgetMicroSeconds(33000);
  sensor2.startRangeContinuous(33);
  Serial.println("OK (Adresse 0x31)");
  
  Serial.print("Initialisiere VL6180X Sensor Middle... ");
  digitalWrite(XSHUT_VL6180X_MIDDLE, HIGH);
  delay(10);
  if (!sensorMiddle.begin()) {
    Serial.println("FEHLER!");
    while (1);
  }
  Serial.println("OK (Adresse 0x29)");
  
  delay(1000);
  Serial.println("\n✅ System bereit - starte Messung...\n");
}

void loop() {
  ArduinoOTA.handle();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi-Verbindung verloren - versuche Reconnect...");
    connectWiFi();
    delay(5000);
    return;
  }
  
  unsigned long currentTime = millis();
  
  // ===== ZEITFENSTER-BLOCKIERUNG (23:00-06:00 UHR) =====
  // Verhindert nächtliche Fehlauslösungen durch empfindliche IR-Sensoren
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int hour = timeinfo.tm_hour;
    int minute = timeinfo.tm_min;
    int currentDay = timeinfo.tm_mday;
    
    // Debug: Zeige aktuelle Zeit alle 30 Sekunden
    static unsigned long lastTimeDebug = 0;
    if (currentTime - lastTimeDebug > 30000) {
      time_t now = time(nullptr);
      Serial.println("⏰ Aktuelle Zeit: " + String(hour) + ":" + String(minute) + " Uhr (Tag: " + String(currentDay) + ", Timestamp: " + String(now) + ")");
      lastTimeDebug = currentTime;
    }
    
    // ===== TÄGLICHER RESET UM 8:00 UHR =====
    time_t now = time(nullptr);
    if (now > 1577836800) {  // NTP synchronisiert
      if (hour == 8 && minute == 0 && currentDay != lastResetDay) {
        performDailyReset();
        lastResetDay = currentDay;  // Markiere Tag als resettet
      }
    }
    
    // NTP muss synchronisiert sein (Timestamp > 01.01.2020)
    if (now > 1577836800 && (hour >= 23 || hour < 6)) {
      // Nachts: Nur Heartbeat senden, keine Messungen
      static unsigned long lastNightHeartbeat = 0;
      if (currentTime - lastNightHeartbeat > heartbeatInterval) {
        Serial.println("🌙 Nachtmodus aktiv (" + String(hour) + ":" + String(minute) + " Uhr) - Sensoren deaktiviert");
        sendHeartbeatAsync();
        lastNightHeartbeat = currentTime;
      }
      delay(5000);
      return;
    }
  } else {
    // Warnung wenn getLocalTime fehlschlägt
    static unsigned long lastTimeWarning = 0;
    if (currentTime - lastTimeWarning > 60000) {
      Serial.println("⚠️ getLocalTime() fehlgeschlagen - Zeitfenster-Blockierung inaktiv");
      lastTimeWarning = currentTime;
    }
  }
  
  // ===== RESET-SIGNAL BEIM ERSTEN LOOP (WENN WIFI STABIL) =====
  if (!resetSent && currentTime > 5000) {
    Serial.println("\n🔄 WiFi stabil - sende Reset-Signal an Server (3x)...");
    for (int attempt = 1; attempt <= 3; attempt++) {
      Serial.print("  Versuch " + String(attempt) + "/3: ");
      sendResetSignal();
      delay(1000);
    }
    resetSent = true;
    Serial.println("✅ Reset-Sequenz abgeschlossen\n");
  }
  
  // ===== SENSOR-VARIABLEN (werden in den einzelnen Checks befüllt) =====
  uint16_t range1 = 0;
  uint16_t range2 = 0;
  uint8_t rangeMiddle = 0;
  
  // DEBUG: Zeige Sensor-Werte alle 5 Sekunden
  static unsigned long lastDebugPrint = 0;
  if (currentTime - lastDebugPrint > 5000) {
    Serial.println("===== Status =====");
    Serial.println("WiFi: " + String(WiFi.status() == WL_CONNECTED ? "✅" : "❌") + " (RSSI: " + String(WiFi.RSSI()) + " dBm)");
    Serial.println("Count: " + String(count));
    Serial.println("State: " + String(state == IDLE ? "IDLE" : (state == POSSIBLE_A ? "POSSIBLE_A" : (state == MIDDLE_CONFIRM ? "MIDDLE_CONFIRM" : "POSSIBLE_B"))));
    Serial.println("RAM frei: " + String(ESP.getFreeHeap() / 1024) + " KB");
    Serial.println("==================");
    lastDebugPrint = currentTime;
  }
  
  // ===== STATE MACHINE (27.11er BEWÄHRTE LOGIK!) =====
  // Sensor A - Auswertung
  if (sensor1.isRangeComplete()) {
    range1 = sensor1.readRangeResult();
    bool range1_valid = (range1 >= MIN_DETECTION_DISTANCE && range1 <= MAX_DETECTION_DISTANCE);
    bool A_trigger = range1_valid && (range1 < triggerThreshold1);
    
    if (state == IDLE && A_trigger) {
      state = POSSIBLE_A;
      lastTriggerTime = currentTime;
      Serial.println("→ Sensor A: " + String(range1) + "mm");
    } 
    else if (state == MIDDLE_CONFIRM && A_trigger && (currentTime - lastTriggerTime) < maxSequenceTime) {
      unsigned long durationMs = currentTime - lastTriggerTime;
      count--;
      if (count < 0) count = 0;
      Serial.println("\n🚪 AUSGANG (B→M→A) | Count: " + String(count) + " | " + String(durationMs) + "ms");
      queueSend("OUT", durationMs);
      resetState();
    }
    else if (state == POSSIBLE_B && A_trigger && (currentTime - lastTriggerTime) < maxSequenceTime) {
      unsigned long durationMs = currentTime - lastTriggerTime;
      count--;
      if (count < 0) count = 0;
      Serial.println("\n🚪 AUSGANG (B→A direkt) | Count: " + String(count) + " | " + String(durationMs) + "ms");
      queueSend("OUT", durationMs);
      resetState();
    } 
    else if ((state == POSSIBLE_A || state == MIDDLE_CONFIRM || state == POSSIBLE_B) && (currentTime - lastTriggerTime) > maxSequenceTime) {
      Serial.println("→ Timeout State=" + String(state));
      resetState();
    }
  }
  
  // Sensor Middle - Auswertung
  rangeMiddle = sensorMiddle.readRange();
  bool rangeMiddle_valid = (rangeMiddle >= MIN_DETECTION_DISTANCE && rangeMiddle <= MAX_DETECTION_DISTANCE);
  bool Middle_trigger = rangeMiddle_valid && (rangeMiddle < triggerThresholdMiddle);
  
  if (state == POSSIBLE_A && Middle_trigger && (currentTime - lastTriggerTime) < maxSequenceTime) {
    state = MIDDLE_CONFIRM;
    Serial.println("  ✓ Middle: " + String(rangeMiddle) + "mm - Bestätigung");
  } 
  else if (state == POSSIBLE_B && Middle_trigger && (currentTime - lastTriggerTime) < maxSequenceTime) {
    state = MIDDLE_CONFIRM;
    Serial.println("  ✓ Middle: " + String(rangeMiddle) + "mm - Bestätigung");
  }
  
  // Sensor B - Auswertung
  if (sensor2.isRangeComplete()) {
    range2 = sensor2.readRangeResult();
    bool range2_valid = (range2 >= MIN_DETECTION_DISTANCE && range2 <= MAX_DETECTION_DISTANCE);
    bool B_trigger = range2_valid && (range2 < triggerThreshold2);
    
    if (state == IDLE && B_trigger) {
      state = POSSIBLE_B;
      lastTriggerTime = currentTime;
      Serial.println("→ Sensor B: " + String(range2) + "mm");
    } 
    else if (state == MIDDLE_CONFIRM && B_trigger && (currentTime - lastTriggerTime) < maxSequenceTime) {
      unsigned long durationMs = currentTime - lastTriggerTime;
      count++;
      Serial.println("\n🚶 EINGANG (A→M→B) | Count: " + String(count) + " | " + String(durationMs) + "ms");
      queueSend("IN", durationMs);
      resetState();
    }
    else if (state == POSSIBLE_A && B_trigger && (currentTime - lastTriggerTime) < maxSequenceTime) {
      unsigned long durationMs = currentTime - lastTriggerTime;
      count++;
      Serial.println("\n🚶 EINGANG (A→B direkt) | Count: " + String(count) + " | " + String(durationMs) + "ms");
      queueSend("IN", durationMs);
      resetState();
    } 
    else if ((state == POSSIBLE_A || state == MIDDLE_CONFIRM || state == POSSIBLE_B) && (currentTime - lastTriggerTime) > maxSequenceTime) {
      Serial.println("→ Timeout State=" + String(state));
      resetState();
    }
  }
  
  // ===== SENDE EVENTS (BEI IN/OUT) =====
  if (pendingSend) {
    sendToUpdate(pendingDirection, pendingDuration);
    sendFlowEventToAPIAsync(pendingDirection, pendingDuration);
    pendingSend = false;
  }
  
  // ===== REGELMÄSSIGE IDLE-UPDATES (ALLE 500ms) =====
  // **KRITISCH: Damit Dashboard weiß, dass Controller online ist!**
  if (currentTime - lastUploadTime > uploadInterval) {
    sendToUpdate("IDLE", 0);
    lastUploadTime = currentTime;
  }
  
  // ===== HEARTBEAT (ALLE 5 MINUTEN) =====
  if (currentTime - lastHeartbeatTime > heartbeatInterval) {
    sendHeartbeatAsync();
    lastHeartbeatTime = currentTime;
  }
  
  // Kurze Pause für WiFi-Stack und Sensor-Stabilität
  delay(50);
}