#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <math.h>
#include <ArduinoOTA.h>
#include <time.h>  // Für ISO 8601 Timestamps
#include "connectWiFi_hochschule.h"  // WPA2-Enterprise (eduroam), Credentials in password_hochschule.h


// ==== WLAN Konfiguration ====
// eduroam/WPA2-Enterprise wird über connectWiFi_hochschule.h + password_hochschule.h konfiguriert.

// Live-Dashboard (Flat-File)
const char* serverURL = "https://corner.klaus-klebband.ch/update_count.php";

// API-Endpoints (Datenbank-Speicherung)
const char* apiSensorEndpoint = "https://corner.klaus-klebband.ch/api/v1/sensor/reading";
const char* apiHeartbeatEndpoint = "https://corner.klaus-klebband.ch/api/v1/device/heartbeat";

// API-Credentials
const char* apiKey = "test_key_audio_789012";
const char* deviceID = "770e8400-e29b-41d4-a716-446655440002";
const char* sensorID = "550e8400-e29b-41d4-a716-446655440003";  // Mikrofon


// ==== I2S Mikrofon Konfiguration (ESP32-C6) ====
#define I2S_WS   12   // GPIO12 - Word Select/LRCLK
#define I2S_SD   11   // GPIO11 - Serial Data/DOUT  
#define I2S_SCK  10   // GPIO10 - Serial Clock/BCLK
#define I2S_PORT I2S_NUM_0


// ==== Audio Parameter ====
#define SAMPLE_RATE 16000
#define SAMPLE_BUFFER_SIZE 1024
#define SAMPLES_PER_MEASUREMENT 512


// ==== Timing ====
unsigned long lastSoundUpdate = 0;
unsigned long lastUploadTime = 0;
unsigned long lastApiUploadTime = 0;
unsigned long lastHeartbeatTime = 0;
const int soundUpdateInterval = 1000;  // ms - Messung
const int uploadInterval = 3000;       // ms - Live-Dashboard
const int apiUploadInterval = 60000;   // ms - API alle 60 Sekunden (nicht zu oft!)
const int heartbeatInterval = 300000;  // ms - Heartbeat alle 5 Minuten


// ==== Sound-Variablen ====
float currentSoundLevel = 30.0;  // dB (intern für Berechnung) - angepasst
int soundPercentage = 0;


// ==== VERBESSERTE KALIBRIERUNG ====
const float QUIET_ROOM_DB = 30.0;  // Niedrigerer Startwert
const float LOUD_ROOM_DB = 90.0;   // Höherer Maximalwert
const float GAIN_FACTOR = 3.5;     // Verstärkungsfaktor für mehr Empfindlichkeit
bool isCalibrated = false;
float baselineNoise = 0.0;
float lastRawValue = 0;


// ==== SETUP ====
void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("ESP32-C6 Sound Monitor Board - Verbesserte Empfindlichkeit");
  Serial.println("==========================================================");
  
  // WLAN-Verbindung über eduroam (WPA2-Enterprise)
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
  
  if (setupI2S()) {
    Serial.println("I2S Mikrofon erfolgreich initialisiert");
  } else {
    Serial.println("FEHLER: I2S Mikrofon konnte nicht initialisiert werden!");
    while(1) delay(1000);
  }
  
  // OTA-Funktionalität aktivieren
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
    Serial.println("📡 OTA bereit - Hostname: mikrofon_sound");
  }
  
  Serial.println("Kalibriere Mikrofon... (5 Sekunden ruhig bleiben)");
  delay(2000);
  calibrateMicrophone();
  
  Serial.println("\n✅ Sound Monitor bereit!");
  Serial.println("Sendet: sound_level als PROZENT (0-100)");
  Serial.printf("Gain-Faktor: %.1f | Bereich: %.0f-%.0f dB\n", 
                GAIN_FACTOR, QUIET_ROOM_DB, LOUD_ROOM_DB);
}


// ==== HAUPTSCHLEIFE ====
void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  
  unsigned long now = millis();
  
  if (now - lastSoundUpdate > soundUpdateInterval) {
    currentSoundLevel = readSoundLevel();
    soundPercentage = soundLevelToPercentage(currentSoundLevel);
    
    Serial.print("Sound: ");
    Serial.print(currentSoundLevel, 1);
    Serial.print(" dB → ");
    Serial.print(soundPercentage);
    Serial.print("% (Raw: ");
    Serial.print(lastRawValue, 0);
    Serial.println(")");
    
    lastSoundUpdate = now;
  }
  
  // Live-Dashboard Update (häufig)
  if (now - lastUploadTime > uploadInterval) {
    sendSoundDataToServer();
    lastUploadTime = now;
  }

  // API-Update (nur alle 60 Sekunden - nicht zu oft!)
  if (now - lastApiUploadTime > apiUploadInterval) {
    sendSensorReadingToAPI();
    lastApiUploadTime = now;
  }

  // Heartbeat alle 5 Minuten
  if (now - lastHeartbeatTime > heartbeatInterval) {
    sendHeartbeat();
    lastHeartbeatTime = now;
  }
  
  delay(100);
}


// ==== LIVE-DASHBOARD UPDATE (Flat-File) ====
void sendSoundDataToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Keine WiFi - Sound-Upload übersprungen");
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
  
  String data = "sound_level=" + String(soundPercentage);
  
  Serial.print("📤 Live-Dashboard: " + data + " ... ");
  
  int httpResponseCode = http.POST(data);
  
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

// ==== NEU: SENSOR-READING AN API SENDEN (alle 60s) ====
void sendSensorReadingToAPI() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Keine WiFi - API-Reading übersprungen");
    return;
  }

  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) return;
  
  client->setInsecure();

  HTTPClient http;
  http.begin(*client, apiSensorEndpoint);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", apiKey);
  http.setTimeout(5000);
  
  // ISO 8601 Timestamp generieren
  String timestamp = getISO8601Timestamp();
  if (timestamp == "") {
    Serial.println("❌ Timestamp-Fehler - Reading übersprungen");
    delete client;
    return;
  }
  
  // JSON-Payload erstellen
  String jsonPayload = "{";
  jsonPayload += "\"sensor_id\":\"" + String(sensorID) + "\",";
  jsonPayload += "\"timestamp\":\"" + timestamp + "\",";
  jsonPayload += "\"value_num\":" + String(currentSoundLevel, 1) + ",";  // dB-Wert
  jsonPayload += "\"quality\":\"OK\"";
  jsonPayload += "}";
  
  Serial.print("💾 API Sensor-Reading: " + String(currentSoundLevel, 1) + " dB ... ");
  
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

// ==== NEU: HEARTBEAT AN API SENDEN ====
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


// ==== I2S SETUP ====
bool setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = SAMPLE_BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("I2S driver install Fehler: %d\n", err);
    return false;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("I2S pin config Fehler: %d\n", err);
    return false;
  }

  i2s_start(I2S_PORT);
  
  // Stabilisierung
  int32_t dummy_samples[128];
  size_t dummy_bytes;
  for (int i = 0; i < 10; i++) {
    i2s_read(I2S_PORT, dummy_samples, sizeof(dummy_samples), &dummy_bytes, pdMS_TO_TICKS(100));
    delay(50);
  }
  
  return true;
}


// ==== VERBESSERTE KALIBRIERUNG ====
void calibrateMicrophone() {
  Serial.println("Kalibriere Ruhepegel...");
  
  float total = 0;
  float maxVal = 0;
  float minVal = 999999;
  int validSamples = 0;
  
  for (int i = 0; i < 50; i++) {
    float rawLevel = getRawAudioLevel();
    if (rawLevel > 100 && rawLevel < 500000) {  // Breiterer Akzeptanzbereich
      total += rawLevel;
      maxVal = max(maxVal, rawLevel);
      minVal = min(minVal, rawLevel);
      validSamples++;
    }
    delay(100);
    Serial.print(".");
  }
  
  if (validSamples > 10) {
    baselineNoise = total / validSamples;
    isCalibrated = true;
    Serial.println("\n✅ Kalibrierung erfolgreich!");
    Serial.printf("   Baseline: %.0f | Min: %.0f | Max: %.0f\n", 
                  baselineNoise, minVal, maxVal);
  } else {
    baselineNoise = 30000;  // Niedrigerer Standardwert
    Serial.println("\n⚠️ Kalibrierung fehlgeschlagen - verwende Standardwerte");
  }
}


// ==== VERBESSERTE RAW AUDIO LEVEL ====
float getRawAudioLevel() {
  int32_t samples[SAMPLES_PER_MEASUREMENT];
  size_t bytes_read = 0;
  
  esp_err_t result = i2s_read(I2S_PORT, samples, sizeof(samples), 
                              &bytes_read, pdMS_TO_TICKS(200));
  
  if (result != ESP_OK || bytes_read == 0) {
    return lastRawValue;
  }

  float sumSquares = 0;
  int sample_count = bytes_read / sizeof(int32_t);
  int validSamples = 0;
  
  for (int i = 0; i < sample_count; i++) {
    int32_t sample32 = samples[i];
    int16_t sample16 = sample32 >> 16;
    
    if (abs(sample16) < 32000) {
      float sampleFloat = (float)sample16;
      sumSquares += sampleFloat * sampleFloat;
      validSamples++;
    }
  }
  
  if (validSamples > 10) {
    lastRawValue = sqrt(sumSquares / validSamples);
    return lastRawValue;
  }
  
  return lastRawValue;
}


// ==== VERBESSERTE SOUND LEVEL BERECHNUNG ====
float readSoundLevel() {
  if (!isCalibrated) {
    return QUIET_ROOM_DB;
  }
  
  float rawLevel = getRawAudioLevel();
  
  // Verstärkung anwenden für mehr Empfindlichkeit
  rawLevel = rawLevel * GAIN_FACTOR;
  
  float ratio = rawLevel / (baselineNoise * GAIN_FACTOR);
  
  // Erweiterte dB-Berechnung
  float db = QUIET_ROOM_DB;
  if (ratio > 1.0) {
    db = QUIET_ROOM_DB + (20.0 * log10(ratio));
  }
  
  // Breitere Grenzen
  if (db < QUIET_ROOM_DB) db = QUIET_ROOM_DB;
  if (db > LOUD_ROOM_DB) db = LOUD_ROOM_DB;
  
  // Weniger aggressive Glättung für schnellere Reaktion
  float smoothed = (currentSoundLevel * 0.6) + (db * 0.4);
  return smoothed;
}


// ==== PROZENT KONVERTIERUNG ====
int soundLevelToPercentage(float dbLevel) {
  int percentage = (int)((dbLevel - QUIET_ROOM_DB) / (LOUD_ROOM_DB - QUIET_ROOM_DB) * 100.0);
  
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;
  
  return percentage;
}


// ==== WIFI VERBINDUNG ====
// Die Implementierung von connectWiFi() kommt nun aus connectWiFi_hochschule.h
// (nutzt lokale Datei password_hochschule.h mit EAP_IDENTITY, ssid, EAP_PASSWORD).