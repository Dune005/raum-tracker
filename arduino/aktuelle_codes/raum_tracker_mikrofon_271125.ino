#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <math.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "connectWiFi_hochschule.h"

const char* serverURL = "https://corner.klaus-klebband.ch/update_count.php";
const char* apiSensorEndpoint = "https://corner.klaus-klebband.ch/api/v1/sensor/reading";
const char* apiHeartbeatEndpoint = "https://corner.klaus-klebband.ch/api/v1/device/heartbeat";

const char* apiKey = "test_key_audio_789012";
const char* deviceID = "770e8400-e29b-41d4-a716-446655440002";
const char* sensorID = "550e8400-e29b-41d4-a716-446655440003";

// ===== I2S INMP441 KONFIGURATION =====
#define I2S_WS  4   // Word Select (L/RCLK)
#define I2S_SD  5   // Serial Data (DOUT)
#define I2S_SCK 6   // Bit Clock (BCLK)
#define I2S_PORT I2S_NUM_0

#define SAMPLE_RATE 16000
#define SAMPLE_BUFFER_SIZE 512  // ← INMP441 braucht kleinere Buffer
#define SAMPLES_PER_MEASUREMENT 512

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

// ===== INMP441 KALIBRIERUNG =====
const float QUIET_ROOM_DB = 30.0;
const float LOUD_ROOM_DB = 90.0;
const float GAIN_FACTOR = 5;
bool isCalibrated = false;
float baselineNoise = 0.0;
float lastRawValue = 0;

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== ESP32-C6 Sound Monitor (INMP441 Mikrofon) ===\n");
  
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
  
  if (setupI2S()) {
    Serial.println("✅ I2S INMP441 Mikrofon initialisiert\n");
  } else {
    Serial.println("❌ FEHLER: INMP441 Mikrofon konnte nicht initialisiert werden!");
    while(1) delay(1000);
  }
  
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
  
  Serial.println("Kalibriere INMP441... (5 Sekunden ruhig bleiben)");
  delay(2000);
  calibrateMicrophone();
  
  Serial.println("\n✅ Sound Monitor bereit!");
  Serial.printf("Gain-Faktor: %.1f | Bereich: %.0f-%.0f dB\n\n", 
                GAIN_FACTOR, QUIET_ROOM_DB, LOUD_ROOM_DB);
}

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
    
    Serial.print("🔊 Sound: ");
    Serial.print(currentSoundLevel, 1);
    Serial.print(" dB → ");
    Serial.print(soundPercentage);
    Serial.print("% (Raw: ");
    Serial.print(lastRawValue, 0);
    Serial.println(")");
    
    lastSoundUpdate = now;
  }
  
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

void sendSoundDataToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Dashboard: Keine WiFi");
    return;
  }

  WiFiClientSecure *client = new WiFiClientSecure;
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

  WiFiClientSecure *client = new WiFiClientSecure;
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
  jsonPayload += "\"quality\":\"OK\"";
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

  WiFiClientSecure *client = new WiFiClientSecure;
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

// ===== I2S SETUP FÜR INMP441 =====
bool setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // ← INMP441 ist mono (links)
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,           // ← Weniger Puffer für INMP441
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
    Serial.printf("❌ I2S install Fehler: %d\n", err);
    return false;
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("❌ I2S pin Fehler: %d\n", err);
    return false;
  }

  i2s_start(I2S_PORT);
  
  // ← INMP441 braucht weniger Aufwärmzeit
  int32_t dummy_samples[128];
  size_t dummy_bytes;
  for (int i = 0; i < 5; i++) {
    i2s_read(I2S_PORT, dummy_samples, sizeof(dummy_samples), &dummy_bytes, pdMS_TO_TICKS(100));
    delay(50);
  }
  
  return true;
}

void calibrateMicrophone() {
  Serial.println("Kalibriere...");
  
  float total = 0;
  float maxVal = 0;
  float minVal = 999999;
  int validSamples = 0;
  
  for (int i = 0; i < 50; i++) {
    float rawLevel = getRawAudioLevel();
    if (rawLevel > 100 && rawLevel < 500000) {
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
    Serial.println("\n✅ Kalibrierung OK");
    Serial.printf("   Baseline: %.0f | Min: %.0f | Max: %.0f\n", 
                  baselineNoise, minVal, maxVal);
  } else {
    baselineNoise = 30000;
    Serial.println("\n⚠️ Kalibrierung fehlgeschlagen - Standardwerte");
  }
}

float getRawAudioLevel() {
  int32_t samples[SAMPLES_PER_MEASUREMENT];
  size_t bytes_read = 0;
  
  // ← INMP441: Kürzere Timeout für schnellere Messung
  esp_err_t result = i2s_read(I2S_PORT, samples, sizeof(samples), 
                              &bytes_read, pdMS_TO_TICKS(100));
  
  if (result != ESP_OK || bytes_read == 0) {
    return lastRawValue;
  }

  float sumSquares = 0;
  int sample_count = bytes_read / sizeof(int32_t);
  int validSamples = 0;
  
  for (int i = 0; i < sample_count; i++) {
    // ← INMP441 gibt 32-bit Samples, nehme obere 16 Bit
    int32_t sample32 = samples[i] >> 14;  // ← Schiebe statt >> 16 für mehr Auflösung
    
    if (abs(sample32) < 32000) {
      float sampleFloat = (float)sample32;
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

float readSoundLevel() {
  if (!isCalibrated) {
    return QUIET_ROOM_DB;
  }
  
  float rawLevel = getRawAudioLevel();
  rawLevel = rawLevel * GAIN_FACTOR;
  
  float ratio = rawLevel / (baselineNoise * GAIN_FACTOR);
  
  float db = QUIET_ROOM_DB;
  if (ratio > 1.0) {
    db = QUIET_ROOM_DB + (20.0 * log10(ratio));
  }
  
  if (db < QUIET_ROOM_DB) db = QUIET_ROOM_DB;
  if (db > LOUD_ROOM_DB) db = LOUD_ROOM_DB;
  
  float smoothed = (currentSoundLevel * 0.6) + (db * 0.4);
  return smoothed;
}

int soundLevelToPercentage(float dbLevel) {
  int percentage = (int)((dbLevel - QUIET_ROOM_DB) / (LOUD_ROOM_DB - QUIET_ROOM_DB) * 100.0);
  
  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;
  
  return percentage;
}
