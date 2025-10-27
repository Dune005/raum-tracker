# API Endpoints - Raum-Tracker

**Base URL:** `https://corner.klaus-klebband.ch/api/index.php?path=v1`  
**Alternative (mit URL Rewriting):** `https://corner.klaus-klebband.ch/api/v1`

---

## 🔐 Authentifizierung

**Mikrocontroller-Endpoints** (geschützt): Benötigen Header `X-API-Key`  
**Display & Dashboard-Endpoints** (öffentlich): Keine Authentifizierung

---

## Mikrocontroller-Endpoints

### 1. POST /sensor/reading
Speichert eine Sensor-Messung (Mikrofon, PIR, Distanz).

**Auth:** ✅ API-Key erforderlich

**Request:**
```json
{
  "sensor_id": "550e8400-e29b-41d4-a716-446655440003",
  "timestamp": "2025-10-22T18:30:00+01:00",
  "value_num": 73.8,
  "quality": "OK"
}
```

**Response (201):**
```json
{
  "success": true,
  "message": "Messung erfolgreich gespeichert",
  "reading_id": 15,
  "sensor_type": "MICROPHONE"
}
```

---

### 2. POST /gate/flow
Speichert ein Flow-Event (Person IN/OUT).

**Auth:** ✅ API-Key erforderlich

**Request:**
```json
{
  "gate_id": "660e8400-e29b-41d4-a716-446655440001",
  "timestamp": "2025-10-22T18:40:00+01:00",
  "direction": "IN",
  "confidence": 0.97,
  "duration_ms": 420
}
```

**Response (201):**
```json
{
  "success": true,
  "message": "Flow-Event erfolgreich gespeichert",
  "flow_event_id": 30,
  "current_net_people": 8
}
```

---

### 3. POST /device/heartbeat
Sendet Lebenszeichen und Status-Metriken.

**Auth:** ✅ API-Key erforderlich

**Request:**
```json
{
  "device_id": "770e8400-e29b-41d4-a716-446655440001",
  "timestamp": "2025-10-22T18:45:00+01:00",
  "ip": "192.168.1.42",
  "rssi": -62,
  "metrics": {
    "uptime_seconds": 25200,
    "temperature_c": 41.2,
    "free_memory_kb": 26800
  }
}
```

**Response (200):**
```json
{
  "success": true,
  "message": "Heartbeat empfangen",
  "data": {
    "heartbeat_id": 9,
    "device_status": "active"
  }
}
```

---

## Display-Endpoints

### 4. GET /display/current
Liefert aktuelle Auslastung für E-Ink Display.

**Auth:** ❌ Öffentlich

**Query-Parameter:**
- `space_id` (required): UUID des Raums

**Response (200):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440001",
    "space_name": "Aufenthaltsraum IM5",
    "timestamp": "2025-10-22T18:30:00",
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

---

### 5. POST /display/ack
Display bestätigt Anzeige eines Frames.

**Auth:** ❌ Öffentlich

**Request:**
```json
{
  "device_id": "990e8400-e29b-41d4-a716-446655440001",
  "timestamp": "2025-10-22T18:50:00+01:00",
  "status": "ACK"
}
```

**Response (200):**
```json
{
  "success": true,
  "message": "Bestätigung gespeichert"
}
```

---

## Dashboard-Endpoints

### 6. GET /occupancy/current
Aktuelle Auslastung mit erweiterten Details.

**Auth:** ❌ Öffentlich

**Query-Parameter:**
- `space_id` (required)

**Response (200):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440001",
    "space_name": "Aufenthaltsraum IM5",
    "timestamp": "2025-10-22T18:30:00",
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

### 7. GET /occupancy/history
Historische Auslastungsdaten (aggregiert).

**Auth:** ❌ Öffentlich

**Query-Parameter:**
- `space_id` (required)
- `from` (optional): ISO 8601 Timestamp (default: -30 Tage)
- `to` (optional): ISO 8601 Timestamp (default: jetzt)
- `interval` (optional): `5min`, `15min`, `1hour`, `1day` (default: `15min`)

**Response (200):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440001",
    "from": "2025-10-15T00:00:00",
    "to": "2025-10-22T23:59:59",
    "interval": "1hour",
    "datapoints": [
      {
        "timestamp": "2025-10-22T12:00:00",
        "level": "HIGH",
        "people_estimate_avg": 15,
        "noise_db_avg": 72.3,
        "motion_count_avg": 8
      }
    ]
  }
}
```

---

### 8. GET /statistics/today
Tagesstatistiken (Zusammenfassung).

**Auth:** ❌ Öffentlich

**Query-Parameter:**
- `space_id` (required)

**Response (200):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440001",
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

### 9. GET /statistics/week
Wochenstatistiken (letzte 7 Tage).

**Auth:** ❌ Öffentlich

**Query-Parameter:**
- `space_id` (required)

**Response (200):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440001",
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

### 10. GET /flow/events
Rohdaten der Flow-Events (IN/OUT).

**Auth:** ❌ Öffentlich

**Query-Parameter:**
- `space_id` (required)
- `from` (optional): ISO 8601 (default: heute 00:00)
- `to` (optional): ISO 8601 (default: jetzt)
- `limit` (optional): max. 1000 (default: 100)

**Response (200):**
```json
{
  "success": true,
  "data": {
    "space_id": "880e8400-e29b-41d4-a716-446655440001",
    "from": "2025-10-22T00:00:00",
    "to": "2025-10-22T23:59:59",
    "total_events": 78,
    "events": [
      {
        "id": 67890,
        "timestamp": "2025-10-22T14:30:05",
        "direction": "IN",
        "gate_name": "Haupteingang Treppe",
        "confidence": 0.95,
        "duration_ms": 450
      }
    ]
  }
}
```

---

## Test-Credentials

**Mikrocontroller 1 (Gate):**
- API-Key: `test_key_gate_123456`
- Device-ID: `770e8400-e29b-41d4-a716-446655440001`

**Mikrocontroller 2 (Audio/PIR):**
- API-Key: `test_key_audio_789012`
- Device-ID: `770e8400-e29b-41d4-a716-446655440002`

**Space-ID:** `880e8400-e29b-41d4-a716-446655440001`

---

## Error-Responses

Alle Endpoints können folgende Fehler zurückgeben:

**400 Bad Request:**
```json
{
  "success": false,
  "error": "Invalid request",
  "details": "Missing required field: sensor_id"
}
```

**401 Unauthorized:**
```json
{
  "success": false,
  "error": "Unauthorized",
  "details": "Invalid or missing API key"
}
```

**404 Not Found:**
```json
{
  "success": false,
  "error": "Not found",
  "details": "Space with ID xyz not found"
}
```

**500 Internal Server Error:**
```json
{
  "success": false,
  "error": "Internal server error",
  "details": "Database connection failed"
}
```

---

**Version:** 1.0  
**Stand:** 22. Oktober 2025
