Hier ist eine klare Übersicht deines Schemas. Pro Tabelle: Zweck, wichtigste Spalten und typische Nutzung.

# Datenmodell – Kurz & verständlich

## 1) `space`

**Wofür?** Der logische Container für euren Aufenthaltsraum.
**Wichtige Spalten:** `id`, `name`, `description`, `created_at/updated_at`
**Nutzung:** Alles (Devices, Gates, Auslastung) hängt an genau **einem** `space`.

---

## 2) `device`

**Wofür?** Eure Hardware-Geräte: Mikrocontroller (ESP32-C6) und das Waveshare-Display.
**Wichtige Spalten:**

* `type` (`MICROCONTROLLER`|`DISPLAY`), `model`, `firmware_version`
* `api_key_hash` (statischer, gehashter API-Key), `is_active`, `last_seen`
* `space_id` (Zugehörigkeit zum Raum)
  **Nutzung:** Authentifizierung von Mess-POSTs, Lebenszeichen, Display-Abfragen.

---

## 3) `sensor`

**Wofür?** Konkrete Messquellen am Gerät.
**Wichtige Spalten:**

* `type` (`LIGHT_BARRIER`, `MICROPHONE`, `PIR`, `DISTANCE`)
* `unit` (z. B. `dB`, `bool`, `mm`), `config` (JSON für Kalibrierung/Schwellen)
* `device_id` (an welchem Gerät)
  **Nutzung:** Definiert, **woher** Rohdaten kommen (z. B. Lichtschranke A/B, Mikrofon, PIR).

---

## 4) `gate`

**Wofür?** Das **Sensor-Paar** am Eingang (A/B) zur Richtungsdetektion.
**Wichtige Spalten:**

* `sensor_a_id`, `sensor_b_id` (müssen verschieden sein)
* `invert_dir` (falls baulich „IN/OUT“ gespiegelt ist)
* `space_id`, `name`
  **Nutzung:** Grundlage für `flow_event` (rein/raus zählen).

---

## 5) `reading` *(optional, aber nützlich)*

**Wofür?** Rohmessungen als Zeitreihe (Debugging, spätere Fusion).
**Wichtige Spalten:**

* `sensor_id`, `ts` (Zeitstempel)
* `value_num` / `value_text` / `value_json` (flexibel je Sensor)
* `quality` (`OK`, `NOISE`, `DROPPED`)
  **Nutzung:** Mikrofon-RMS, PIR-Impulse, Distanzwerte; Basis für spätere Auswertungen.

---

## 6) `flow_event`

**Wofür?** **Ereignisse** aus dem Gate: Person **IN** oder **OUT**.
**Wichtige Spalten:**

* `gate_id`, `space_id`, `ts`, `direction` (`IN`|`OUT`)
* `confidence`, `duration_ms`, `raw_refs` (optional: Referenz auf Rohdaten)
  **Nutzung:** Kern für die **Personenzählung**: Nettosaldo = IN − OUT.

---

## 7) `occupancy_snapshot`

**Wofür?** Abgeleitete Auslastung zum Zeitpunkt *t*.
**Wichtige Spalten:**

* `people_estimate` (kann `NULL` sein, falls unzuverlässig)
* `level` (`LOW`, `MEDIUM`, `HIGH`)
* `method` (`FLOW_ONLY`, `NOISE_ONLY`, `FUSION`)
* `noise_db`, `motion_count`, `window_seconds`, `ts`, `space_id`
  **Nutzung:** Das ist der Wert fürs **Display** und fürs Dashboard. Entsteht z. B. alle 60–120 s durch euren Server-Job (Fusion aus Flow + Mikro + PIR).

---

## 8) `threshold_profile`

**Wofür?** Konfigurierbare **Schwellen** zur Ableitung von Levels.
**Wichtige Spalten:**

* `noise_levels` (JSON, z. B. `{"low":45,"medium":60,"high":75}`)
* `motion_levels` (JSON), `fusion_rules` (Gewichtungen)
* `is_default`, `space_id`, `name`
  **Nutzung:** Ohne Codeänderung Level-Grenzen anpassen; pro Raum ein Default-Profil.

---

## 9) `display_frame`

**Wofür?** Was das **Waveshare-Display** gezeigt hat / zeigen soll.
**Wichtige Spalten:**

* `device_id`, `ts`, `payload` (kompaktes JSON: Level, Text, Icon-ID)
* `status` (`SENT`, `ACK`, `FAILED`)
  **Nutzung:** Display-Pull oder Push-Protokoll; spätere Nachverfolgung/Retry.

---

## 10) `heartbeat`

**Wofür?** **Gesundheitsdaten** der Geräte.
**Wichtige Spalten:**

* `device_id`, `ts`, `ip`, `rssi`, `metrics` (JSON: uptime, temp, mem …)
  **Nutzung:** Monitoring: „lebt das Gerät“, wann zuletzt online, Signalstärke etc.

---

## 11) `calibration`

**Wofür?** Historie der **Kalibrierungen** je Sensor.
**Wichtige Spalten:**

* `sensor_id`, `ts`, `params` (JSON), `note`
  **Nutzung:** Nachvollziehbarkeit, warum sich Messwerte/Auslastung geändert haben.

---

## 12) View `v_people_net_today`

**Wofür?** Schnellübersicht: **Nettosaldo** der Personen **seit Mitternacht**.
**Spalten:** `space_id`, `day_start`, `net_people`
**Nutzung:** Einfache Anzeige/Debug: „Wie viele netto drin?“

---

## Mini-Datenfluss (Gedankengerüst)

`SENSOR → READING` (optional) → Gate-Logik → `FLOW_EVENT` → Aggregation/Logging → `OCCUPANCY_SNAPSHOT` → `DISPLAY_FRAME`
Konfiguration & Monitoring laufen über `threshold_profile`, `heartbeat`, `calibration`.

---

Wenn du willst, erstelle ich dir noch ein kleines **Insert-Beispiel** (1 Raum, 1 Gate mit Lichtschranken A/B, Mikro & PIR, Default-Thresholds) exakt passend zu deinem MySQL-Schema – dann kannst du in phpMyAdmin sofort testen.
