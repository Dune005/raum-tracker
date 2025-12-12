# Drift-Korrektur für Lichtschranken-basierte Personenzählung

**Version:** 2.0  
**Datum:** 6. Dezember 2025  
**System:** Raum-Tracker IM5 Aufenthaltsraum

---

## 📋 Inhaltsverzeichnis

1. [Problem: Was ist Counter-Drift?](#problem-was-ist-counter-drift)
2. [Lösungsansatz: Software-basierte Korrektur](#lösungsansatz-software-basierte-korrektur)
3. [Systemarchitektur](#systemarchitektur)
4. [Komponenten im Detail](#komponenten-im-detail)
5. [Drift-Erkennung: Die Heuristik](#drift-erkennung-die-heuristik)
6. [Skalierung bei hohen Werten](#skalierung-bei-hohen-werten)
7. [Arduino-Synchronisation](#arduino-synchronisation)
8. [Konfiguration & Parameter](#konfiguration--parameter)
9. [Monitoring & Debugging](#monitoring--debugging)
10. [Deployment-Anleitung](#deployment-anleitung)

---

## Problem: Was ist Counter-Drift?

### Symptome
Bei Tests des Lichtschranken-Systems wurden folgende Probleme festgestellt:

- **Unter-Zählung:** Bei 30-45 anwesenden Personen zeigt der Counter nur 12-22 (Faktor 1.5-3x zu niedrig)
- **Geister-Personen:** Auch wenn der Raum komplett leer ist, zeigt der Counter 5-7 Personen an
- **Asymmetrie:** OUT-Events werden häufiger verpasst als IN-Events → Counter driftet nach oben

### Ursachen
1. **Schnelle Durchgänge:** Zwei Personen gleichzeitig = nur 1 Event erkannt
2. **Seitliche Durchgänge:** Personen gehen nah an der Wand vorbei → Sensor-Range zu groß
3. **Gruppen-Effekt:** Bei Stoßzeiten mehrere Personen gleichzeitig → systematisches Unter-Zählen
4. **OUT-Event-Verlust:** Beim Verlassen wird öfter "geschummelt" (seitlich raus, zu schnell, etc.)

### Warum keine Hardware-Lösung?
- Hardware (3x Lichtschranken) bereits montiert und im Betrieb
- Budget-Beschränkung: Keine zusätzlichen Sensoren verfügbar
- Zeitrahmen: Software-Update schneller als Hardware-Umbau

**→ Entscheidung: Software-basierte Korrektur im Backend**

---

## Lösungsansatz: Software-basierte Korrektur

### Zwei-Stufen-Ansatz

#### 1. **Drift-Korrektur** (Geister-Personen eliminieren)
- **Problem:** Counter zeigt 5-7 Personen obwohl Raum leer ist
- **Lösung:** Backend erkennt "leerer Raum" und setzt Counter auf 0
- **Bedingung:** Keine IN-Events in letzten 30 Minuten, aber ≥2 OUT-Events

#### 2. **Skalierung** (Unter-Zählung kompensieren)
- **Problem:** Bei 40 Personen zeigt Counter nur 20 (Faktor 2x zu niedrig)
- **Lösung:** Ab 10 gezählten Personen wird Counter mit Faktor 2.0 multipliziert
- **Ergebnis:** `display_count = counter_raw × 2.0`

### Zwei-Zähler-Prinzip
Das System arbeitet mit **zwei separaten Werten**:

| Wert | Beschreibung | Quelle |
|------|--------------|--------|
| `counter_raw` | Roher Zählerstand vom Arduino (mit Drift) | ESP32 Lichtschranken |
| `display_count` | Korrigierter Wert (nach Drift-Korrektur & Skalierung) | Backend-Berechnung |

**Wichtig:** Nur `display_count` wird an Display und Dashboard gesendet!

---

## Systemarchitektur

```
┌─────────────────┐
│   ESP32-C6      │  1. Zählt Personen (IN/OUT)
│  Lichtschranken │  2. POST count zu update_count.php
└────────┬────────┘  3. Empfängt JSON mit drift_corrected Flag
         │           4. Setzt lokalen Counter auf 0 bei Drift
         ↓
┌─────────────────────────────────────────────────────┐
│  update_count.php (PHP)                             │
│  ─────────────────────────────────────────────────  │
│  • Empfängt counter_raw vom Arduino                 │
│  • Lädt DriftCorrector.php                          │
│  • Prüft ob Drift-Korrektur notwendig               │
│    → shouldCorrectDrift(space_id, counter_raw)      │
│  • Wenn JA: applyDriftCorrection() → counter=0      │
│  • Wenn NEIN: computeDisplayCount() → Skalierung    │
│  • Speichert in counter_state Tabelle               │
│  • Gibt JSON zurück mit drift_corrected Flag        │
└────────┬────────────────────────────────────────────┘
         │
         ↓
┌─────────────────────────────────────────────────────┐
│  Cron-Job: generate_occupancy_snapshot.php          │
│  (läuft alle 60 Sekunden)                           │
│  ─────────────────────────────────────────────────  │
│  • Lädt counter_state aus DB (nicht vom Arduino!)   │
│  • Prüft erneut auf Drift                           │
│  • Berechnet Level (LOW/MEDIUM/HIGH)                │
│  • Speichert Snapshot mit beiden Werten:            │
│    - counter_raw (roh)                              │
│    - display_count (korrigiert)                     │
│    - drift_corrected (Flag)                         │
│    - scale_applied (Flag)                           │
└────────┬────────────────────────────────────────────┘
         │
         ↓
┌─────────────────────────────────────────────────────┐
│  API-Endpoints                                      │
│  ─────────────────────────────────────────────────  │
│  • /api/v1/occupancy/current                        │
│    → Liefert display_count für Dashboard            │
│  • /api/v1/display/current                          │
│    → Liefert display_count als people_count         │
│  • Beide zeigen korrigierten Wert!                  │
└─────────────────────────────────────────────────────┘
```

---

## Komponenten im Detail

### 1. Datenbank: `counter_state` Tabelle

**Speichert den aktuellen Zählerstand pro Space:**

```sql
CREATE TABLE counter_state (
    space_id CHAR(36) PRIMARY KEY,
    counter_raw INT DEFAULT 0,           -- Roher Arduino-Wert
    display_count INT DEFAULT 0,         -- Korrigierter Wert
    last_drift_correction DATETIME,      -- Wann wurde zuletzt korrigiert?
    drift_corrections_today INT DEFAULT 0,  -- Wie oft heute korrigiert?
    in_events_today INT DEFAULT 0,       -- Statistik
    out_events_today INT DEFAULT 0,
    last_update DATETIME
);
```

**Wichtig:** Diese Tabelle ist die **Single Source of Truth** für den aktuellen Zählerstand!

### 2. Drift-Config in `threshold_profile`

**JSON-Spalte mit allen Parametern:**

```json
{
  "drift_max": 7,                        // Max. Wert für Drift-Erkennung
  "drift_window_minutes": 30,            // Zeitfenster für Event-Analyse
  "min_out_events_for_reset": 2,         // Mind. OUT-Events für Reset
  "scale_threshold": 10,                 // Ab wann skalieren?
  "scale_factor": 2.0,                   // Multiplikations-Faktor
  "max_capacity": 60,                    // Obergrenze Raum-Kapazität
  "min_correction_interval_minutes": 5   // Mind. Abstand zw. Korrekturen
}
```

### 3. PHP-Klasse: `DriftCorrector.php`

**Zentrale Logik-Komponente mit 6 Hauptfunktionen:**

#### `getDriftConfig($spaceId)`
Lädt Drift-Parameter aus `threshold_profile.drift_config`.

#### `shouldCorrectDrift($spaceId, $counterRaw, $driftConfig)`
Prüft ob Drift-Korrektur notwendig ist:
- Counter zwischen 1-7 ✓
- Keine IN-Events in letzten 30 Minuten ✓
- Mindestens 2 OUT-Events vorhanden ✓
- Letzte Korrektur mindestens 5 Minuten her ✓

#### `applyDriftCorrection($spaceId)`
Setzt `counter_raw` und `display_count` auf 0, erhöht `drift_corrections_today`.

#### `computeDisplayCount($counterRaw, $driftConfig)`
Berechnet korrigierten Wert:
```php
if ($counterRaw < 10) {
    return $counterRaw;  // Kleine Werte: keine Skalierung
}
$scaled = round($counterRaw * 2.0);
return min($scaled, 60);  // Begrenzt auf max_capacity
```

#### `updateCounterState($spaceId, $counterRaw, $displayCount)`
Speichert neue Werte in DB.

#### `getCounterState($spaceId)`
Liest aktuellen State aus DB.

### 4. Arduino-Sketch mit JSON-Parsing

**Neue Funktionalität in `sendToUpdate()`:**

```cpp
// JSON-Response vom Server parsen
StaticJsonDocument<512> doc;
deserializeJson(doc, response);

bool driftCorrected = doc["drift_corrected"] | false;

if (driftCorrected) {
    // DRIFT-KORREKTUR: Lokalen Counter zurücksetzen!
    int oldCount = count;
    count = 0;
    Serial.println("🔄 DRIFT-KORREKTUR: " + String(oldCount) + " → 0");
}
```

**Wichtig:** Arduino benötigt `ArduinoJson` Library (Version 6.x)!

---

## Drift-Erkennung: Die Heuristik

### Wann wird Drift erkannt?

Das System erkennt "Geister-Personen" anhand dieser Logik:

```
WENN alle 4 Bedingungen erfüllt sind:
  1. counter_raw ist KLEIN (1-7 Personen)
  2. Keine IN-Events in letzten 30 Minuten
  3. Mindestens 2 OUT-Events in letzten 30 Minuten
  4. Letzte Korrektur mindestens 5 Minuten her
  
DANN:
  → Raum ist vermutlich leer!
  → Setze counter_raw auf 0
  → Informiere Arduino via JSON
```

### Beispiel-Szenario

**Situation:**
- Counter zeigt 5 Personen
- Letzte 30 Minuten:
  - 0 IN-Events (niemand kam rein)
  - 3 OUT-Events (3 Personen gingen raus)

**Analyse:**
- ✓ Counter ist klein (5 ≤ 7)
- ✓ Keine IN-Events
- ✓ Mehrere OUT-Events (3 ≥ 2)
- ✓ Letzte Korrektur vor >5 Minuten

**Aktion:**
→ **DRIFT ERKANNT!** Counter wird auf 0 gesetzt.

### Warum funktioniert das?

**Grundannahme:** Wenn über 30 Minuten niemand reinkommt, aber mehrere rausgehen, dann:
- War der ursprüngliche Counter zu hoch (Drift)
- Raum leert sich gerade / ist schon leer
- Verbleibender Rest-Counter = Geister-Personen

**Schutz vor Fehlalarmen:**
- Nur bei kleinen Werten (≤7) → keine "echten" 30 Personen werden gelöscht
- Mindestabstand 5 Minuten → nicht bei jedem Zyklus
- Minimum 2 OUT-Events → nicht bei einzelnen Ausreißern

---

## Skalierung bei hohen Werten

### Warum Skalierung?

Bei hoher Auslastung (Stoßzeiten) wird systematisch unter-gezählt:
- 2 Personen gleichzeitig → nur 1 Event erkannt
- Schnelle Durchgänge → Sensor zu langsam
- **Beobachtung:** Ab 10 gezählten Personen ist Faktor ca. 2x zu niedrig

### Skalierungs-Logik

```
WENN counter_raw < 10:
  → Keine Skalierung (oft akkurat genug)
  → display_count = counter_raw
  
WENN counter_raw >= 10:
  → Skalierung anwenden
  → display_count = counter_raw × 2.0
  → Begrenzt auf max_capacity (60)
```

### Beispiel-Werte

| counter_raw | Berechnung | display_count |
|-------------|------------|---------------|
| 5 | 5 (keine Skalierung) | **5** |
| 9 | 9 (keine Skalierung) | **9** |
| 10 | 10 × 2.0 | **20** |
| 15 | 15 × 2.0 | **30** |
| 25 | 25 × 2.0 | **50** |
| 35 | 35 × 2.0 = 70 → max 60 | **60** |

### Warum Schwellenwert bei 10?

- **Unter 10 Personen:** Messfehler oft ausgeglichen (mal +1, mal -1)
- **Ab 10 Personen:** Systematischer Fehler dominiert (immer zu niedrig)
- **Empirisch validiert:** Tests zeigten besten Kompromiss bei Schwelle 10

---

## Arduino-Synchronisation

### Problem ohne Synchronisation

**Ohne Drift-Korrektur-Synchronisation:**

```
Arduino:  count = 7  →  POST count=7  →  Server setzt auf 0
Arduino:  count = 7  (weiß nichts von Reset!)
          ↓
Person kommt rein
          ↓
Arduino:  count = 8  →  POST count=8
Server:   "8? Aber war doch gerade 0!" → Falsche Daten!
```

### Lösung: JSON-Response mit Flag

**Mit Synchronisation:**

```
Arduino:  count = 7  →  POST count=7
                         ↓
Server:   Erkennt Drift
          Setzt counter_state auf 0
          Antwortet: {"drift_corrected": true, "count": 0}
                         ↓
Arduino:  Parst JSON
          Liest drift_corrected = true
          Setzt count = 0
          ↓
          SYNCHRONISIERT! ✓
```

### JSON-Response-Format

**Normale Antwort (kein Drift):**
```json
{
  "status": "success",
  "count": 12,
  "display_count": 24,
  "drift_corrected": false
}
```

**Drift-Korrektur-Antwort:**
```json
{
  "status": "success",
  "count": 0,
  "display_count": 0,
  "drift_corrected": true  ← Arduino reagiert darauf!
}
```

---

## Konfiguration & Parameter

### Standard-Werte (IM5 Aufenthaltsraum)

| Parameter | Wert | Bedeutung |
|-----------|------|-----------|
| `drift_max` | 7 | Max. Counter für Drift-Erkennung |
| `drift_window_minutes` | 30 | Zeitfenster für Event-Analyse |
| `min_out_events_for_reset` | 2 | Mind. OUT-Events für Reset |
| `scale_threshold` | 10 | Ab dieser Zahl wird skaliert |
| `scale_factor` | 2.0 | Multiplikations-Faktor |
| `max_capacity` | 60 | Obergrenze Raum-Kapazität |
| `min_correction_interval_minutes` | 5 | Mind. Abstand zwischen Korrekturen |

### Anpassung für anderen Space

**In phpMyAdmin / MySQL:**

```sql
UPDATE threshold_profile 
SET drift_config = JSON_SET(
    drift_config,
    '$.scale_factor', 1.8,          -- Weniger aggressiv skalieren
    '$.drift_window_minutes', 20    -- Kürzeres Zeitfenster
)
WHERE space_id = 'deine-space-uuid';
```

### Parameter-Kalibrierung

**scale_factor** (Empfehlung 1.5 - 2.5):
- Zu niedrig (1.5): Immer noch Unter-Zählung
- Optimal (2.0): Realistische Werte bei Stoßzeiten
- Zu hoch (3.0): Über-Zählung möglich

**drift_window_minutes** (Empfehlung 20-45):
- Zu kurz (10): Fehlalarme bei kurzen ruhigen Phasen
- Optimal (30): Gute Balance
- Zu lang (60): Drift-Korrektur zu träge

**drift_max** (Empfehlung 5-10):
- Zu niedrig (3): Nur sehr kleine Geister erkannt
- Optimal (7): Deckt typische Drift-Werte ab
- Zu hoch (15): Könnte echte Personen löschen

---

## Monitoring & Debugging

### Wichtige Datenbank-Abfragen

#### Aktueller Counter-State
```sql
SELECT 
    space_id,
    counter_raw,
    display_count,
    drift_corrections_today,
    last_drift_correction,
    last_update
FROM counter_state
WHERE space_id = '880e8400-e29b-41d4-a716-446655440001';
```

#### Drift-Korrekturen heute
```sql
SELECT 
    ts,
    counter_raw,
    display_count,
    drift_corrected,
    scale_applied
FROM occupancy_snapshot
WHERE space_id = '880e8400-e29b-41d4-a716-446655440001'
  AND DATE(ts) = CURDATE()
  AND drift_corrected = 1
ORDER BY ts DESC;
```

#### Skalierungs-Statistik
```sql
SELECT 
    DATE(ts) AS datum,
    COUNT(*) AS snapshots_total,
    SUM(scale_applied) AS skaliert_count,
    AVG(CASE WHEN counter_raw > 0 THEN display_count / counter_raw ELSE 1 END) AS avg_faktor
FROM occupancy_snapshot
WHERE space_id = '880e8400-e29b-41d4-a716-446655440001'
  AND counter_raw > 0
GROUP BY DATE(ts)
ORDER BY datum DESC
LIMIT 7;
```

### Arduino Serial Monitor

**Bei Drift-Korrektur sichtbar:**
```
🚶 EINGANG (A→M→B) | Count: 5 | 450ms
📤 Sende an Dashboard... count=5 direction=IN ... ✅ 200 (85ms)
🔄 DRIFT-KORREKTUR: Counter 5 → 0 (Server-Reset)
   ℹ️ Display-Count: 0
```

**Bei normaler Skalierung:**
```
🚶 EINGANG (A→M→B) | Count: 12 | 520ms
📤 Sende an Dashboard... count=12 direction=IN ... ✅ 200 (92ms)
   ✓ Server-Count: 12
   ℹ️ Display-Count: 24
```

### Cron-Job Logs

**Beispiel-Output bei Drift:**
```
[2025-12-06 14:23:45] Verarbeite Space: Aufenthaltsraum IM5 (880e...)
[2025-12-06 14:23:45]   → Counter-State: raw=6, display=6
[2025-12-06 14:23:45]   → ⚠️ Drift erkannt! Korrektur wird angewendet...
[2025-12-06 14:23:45]   → Berechnetes Level: LOW
[2025-12-06 14:23:45]   ✅ Snapshot erstellt (ID: 1234) [DRIFT-KORREKTUR!]
```

---

## Deployment-Anleitung

### Schritt 1: Datenbank-Migration

**Auf Server via phpMyAdmin:**

1. Backup der Datenbank erstellen!
2. SQL-Script ausführen: `db/drift_korrektur_schema.sql`
3. Verifizieren:
   ```sql
   SHOW TABLES LIKE 'counter_state';
   SELECT * FROM counter_state;
   ```

### Schritt 2: PHP-Dateien hochladen

**Dateien auf Server kopieren (via FTP/SFTP):**

```
Lokal → Server
─────────────────────────────────────────────────────────
api/includes/DriftCorrector.php
  → /httpdocs/api/includes/DriftCorrector.php

update_count.php
  → /httpdocs/update_count.php

api/cron/generate_occupancy_snapshot.php
  → /httpdocs/api/cron/generate_occupancy_snapshot.php

api/v1/occupancy/current.php
  → /httpdocs/api/v1/occupancy/current.php

api/v1/display/current.php
  → /httpdocs/api/v1/display/current.php
```

### Schritt 3: Arduino-Update

**In Arduino IDE:**

1. Library installieren:
   - `Sketch > Include Library > Manage Libraries`
   - Suche: **ArduinoJson**
   - Installiere Version 6.x von Benoit Blanchon

2. Sketch hochladen:
   - Öffne: `arduino/aktuelle_codes/raum_tracker_lichtschranke_061225_drift.ino`
   - Board: ESP32-C6
   - Upload

3. Serial Monitor prüfen:
   - Baudrate: 115200
   - Auf "Drift-Korrektur v2.0" achten

### Schritt 4: Verifizierung

**1. API-Test:**
```bash
curl "https://corner.klaus-klebband.ch/api/v1/occupancy/current?space_id=880e8400-e29b-41d4-a716-446655440001"
```

**Erwartete Response (mit neuen Feldern):**
```json
{
  "space_id": "880e8400-e29b-41d4-a716-446655440001",
  "display_count": 24,
  "counter_raw": 12,
  "drift_corrected": false,
  "scale_applied": true,
  ...
}
```

**2. Arduino-Test:**
- Durchgang auslösen
- Serial Monitor beobachten
- Auf "drift_corrected" in Response achten

**3. Datenbank-Check:**
```sql
SELECT * FROM counter_state ORDER BY last_update DESC LIMIT 1;
SELECT * FROM occupancy_snapshot ORDER BY ts DESC LIMIT 5;
```

### Schritt 5: Monitoring erste Stunden

**Nach Deployment überwachen:**

1. **Arduino Serial Output** (~15 Minuten)
   - Drift-Korrekturen werden geloggt?
   - JSON-Parsing funktioniert?

2. **counter_state Tabelle** (~30 Minuten)
   - Werte werden aktualisiert?
   - `drift_corrections_today` zählt hoch?

3. **occupancy_snapshot** (~1 Stunde)
   - Neue Spalten gefüllt?
   - `scale_applied` = 1 bei counter_raw ≥ 10?

---

## Troubleshooting

### Problem: Arduino zeigt JSON-Parse-Error

**Symptom:**
```
⚠️ JSON-Parse-Error: InvalidInput
```

**Ursache:** Server gibt kein gültiges JSON zurück.

**Lösung:**
1. `update_count.php` Response im Browser prüfen
2. PHP-Fehler in `error_log` checken
3. Sicherstellen dass `DriftCorrector.php` korrekt eingebunden ist

---

### Problem: Drift wird nicht erkannt

**Symptom:** Counter bleibt bei 5-7 stehen, obwohl Raum leer.

**Debug:**
```sql
-- Prüfe Event-Historie
SELECT direction, COUNT(*) 
FROM flow_event 
WHERE space_id = '880e8400...' 
  AND ts >= DATE_SUB(NOW(), INTERVAL 30 MINUTE)
GROUP BY direction;
```

**Mögliche Ursachen:**
- `min_out_events_for_reset` zu hoch (senken auf 1)
- `drift_window_minutes` zu kurz (erhöhen auf 45)
- Letzte Korrektur < 5 Minuten her (warten)

---

### Problem: Skalierung zu aggressiv

**Symptom:** Bei 15 echten Personen zeigt Display 30.

**Lösung:**
```sql
UPDATE threshold_profile 
SET drift_config = JSON_SET(
    drift_config,
    '$.scale_factor', 1.6,        -- Weniger skalieren
    '$.scale_threshold', 15       -- Später beginnen
)
WHERE space_id = '880e8400...';
```

---

### Problem: counter_state wird nicht aktualisiert

**Symptom:** `last_update` ist veraltet.

**Debug:**
1. `update_count.php` Fehlerlog prüfen
2. Datenbankverbindung testen
3. Try-Catch-Block in `update_count.php` prüfen

**Quick-Fix:**
```sql
-- Manuell zurücksetzen
UPDATE counter_state 
SET counter_raw = 0, 
    display_count = 0, 
    last_update = NOW() 
WHERE space_id = '880e8400...';
```

---

## Zusammenfassung

### Kern-Konzepte

1. **Zwei-Zähler-System:** `counter_raw` (Arduino) vs. `display_count` (Backend)
2. **Drift-Erkennung:** Leerer Raum = keine IN-Events + OUT-Events → Reset
3. **Skalierung:** Ab 10 Personen Faktor 2x für realistische Werte
4. **Arduino-Sync:** JSON-Response mit `drift_corrected` Flag
5. **counter_state:** Single Source of Truth für aktuellen Zählerstand

### Vorteile dieser Lösung

✅ Keine Hardware-Änderungen notwendig  
✅ Einfach konfigurierbar (JSON-Parameter)  
✅ Transparent nachvollziehbar (beide Werte gespeichert)  
✅ Skaliert automatisch mit Auslastung  
✅ Selbstkorrigierend bei Drift  

### Nachteile / Limitationen

⚠️ Heuristik kann bei ungewöhnlichen Mustern versagen  
⚠️ Skalierungs-Faktor muss ggf. nachjustiert werden  
⚠️ Erfordert Arduino-Update (ArduinoJson)  
⚠️ Zusätzliche DB-Tabelle erforderlich  

---

**Bei Fragen oder Problemen:**  
Siehe `Anleitungen/Drift_Korrektur_Implementierungsplan.md` für technische Details.
