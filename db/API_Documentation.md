# API Dokumentation - Raum-Tracker

## Übersicht

REST API für das Aufenthaltsraum-Tracking-System. Die API ermöglicht die Kommunikation zwischen Mikrocontrollern, Display und Web-Dashboard.

**Base URL:** `https://your-domain.ch/api/v1`

---

## Authentifizierung

### Für Mikrocontroller (geschützte Endpoints)
Alle Mikrocontroller-Endpoints erfordern einen API-Key im Header:

```http
X-API-Key: {api_key}
```

Der API-Key wird gegen den `api_key_hash` in der `device`-Tabelle geprüft (z.B. via `password_verify()` in PHP).

### Für Display & Dashboard
Display und Dashboard-Endpoints sind **öffentlich zugänglich** (kein API-Key erforderlich).

---

## Endpoints

### 1. Mikrocontroller Endpoints (geschützt)

#### 1.1 Rohmessung senden
Sendet eine einzelne Sensor-Messung.

**Endpoint:** `POST /sensor/reading`

**Headers:**
```http
Content-Type: application/json
X-API-Key: {api_key}
```

**Request Body:**
```json
{
  "sensor_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2025-10-22T14:30:00Z",
  "value_num": 65.3,
  "value_text": null,
  "value_json": null,
  "quality": "OK"
}
```

**Parameter:**
- `sensor_id` (string, required): UUID des Sensors
- `timestamp` (string, required): ISO 8601 Zeitstempel
- `value_num` (float, optional): Numerischer Wert (z.B. dB, mm, °C)
- `value_text` (string, optional): Text-Wert
- `value_json` (object, optional): JSON-Objekt für komplexe Daten
- `quality` (string, optional): `OK`, `NOISE`, `DROPPED` (default: `OK`)

**Response (201 Created):**
```json
{
  "success": true,
  "reading_id": 12345,
  "message": "Messung erfolgreich gespeichert"
}
```

**Response (401 Unauthorized):**
```json
{
  "success": false,
  "error": "Invalid API key"
}
```

---

#### 1.2 Flow-Event senden
Sendet ein IN/OUT-Event vom Gate (Lichtschranken-Paar).

**Endpoint:** `POST /gate/flow`

**Headers:**
```http
Content-Type: application/json
X-API-Key: {api_key}
```

**Request Body:**
```json
{
  "gate_id": "660e8400-e29b-41d4-a716-446655440001",
  "timestamp": "2025-10-22T14:30:05Z",
  "direction": "IN",
  "confidence": 0.95,
  "duration_ms": 450,
  "raw_refs": {
    "sensor_a_reading_id": 12340,
    "sensor_b_reading_id": 12341
  }
}
```

**Parameter:**
- `gate_id` (string, required): UUID des Gates
- `timestamp` (string, required): ISO 8601 Zeitstempel
- `direction` (string, required): `IN` oder `OUT`
- `confidence` (float, optional): Konfidenz 0-1 (wie sicher ist die Richtungserkennung)
- `duration_ms` (int, optional): Dauer des Durchgangs in Millisekunden
- `raw_refs` (object, optional): Referenzen zu den zugrundeliegenden Sensor-Readings

**Response (201 Created):**
```json
{
  "success": true,
  "flow_event_id": 67890,
  "message": "Flow-Event erfolgreich gespeichert",
  "current_net_people": 12
}
```

---

#### 1.3 Heartbeat senden
Sendet Lebenszeichen und Gesundheitsdaten des Geräts.

**Endpoint:** `POST /device/heartbeat`

**Headers:**
```http
Content-Type: application/json
X-API-Key: {api_key}
```

**Request Body:**
```json
{
  "device_id": "770e8400-e29b-41d4-a716-446655440002",
  "timestamp": "2025-10-22T14:30:00Z",
  "ip": "192.168.1.42",
  "rssi": -65,
  "metrics": {
    "uptime_seconds": 3600,
    "temperature_c": 42.5,
    "free_memory_kb": 25600,
    "wifi_quality": "good"
  }
}
```

**Parameter:**
- `device_id` (string, required): UUID des Geräts
- `timestamp` (string, required): ISO 8601 Zeitstempel
- `ip` (string, optional): IP-Adresse des Geräts
- `rssi` (int, optional): WiFi-Signalstärke in dBm
- `metrics` (object, optional): Beliebige Health-Metriken

**Response (200 OK):**
```json
{
  "success": true,
  "message": "Heartbeat empfangen",
  "device_status": "active"
}
```

---

### 2. Display Endpoints (öffentlich)

#### 2.1 Aktuelle Auslastung abrufen
Display fragt die aktuellsten Daten ab (Pull-Mechanismus).

**Endpoint:** `GET /display/current?space_id={space_id}`

**Query Parameter:**
- `space_id` (string, required): UUID des Raums

**Response (200 OK):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440003",
    "space_name": "Aufenthaltsraum IM5",
    "timestamp": "2025-10-22T14:30:00Z",
    "level": "MEDIUM",
    "people_estimate": 8,
    "noise_db": 62.5,
    "method": "FUSION",
    "display_text": "Mittlere Auslastung",
    "icon": "medium_occupancy",
    "color": "orange"
  }
}
```

**Parameter (Response):**
- `level`: `LOW`, `MEDIUM`, `HIGH`
- `people_estimate`: Geschätzte Personenanzahl (kann `null` sein)
- `noise_db`: Aktuelle Lautstärke
- `method`: `FLOW_ONLY`, `NOISE_ONLY`, `FUSION`
- `display_text`: Vorgefertigter Text fürs Display
- `icon`: Icon-Identifier für die Visualisierung
- `color`: Farb-Code (für das 4-Farben E-Ink Display)

---

#### 2.2 Display-Bestätigung senden
Display bestätigt, dass der Frame angezeigt wurde.

**Endpoint:** `POST /display/ack`

**Headers:**
```http
Content-Type: application/json
```

**Request Body:**
```json
{
  "device_id": "990e8400-e29b-41d4-a716-446655440004",
  "frame_id": 12345,
  "timestamp": "2025-10-22T14:30:10Z",
  "status": "ACK"
}
```

**Parameter:**
- `device_id` (string, required): UUID des Display-Geräts
- `frame_id` (int, optional): ID des angezeigten Frames (falls vorhanden)
- `timestamp` (string, required): Zeitstempel der Anzeige
- `status` (string, required): `ACK` oder `FAILED`

**Response (200 OK):**
```json
{
  "success": true,
  "message": "Bestätigung gespeichert"
}
```

---

### 3. Dashboard Endpoints (öffentlich)

#### 3.1 Aktuelle Auslastung
Liefert die aktuellste Auslastung mit zusätzlichen Details.

**Endpoint:** `GET /occupancy/current?space_id={space_id}`

**Query Parameter:**
- `space_id` (string, required): UUID des Raums

**Response (200 OK):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440003",
    "space_name": "Aufenthaltsraum IM5",
    "timestamp": "2025-10-22T14:30:00Z",
    "level": "MEDIUM",
    "people_estimate": 8,
    "noise_db": 62.5,
    "motion_count": 5,
    "method": "FUSION",
    "window_seconds": 120,
    "net_people_today": 12,
    "trend": "increasing"
  }
}
```

---

#### 3.2 Historische Auslastung
Liefert Auslastungsdaten für einen Zeitraum.

**Endpoint:** `GET /occupancy/history`

**Query Parameter:**
- `space_id` (string, required): UUID des Raums
- `from` (string, optional): Start-Zeitstempel (ISO 8601), default: vor 30 Tagen
- `to` (string, optional): End-Zeitstempel (ISO 8601), default: jetzt
- `interval` (string, optional): Aggregationsintervall (`5min`, `15min`, `1hour`, `1day`), default: `15min`

**Beispiel:** `GET /occupancy/history?space_id=880e8400&from=2025-10-15T00:00:00Z&to=2025-10-22T23:59:59Z&interval=1hour`

**Response (200 OK):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440003",
    "from": "2025-10-15T00:00:00Z",
    "to": "2025-10-22T23:59:59Z",
    "interval": "1hour",
    "datapoints": [
      {
        "timestamp": "2025-10-22T12:00:00Z",
        "level": "HIGH",
        "people_estimate_avg": 15,
        "noise_db_avg": 72.3,
        "motion_count_avg": 8
      },
      {
        "timestamp": "2025-10-22T13:00:00Z",
        "level": "MEDIUM",
        "people_estimate_avg": 9,
        "noise_db_avg": 58.1,
        "motion_count_avg": 4
      }
    ]
  }
}
```

---

#### 3.3 Statistiken heute
Zusammenfassung der heutigen Auslastung.

**Endpoint:** `GET /statistics/today?space_id={space_id}`

**Query Parameter:**
- `space_id` (string, required): UUID des Raums

**Response (200 OK):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440003",
    "date": "2025-10-22",
    "net_people_today": 12,
    "total_entries": 45,
    "total_exits": 33,
    "peak_occupancy": {
      "time": "12:15:00",
      "people_estimate": 18,
      "level": "HIGH"
    },
    "avg_noise_db": 62.4,
    "busiest_hour": {
      "hour": 12,
      "avg_people": 15
    },
    "current_level": "MEDIUM"
  }
}
```

---

#### 3.4 Wochenübersicht
Statistiken für die letzten 7 Tage.

**Endpoint:** `GET /statistics/week?space_id={space_id}`

**Query Parameter:**
- `space_id` (string, required): UUID des Raums

**Response (200 OK):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440003",
    "from": "2025-10-15",
    "to": "2025-10-22",
    "daily_stats": [
      {
        "date": "2025-10-22",
        "avg_occupancy": 12,
        "peak_occupancy": 18,
        "total_entries": 45,
        "avg_noise_db": 62.4,
        "busiest_hour": 12
      },
      {
        "date": "2025-10-21",
        "avg_occupancy": 10,
        "peak_occupancy": 15,
        "total_entries": 38,
        "avg_noise_db": 58.2,
        "busiest_hour": 13
      }
    ],
    "week_summary": {
      "avg_daily_entries": 42,
      "most_busy_day": "2025-10-22",
      "least_busy_day": "2025-10-20",
      "avg_peak_hour": 12
    }
  }
}
```

---

#### 3.5 Flow-Events (Rohdaten)
Liefert die einzelnen IN/OUT-Events für einen Zeitraum.

**Endpoint:** `GET /flow/events`

**Query Parameter:**
- `space_id` (string, required): UUID des Raums
- `from` (string, optional): Start-Zeitstempel (ISO 8601), default: heute 00:00
- `to` (string, optional): End-Zeitstempel (ISO 8601), default: jetzt
- `limit` (int, optional): Max. Anzahl Events, default: 100

**Response (200 OK):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440003",
    "from": "2025-10-22T00:00:00Z",
    "to": "2025-10-22T23:59:59Z",
    "total_events": 78,
    "events": [
      {
        "id": 67890,
        "timestamp": "2025-10-22T14:30:05Z",
        "direction": "IN",
        "gate_name": "Haupteingang",
        "confidence": 0.95,
        "duration_ms": 450
      },
      {
        "id": 67889,
        "timestamp": "2025-10-22T14:28:12Z",
        "direction": "OUT",
        "gate_name": "Haupteingang",
        "confidence": 0.88,
        "duration_ms": 520
      }
    ]
  }
}
```

---

### 4. Admin Endpoints (optional, für spätere Erweiterung)

#### 4.1 Geräte-Liste
Übersicht aller registrierten Geräte.

**Endpoint:** `GET /admin/devices?space_id={space_id}`

**Response (200 OK):**
```json
{
  "success": true,
  "data": [
    {
      "id": "770e8400-e29b-41d4-a716-446655440002",
      "type": "MICROCONTROLLER",
      "model": "ESP32-C6-N8",
      "firmware_version": "1.2.0",
      "is_active": true,
      "last_seen": "2025-10-22T14:30:00Z",
      "sensors": [
        {
          "id": "550e8400-e29b-41d4-a716-446655440000",
          "type": "LIGHT_BARRIER",
          "unit": "mm",
          "is_active": true
        }
      ]
    }
  ]
}
```

---

#### 4.2 Schwellenwerte aktualisieren
Ändert die Threshold-Konfiguration.

**Endpoint:** `PUT /admin/threshold/{profile_id}`

**Request Body:**
```json
{
  "noise_levels": {
    "low": 45,
    "medium": 60,
    "high": 75
  },
  "motion_levels": {
    "low": 2,
    "medium": 5,
    "high": 10
  },
  "fusion_rules": {
    "weights": {
      "flow": 0.6,
      "noise": 0.3,
      "motion": 0.1
    }
  }
}
```

**Response (200 OK):**
```json
{
  "success": true,
  "message": "Schwellenwerte aktualisiert"
}
```

---

#### 4.3 Kalibrierung durchführen
Speichert eine neue Kalibrierung für einen Sensor.

**Endpoint:** `POST /admin/calibration`

**Request Body:**
```json
{
  "sensor_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": "2025-10-22T14:30:00Z",
  "params": {
    "offset": 5.2,
    "scale_factor": 0.98,
    "reference_value": 100
  },
  "note": "Kalibrierung nach Reinigung des Sensors"
}
```

**Response (201 Created):**
```json
{
  "success": true,
  "calibration_id": 123,
  "message": "Kalibrierung gespeichert"
}
```

---

## Error Responses

Alle Endpoints können folgende Fehler zurückgeben:

### 400 Bad Request
```json
{
  "success": false,
  "error": "Invalid request",
  "details": "Missing required field: sensor_id"
}
```

### 401 Unauthorized
```json
{
  "success": false,
  "error": "Unauthorized",
  "details": "Invalid or missing API key"
}
```

### 404 Not Found
```json
{
  "success": false,
  "error": "Not found",
  "details": "Space with ID 880e8400 not found"
}
```

### 500 Internal Server Error
```json
{
  "success": false,
  "error": "Internal server error",
  "details": "Database connection failed"
}
```

---

## Cron-Job: Occupancy Snapshot Generation

Ein PHP-Cron-Job läuft alle 60-120 Sekunden und:

1. Liest die letzten Flow-Events (IN/OUT)
2. Berechnet den Netto-Personensaldo
3. Aggregiert Mikrofon-Daten (durchschnittliche dB)
4. Aggregiert PIR/Motion-Daten
5. Wendet die `threshold_profile`-Regeln an (Fusion)
6. Speichert einen neuen `occupancy_snapshot`-Eintrag

**Beispiel-Logik:**
```php
// Pseudo-Code für Cron-Job
$flowNet = countFlowEvents($spaceId, $windowSeconds); // IN - OUT
$avgNoise = getAvgNoise($spaceId, $windowSeconds);
$motionCount = getMotionCount($spaceId, $windowSeconds);

$threshold = getDefaultThreshold($spaceId);

// Fusion-Logik
$level = calculateLevel($flowNet, $avgNoise, $motionCount, $threshold);

// Snapshot speichern
insertOccupancySnapshot([
    'space_id' => $spaceId,
    'ts' => now(),
    'people_estimate' => $flowNet,
    'level' => $level,
    'method' => 'FUSION',
    'noise_db' => $avgNoise,
    'motion_count' => $motionCount,
    'window_seconds' => $windowSeconds
]);
```

---

## Implementierungs-Reihenfolge (Empfehlung)

1. **Phase 1:** Basis-Infrastruktur
   - DB-Connection (PDO)
   - API-Key-Authentifizierung
   - Error-Handling

2. **Phase 2:** Mikrocontroller-Endpoints
   - `POST /sensor/reading`
   - `POST /gate/flow`
   - `POST /device/heartbeat`

3. **Phase 3:** Cron-Job
   - Occupancy-Snapshot-Berechnung
   - Threshold-Evaluation

4. **Phase 4:** Display-Endpoints
   - `GET /display/current`
   - `POST /display/ack`

5. **Phase 5:** Dashboard-Endpoints
   - `GET /occupancy/current`
   - `GET /occupancy/history`
   - `GET /statistics/*`

6. **Phase 6:** Admin-Endpoints (optional)
   - Device-Management
   - Threshold-Konfiguration
   - Kalibrierung

---

## Testing

### Mikrocontroller-Simulator (curl)

**Messung senden:**
```bash
curl -X POST http://localhost/api/v1/sensor/reading \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-test-key" \
  -d '{
    "sensor_id": "550e8400-e29b-41d4-a716-446655440000",
    "timestamp": "2025-10-22T14:30:00Z",
    "value_num": 65.3,
    "quality": "OK"
  }'
```

**Flow-Event senden:**
```bash
curl -X POST http://localhost/api/v1/gate/flow \
  -H "Content-Type: application/json" \
  -H "X-API-Key: your-test-key" \
  -d '{
    "gate_id": "660e8400-e29b-41d4-a716-446655440001",
    "timestamp": "2025-10-22T14:30:05Z",
    "direction": "IN",
    "confidence": 0.95
  }'
```

### Dashboard-Test

**Aktuelle Auslastung abrufen:**
```bash
curl http://localhost/api/v1/occupancy/current?space_id=880e8400-e29b-41d4-a716-446655440003
```

---

## Nächste Schritte

1. ✅ Datenbank-Schema erstellt
2. ✅ API-Dokumentation erstellt
3. ⏳ PHP-Backend implementieren
4. ⏳ Mikrocontroller-Firmware (C++ für ESP32)
5. ⏳ Display-Software (C++ für Waveshare)
6. ⏳ Web-Dashboard (HTML/CSS/JS)

---

**Dokumentations-Version:** 1.0  
**Letzte Aktualisierung:** 22. Oktober 2025
