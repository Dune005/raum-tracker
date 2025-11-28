#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "connectWiFi_zuhause.h"

// ===== SERVER KONFIGURATION =====
const char* serverURL = "https://corner.klaus-klebband.ch/update_count.php";
const char* apiSensorEndpoint = "https://corner.klaus-klebband.ch/api/v1/sensor/reading";
const char* apiHeartbeatEndpoint = "https://corner.klaus-klebband.ch/api/v1/device/heartbeat";

const char* apiKey = "test_key_audio_789012";
const char* deviceID = "770e8400-e29b-41d4-a716-446655440002";
const char* sensorID = "550e8400-e29b-41d4-a716-446655440003";

// ===== TEST-SIMULATION Parameter =====
unsigned long testCycleStart = 0;
bool testRunning = true;
float testCurrentDB = 30.0;
const float TEST_MAX_DB = 80.0;
const float TEST_STEP_DB = 5.0;
const unsigned long TEST_RAMP_DURATION = 120000;  // 2 Minuten
const unsigned long TEST_HOLD_DURATION = 60000;   // 1 Minute

// ===== TIMING =====
unsigned long lastSoundUpdate = 0;
unsigned long lastUploadTime = 0;
unsigned long lastApiUploadTime = 0;
unsigned long lastHeartbeatTime = 0;
const int soundUpdateInterval = 1000;
const int uploadInterval = 3000;
const int apiUploadInterval = 60000;
const int heartbeatInterval = 300000;

float currentSoundLevel = 30.0;
int soundPercentage = 0;

const float QUIET_ROOM_DB = 30.0;
const float LOUD_ROOM_DB = 90.0;

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n=== ESP32-C6 SOUND TEST-SIMULATION (KEIN MIKROFON) ===\n");

  connectWiFi();

  configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
  Serial.println("Warte auf NTP...");
  time_t now = time(nullptr);
  int attempts = 0;
  while (now < 8 * 3600 * 24 && attempts < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }
  Serial.println(now > 8 * 3600 * 24 ? "\n✅ Zeit OK\n" : "\n⚠️ Zeit Fehler\n");

  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname("mikrofon_sound");
    ArduinoOTA.setPassword("mikro2025");

    ArduinoOTA.onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("🔄 OTA Update startet: " + type);
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\n✅ OTA Update abgeschlossen");
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
    Serial.println("📡 OTA bereit\n");
  }

  // TEST-SIMULATION START
  testCycleStart = millis();
  Serial.println("\n🚀 TEST-SIMULATION STARTET!");
  Serial.println("   → 2min ANSTIEG 30→80dB (5dB-Schritte)");
  Serial.println("   → 1min PLATEAU 80dB");
  Serial.println("   → 2min ABSTIEG 80→30dB\n");

  Serial.println("✅ SYSTEM BEREIT!\n");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long now = millis();

  // TEST-SIMULATION UPDATE
  updateTestSimulation();

  // SOUND LEVEL UPDATE
  if (now - lastSoundUpdate > soundUpdateInterval) {
    currentSoundLevel = testCurrentDB;
    soundPercentage = soundLevelToPercentage(currentSoundLevel);

    Serial.print("🔊 Sound: ");
    Serial.print(currentSoundLevel, 1);
    Serial.print(" dB → ");
    Serial.print(soundPercentage);
    Serial.println("% (TEST)");

    lastSoundUpdate = now;
  }

  // SERVER UPLOADS
  if (now - lastUploadTime > uploadInterval) {
    sendSoundDataToServer();
    lastUploadTime = now;
  }

  if (now - lastApiUploadTime > apiUploadInterval) {
    sendSensorReadingToAPI();
    lastApiUploadTime = now;
  }

  if (now - lastHeartbeatTime > heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeatTime = now;
  }

  delay(100);
}

// 🔥 TEST-SIMULATION
void updateTestSimulation() {
  unsigned long elapsed = millis() - testCycleStart;

  if (elapsed < TEST_RAMP_DURATION) {
    // PHASE 1: ANSTIEG 30→80dB (2 Minuten)
    float progress = (float)elapsed / TEST_RAMP_DURATION;
    int steps = (TEST_MAX_DB - QUIET_ROOM_DB) / TEST_STEP_DB;
    int currentStep = (int)(progress * steps);
    testCurrentDB = QUIET_ROOM_DB + (currentStep * TEST_STEP_DB);
  } 
  else if (elapsed < TEST_RAMP_DURATION + TEST_HOLD_DURATION) {
    // PHASE 2: PLATEAU 80dB (1 Minute)
    testCurrentDB = TEST_MAX_DB;
  } 
  else if (elapsed < 2 * TEST_RAMP_DURATION + TEST_HOLD_DURATION) {
    // PHASE 3: ABSTIEG 80→30dB (2 Minuten)
    unsigned long rampElapsed = elapsed - TEST_RAMP_DURATION - TEST_HOLD_DURATION;
    float progress = (float)rampElapsed / TEST_RAMP_DURATION;
    int steps = (TEST_MAX_DB - QUIET_ROOM_DB) / TEST_STEP_DB;
    int currentStep = steps - (int)(progress * steps);
    testCurrentDB = QUIET_ROOM_DB + (currentStep * TEST_STEP_DB);
  } 
  else {
    // ZYKLUS WIEDERHOLEN
    testCycleStart = millis();
    Serial.println("\n🔄 TEST-ZYKLUS WIEDERHOLEN\n");
  }
}

// DB → PERCENT
int soundLevelToPercentage(float dbLevel) {
  int percentage = (int)((dbLevel - QUIET_ROOM_DB) / (LOUD_ROOM_DB - QUIET_ROOM_DB) * 100.0);
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;
  return percentage;
}

void sendSoundDataToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Dashboard: Keine WiFi");
    return;
  }

  WiFiClientSecure* client = new WiFiClientSecure;
  if (!client) return;

  client->setInsecure();

  HTTPClient http;
  http.begin(*client, serverURL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setTimeout(5000);

  String data = "sound_level=" + String(soundPercentage);

  Serial.print("📤 Dashboard: " + data + " ... ");

  int httpResponseCode = http.POST(data);

  if (httpResponseCode > 0) {
    Serial.println("✅ (" + String(httpResponseCode) + ")");
  } else {
    Serial.println("❌ (" + String(httpResponseCode) + ")");
  }

  http.end();
  delete client;
}

String getISO8601Timestamp() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  if (!gmtime_r(&now, &timeinfo)) {
    return "";
  }

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S+00:00", &timeinfo);
  return String(buffer);
}

void sendSensorReadingToAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ API: Keine WiFi");
    return;
  }

  WiFiClientSecure* client = new WiFiClientSecure;
  if (!client) return;

  client->setInsecure();

  HTTPClient http;
  http.begin(*client, apiSensorEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", apiKey);
  http.setTimeout(5000);

  String timestamp = getISO8601Timestamp();
  if (timestamp == "") {
    Serial.println("❌ Timestamp-Fehler");
    delete client;
    return;
  }

  String jsonPayload = "{";
  jsonPayload += "\"sensor_id\":\"" + String(sensorID) + "\",";
  jsonPayload += "\"timestamp\":\"" + timestamp + "\",";
  jsonPayload += "\"value_num\":" + String(currentSoundLevel, 1) + ",";
  jsonPayload += "\"quality\":\"TEST\"";
  jsonPayload += "}";

  Serial.print("💾 API: " + String(currentSoundLevel, 1) + " dB ... ");

  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    Serial.println("✅ (" + String(httpResponseCode) + ")");
  } else {
    Serial.println("❌ (" + String(httpResponseCode) + ")");
  }

  http.end();
  delete client;
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Heartbeat: Keine WiFi");
    return;
  }

  WiFiClientSecure* client = new WiFiClientSecure;
  if (!client) return;

  client->setInsecure();

  HTTPClient http;
  http.begin(*client, apiHeartbeatEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", apiKey);
  http.setTimeout(5000);

  String timestamp = getISO8601Timestamp();
  if (timestamp == "") {
    Serial.println("❌ Timestamp-Fehler");
    delete client;
    return;
  }

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
