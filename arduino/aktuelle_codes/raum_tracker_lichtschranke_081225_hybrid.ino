/*
 * ESP32-C6 Personenzähler mit Drift-Korrektur (HYBRID-VERSION)
 * 
 * Basiert auf funktionierendem Code vom 27.11.2025
 * + JSON-Parsing für Drift-Korrektur vom 06.12.2025
 * 
 * Version: 2.1 Hybrid
 * Datum: 8. Dezember 2025
 * 
 * Hardware: ESP32-C6-N8 mit 3x Lichtschranken (VL53L0X + VL6180X)
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

const int MIN_DETECTION_DISTANCE = 50;
const int MAX_DETECTION_DISTANCE = 1200;

const int debounceTime = 800;
const int uploadInterval = 500;
const int heartbeatInterval = 300000;
const int triggerThreshold1 = 950;
const int triggerThreshold2 = 950;
const int triggerThresholdMiddle = 950;
const unsigned long maxSequenceTime = 1000;

enum DirectionState { IDLE, POSSIBLE_A, MIDDLE_CONFIRM_IN, MIDDLE_CONFIRM_OUT, POSSIBLE_B };
DirectionState state = IDLE;

bool pendingSend = false;
String pendingDirection = "";
unsigned long pendingDuration = 0;

// ===== JSON-PARSING FUNKTION (NEU) =====
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

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("ESP32-C6 Personenzähler mit Drift-Korrektur (Hybrid v2.1)");
  Serial.println("=========================================================");
  
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
  
  uint16_t range1 = 0;
  uint16_t range2 = 0;
  uint8_t rangeMiddle = 0;
  
  if (sensor1.isRangeComplete()) {
    range1 = sensor1.readRangeResult();
  }
  
  if (sensor2.isRangeComplete()) {
    range2 = sensor2.readRangeResult();
  }
  
  rangeMiddle = sensorMiddle.readRange();
  
  bool triggerA = (range1 < triggerThreshold1 && range1 >= MIN_DETECTION_DISTANCE);
  bool triggerMiddle = (rangeMiddle < triggerThresholdMiddle && rangeMiddle >= MIN_DETECTION_DISTANCE);
  bool triggerB = (range2 < triggerThreshold2 && range2 >= MIN_DETECTION_DISTANCE);
  
  // DEBUG: Zeige Sensor-Werte alle 3 Sekunden
  static unsigned long lastDebugPrint = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastDebugPrint > 3000) {
    Serial.printf("📏 A=%dmm M=%dmm B=%dmm | State=%d Count=%d\n", 
                  range1, rangeMiddle, range2, state, count);
    lastDebugPrint = currentTime;
  }
  
  // State Machine (ORIGINAL LOGIK vom funktionierenden Code)
  switch (state) {
    case IDLE:
      if (triggerA && !triggerMiddle && !triggerB) {
        state = POSSIBLE_A;
        lastTriggerTime = currentTime;
        Serial.println("🔵 Sensor A → Start Eingang-Sequenz");
      } else if (triggerB && !triggerMiddle && !triggerA) {
        state = POSSIBLE_B;
        lastTriggerTime = currentTime;
        Serial.println("🔵 Sensor B → Start Ausgang-Sequenz");
      }
      break;
      
    case POSSIBLE_A:
      if (triggerMiddle) {
        state = MIDDLE_CONFIRM_IN;
        lastTriggerTime = currentTime;
      } else if (currentTime - lastTriggerTime > maxSequenceTime) {
        state = IDLE;
      }
      break;
      
    case MIDDLE_CONFIRM_IN:
      if (triggerB && !triggerMiddle && (currentTime - lastTriggerTime < maxSequenceTime)) {
        unsigned long durationMs = currentTime - lastTriggerTime;
        count++;
        Serial.println("\n🚶 EINGANG (A→M→B) | Count: " + String(count) + " | " + String(durationMs) + "ms");
        
        pendingSend = true;
        pendingDirection = "IN";
        pendingDuration = durationMs;
        
        state = IDLE;
        lastUploadTime = currentTime;
      } else if (currentTime - lastTriggerTime > maxSequenceTime) {
        state = IDLE;
      }
      break;
      
    case POSSIBLE_B:
      if (triggerMiddle) {
        state = MIDDLE_CONFIRM_OUT;
        lastTriggerTime = currentTime;
      } else if (currentTime - lastTriggerTime > maxSequenceTime) {
        state = IDLE;
      }
      break;
      
    case MIDDLE_CONFIRM_OUT:
      if (triggerA && !triggerMiddle && (currentTime - lastTriggerTime < maxSequenceTime)) {
        unsigned long durationMs = currentTime - lastTriggerTime;
        count--;
        Serial.println("\n🚪 AUSGANG (B→M→A) | Count: " + String(count) + " | " + String(durationMs) + "ms");
        
        pendingSend = true;
        pendingDirection = "OUT";
        pendingDuration = durationMs;
        
        state = IDLE;
        lastUploadTime = currentTime;
      } else if (currentTime - lastTriggerTime > maxSequenceTime) {
        state = IDLE;
      }
      break;
  }
  
  if (pendingSend && (currentTime - lastUploadTime > uploadInterval)) {
    sendToUpdate(pendingDirection, pendingDuration);
    sendFlowEventToAPIAsync(pendingDirection, pendingDuration);
    pendingSend = false;
  }
  
  if (currentTime - lastHeartbeatTime > heartbeatInterval) {
    sendHeartbeatAsync();
    lastHeartbeatTime = currentTime;
  }
  
  delay(10);
}
