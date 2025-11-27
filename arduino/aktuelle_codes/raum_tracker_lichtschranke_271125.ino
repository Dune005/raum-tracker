#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <Adafruit_VL6180X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <time.h>
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

// ===== AUSKOMMENTIERT: Bewegungserkennung =====
// uint16_t lastRange1 = 8000;
// uint16_t lastRange2 = 8000;
// uint16_t lastRangeMiddle = 8000;
// const int MOVEMENT_THRESHOLD = 50;

const int MIN_DETECTION_DISTANCE = 50;
const int MAX_DETECTION_DISTANCE = 1200;  // ← Geändert von 1300 auf 1200mm (1.2m)

const int debounceTime = 800;
const int uploadInterval = 500;
const int heartbeatInterval = 300000;
const int triggerThreshold1 = 950;
const int triggerThreshold2 = 950;
const int triggerThresholdMiddle = 950;
const unsigned long maxSequenceTime = 1000;

enum DirectionState { IDLE, POSSIBLE_A, MIDDLE_CONFIRM, POSSIBLE_B };
DirectionState state = IDLE;

bool pendingSend = false;
String pendingDirection = "";
unsigned long pendingDuration = 0;

void setup() {
Serial.begin(115200);
delay(100);

Serial.println("ESP32-C6 Personenzähler (3-Sensor Validation Mode)");
Serial.println("==================================================");

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

Serial.print("Initialisiere VL6180X Sensor (Mitte - Validierung)... ");
digitalWrite(XSHUT_VL6180X_MIDDLE, HIGH);
delay(10);
if (!sensorMiddle.begin()) {
Serial.println("FEHLER!");
while (1);
}

Serial.println("OK (Adresse 0x29)");

Serial.println("\n✅ Personenzähler bereit!");
Serial.println("Konfiguration:");
Serial.println("  - 2x VL53L0X (High Speed, Adressen 0x30 & 0x31)");
Serial.println("  - 1x VL6180X Middle Validator (Adresse 0x29)");
Serial.println("  - Erkennungsbereich: 50-1200mm (1.2m)");
Serial.println("  - Upload-Intervall: 500ms");
Serial.println("  - 3-Sensor Validierungslogik aktiv");
Serial.println("Aktuelle Personenzahl: " + String(count));
}

void loop() {
static unsigned long lastDebugPrint = 0;
if (millis() - lastDebugPrint > 5000) {
Serial.println("===== Status =====");
Serial.println("WiFi: " + String(WiFi.status() == WL_CONNECTED ? "✅" : "❌") + " (RSSI: " + String(WiFi.RSSI()) + " dBm)");
Serial.println("Count: " + String(count));
Serial.println("State: " + String(state == IDLE ? "IDLE" : (state == POSSIBLE_A ? "POSSIBLE_A" : (state == MIDDLE_CONFIRM ? "MIDDLE_CONFIRM" : "POSSIBLE_B"))));
Serial.println("RAM frei: " + String(ESP.getFreeHeap() / 1024) + " KB");
Serial.println("==================");
lastDebugPrint = millis();
}

if (WiFi.status() == WL_CONNECTED) {
ArduinoOTA.handle();
}

if (WiFi.status() != WL_CONNECTED) {
connectWiFi();
}

unsigned long now = millis();

if (sensor1.isRangeComplete()) {
uint16_t range1 = sensor1.readRangeResult();
bool range1_valid = (range1 >= MIN_DETECTION_DISTANCE && range1 <= MAX_DETECTION_DISTANCE);

// ===== AUSKOMMENTIERT: Bewegungserkennung =====
// bool movement1 = range1_valid && (abs((int)range1 - (int)lastRange1) > MOVEMENT_THRESHOLD);
// bool A_trigger = movement1 && (range1 < triggerThreshold1);
// if (range1_valid) lastRange1 = range1;

// ===== NEUE LOGIK: Direkte Blockier-Erkennung =====
bool A_trigger = range1_valid && (range1 < triggerThreshold1);

if (state == IDLE && A_trigger) {
state = POSSIBLE_A;
lastTriggerTime = now;
Serial.println("→ Sensor A: " + String(range1) + "mm");
} else if (state == POSSIBLE_B && A_trigger && (now - lastTriggerTime) < maxSequenceTime) {
count--;
if (count < 0) count = 0;
unsigned long durationMs = now - lastTriggerTime;
Serial.println(">>> AUSTRITT! Anzahl: " + String(count));
queueSend("OUT", durationMs);
resetState();
} else if ((state == POSSIBLE_A || state == MIDDLE_CONFIRM || state == POSSIBLE_B) && (now - lastTriggerTime) > maxSequenceTime) {
Serial.println("→ Timeout");
resetState();
}
}

if (sensorMiddle.readRangeResult()) {
uint8_t rangeMiddle = sensorMiddle.readRange();
bool rangeMiddle_valid = (rangeMiddle >= MIN_DETECTION_DISTANCE && rangeMiddle <= MAX_DETECTION_DISTANCE);

// ===== AUSKOMMENTIERT: Bewegungserkennung =====
// bool movementMiddle = rangeMiddle_valid && (abs((int)rangeMiddle - (int)lastRangeMiddle) > MOVEMENT_THRESHOLD);
// bool Middle_trigger = movementMiddle && (rangeMiddle < triggerThresholdMiddle);
// if (rangeMiddle_valid) lastRangeMiddle = rangeMiddle;

// ===== NEUE LOGIK: Direkte Blockier-Erkennung =====
bool Middle_trigger = rangeMiddle_valid && (rangeMiddle < triggerThresholdMiddle);

if (state == POSSIBLE_A && Middle_trigger && (now - lastTriggerTime) < maxSequenceTime) {
state = MIDDLE_CONFIRM;
Serial.println("  ✓ Middle: " + String(rangeMiddle) + "mm - Bestätigung");
} else if (state == POSSIBLE_B && Middle_trigger && (now - lastTriggerTime) < maxSequenceTime) {
state = MIDDLE_CONFIRM;
Serial.println("  ✓ Middle: " + String(rangeMiddle) + "mm - Bestätigung");
}
}

if (sensor2.isRangeComplete()) {
uint16_t range2 = sensor2.readRangeResult();
bool range2_valid = (range2 >= MIN_DETECTION_DISTANCE && range2 <= MAX_DETECTION_DISTANCE);

// ===== AUSKOMMENTIERT: Bewegungserkennung =====
// bool movement2 = range2_valid && (abs((int)range2 - (int)lastRange2) > MOVEMENT_THRESHOLD);
// bool B_trigger = movement2 && (range2 < triggerThreshold2);
// if (range2_valid) lastRange2 = range2;

// ===== NEUE LOGIK: Direkte Blockier-Erkennung =====
bool B_trigger = range2_valid && (range2 < triggerThreshold2);

if (state == IDLE && B_trigger) {
state = POSSIBLE_B;
lastTriggerTime = now;
Serial.println("→ Sensor B: " + String(range2) + "mm");
} else if (state == MIDDLE_CONFIRM && B_trigger && (now - lastTriggerTime) < maxSequenceTime) {
count++;
unsigned long durationMs = now - lastTriggerTime;
Serial.println(">>> EINTRITT! Anzahl: " + String(count));
queueSend("IN", durationMs);
resetState();
} else if (state == POSSIBLE_A && B_trigger && (now - lastTriggerTime) < maxSequenceTime) {
count++;
unsigned long durationMs = now - lastTriggerTime;
Serial.println(">>> EINTRITT! Anzahl: " + String(count));
queueSend("IN", durationMs);
resetState();
} else if ((state == POSSIBLE_A || state == MIDDLE_CONFIRM || state == POSSIBLE_B) && (now - lastTriggerTime) > maxSequenceTime) {
Serial.println("→ Timeout");
resetState();
}
}

if (pendingSend) {
sendDataToServerAsync(pendingDirection);
sendFlowEventToAPIAsync(pendingDirection, pendingDuration);
pendingSend = false;
}

if (now - lastUploadTime > uploadInterval) {
sendDataToServerAsync("IDLE");
lastUploadTime = now;
}

if (now - lastHeartbeatTime > heartbeatInterval) {
sendHeartbeatAsync();
lastHeartbeatTime = now;
}
}

void resetState() {
state = IDLE;
// ===== AUSKOMMENTIERT: lastRange-Variablen =====
// lastRange1 = 8000;
// lastRange2 = 8000;
// lastRangeMiddle = 8000;
delay(debounceTime);
}

void queueSend(String direction, unsigned long durationMs) {
pendingSend = true;
pendingDirection = direction;
pendingDuration = durationMs;
}

void sendDataToServerAsync(String direction) {
if (WiFi.status() != WL_CONNECTED) {
Serial.println("❌ Dashboard: Keine WiFi-Verbindung");
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
if (response.length() > 0 && response.length() < 200) {
Serial.println("   Server-Antwort: " + response);
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

Serial.println("");
Serial.println("   JSON: " + jsonPayload);

unsigned long startTime = millis();
int httpResponseCode = http.POST(jsonPayload);
unsigned long duration = millis() - startTime;

if (httpResponseCode > 0) {
String response = http.getString();
Serial.println("✅ API: " + String(httpResponseCode) + " (" + String(duration) + "ms)");
if (response.length() > 0 && response.length() < 500) {
Serial.println("   API-Antwort: " + response);
}
} else {
Serial.println("❌ API Fehler: " + String(httpResponseCode) + " (" + String(duration) + "ms)");
}

http.end();
delete client;
}

void sendHeartbeatAsync() {
if (WiFi.status() != WL_CONNECTED) {
Serial.println("❌ Heartbeat: Keine WiFi-Verbindung");
return;
}

Serial.print("💓 Heartbeat ... ");

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