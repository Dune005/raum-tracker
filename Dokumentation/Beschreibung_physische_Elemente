# Physische Komponenten – Wie sie zusammenarbeiten

> Eine Übersicht der Hardware, deren Funktion und wie alles miteinander kommuniziert

---

## 🏗️ **Systemüberblick**

```
┌─────────────────────────────────────────────────────────────┐
│                        RAUM (Aufenthaltsraum IM5)            │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  🚪 Eingang                              🖥️ Foyer             │
│  ├─ Lichtschranke A (links)             └─ E-Ink Display    │
│  └─ Lichtschranke B (rechts)                (zeigt Auslastung)
│                                                               │
│  🎤 Raummitte                                                │
│  └─ Mikrofon (misst Lautstärke)                             │
│                                                               │
└─────────────────────────────────────────────────────────────┘
          ↓ WiFi (eduroam)
    ☁️ SERVER (corner.klaus-klebband.ch)
          ↓
    💾 Datenbank (occupancy_snapshot)
```

---

## 🔧 **Hardware-Komponenten**

### **1️⃣ Lichtschranken-Gate (Eingang)**

**Was:** Zwei ToF-Sensoren (VL53L0X) nebeneinander
- **Sensor A** (GPIO2 / Adresse 0x30) - Eingang links
- **Sensor B** (GPIO3 / Adresse 0x29) - Eingang rechts

**Wie es funktioniert:**
```
Person geht REIN:
  Sensor A wird unterbrochen → dann Sensor B
  → Reihenfolge: A vor B = REIN (+1 Person)

Person geht RAUS:
  Sensor B wird unterbrochen → dann Sensor A
  → Reihenfolge: B vor A = RAUS (-1 Person)
```

**Wo sitzt das:** ESP32-C6 Mikrocontroller (oben am Gate)

**Was es speichert:** 
- Flow-Events (IN/OUT) in Datenbank
- Live-Dashboard Update (aktueller Zählerstand)

---

### **2️⃣ Mikrofon (Raumlautstärke)**

**Was:** INMP441-I2S Mikrofon (digitales Mikrofon)
- Sitzt in der Raummitte
- Verbunden mit separatem **ESP32-C6** (Audio-Board)
- Über I2S-Protokoll

**Wie es funktioniert:**
```
1. Mikrofon misst Schallwellen
2. Konvertiert zu digitalen Samples (16-bit, 16 kHz)
3. Berechnet RMS (Effektivwert)
4. Konvertiert zu Dezibel (dB) → 30-90 dB Skala
5. Glättet den Wert (alle 1 Sekunde)
```

**Was es speichert:**
- Lautstärke-Readings in API (alle 60 Sekunden)
- Live-Dashboard Update (Schallpegel in %)

---

### **3️⃣ E-Ink Display (Foyer)**

**Was:** 4-Farben E-Ink Display (GxEPD2)
- Größe: 400×300 Pixel
- Farben: Schwarz, Weiß, Rot, Gelb
- Verbunden mit **ESP32-C6** (Display-Board)

**Wie es funktioniert:**
```
1. Fragt API ab: GET /api/v1/occupancy/current?space_id=...
2. Empfängt Snapshot-Daten:
   - people_estimate (Personenzahl)
   - noise_db (Lautstärke)
   - level (LOW/MEDIUM/HIGH)
   - timestamp (Zeitstempel)
3. Zeigt 4 Quadranten an:
   ┌─────────────┬─────────────┐
   │  PERSONEN   │ AUSLASTUNG  │
   ├─────────────┼─────────────┤
   │ LAUTSTÄRKE  │  STOSSZEIT  │
   └─────────────┴─────────────┘
4. Aktualisiert alle 60 Sekunden
```

**Was es tut:**
- ✅ Zeigt aktuelle Auslastung an
- ✅ Färbt Quadranten rot/gelb/weiß je nach Level
- ✅ Zeigt "Keine Daten" außerhalb von 10:00-17:00 Uhr
- ✅ Speichert Acknowledgements in DB

---

## 🔄 **Datenfluss – Schritt für Schritt**

### **Szenario: Person betritt den Raum**

```
1. LICHTSCHRANKE (Gate-ESP32)
   ├─ Sensor A erkennt Bewegung (von 500mm → 350mm)
   ├─ State Machine: IDLE → POSSIBLE_A
   ├─ Wartet auf Sensor B
   └─ (max. 600ms)

2. LICHTSCHRANKE (weiterhin)
   ├─ Sensor B folgt schnell nach
   ├─ State Machine: POSSIBLE_A → EINTRITT erkannt
   ├─ count++ (z.B. 15 → 16)
   └─ 📤 Sendet sofort:
      ├─ POST /update_count.php (Zähler live)
      └─ POST /api/v1/gate/flow (Datenbank speichern)

3. SERVER (alle 60 Sekunden via Cron)
   ├─ Cron lädt: generate_occupancy_snapshot.php
   ├─ Berechnet neuen Snapshot:
   │  ├─ people_estimate = aktuelle Flow-Bilanz
   │  ├─ level = LOW/MEDIUM/HIGH (basierend auf Lautstärke + Personen)
   │  └─ noise_db = letzter Mikrofon-Wert
   └─ 💾 Speichert in occupancy_snapshot Tabelle

4. DISPLAY (alle 60 Sekunden)
   ├─ GET /api/v1/occupancy/current?space_id=...
   ├─ Erhält Snapshot-Daten
   ├─ Rendert neue Anzeige (in Quadranten)
   └─ 📊 Zeigt aktualisierte Werte

5. DATENBANK
   └─ flow_event + occupancy_snapshot gespeichert ✅
```

---

## 📡 **Kommunikationsprotokolle**

| Komponente | Protokoll | Ziel | Frequenz |
|-----------|-----------|------|----------|
| **Gate-ESP32** | WiFi (eduroam) | Server API | sofort + alle 3s |
| **Audio-ESP32** | WiFi (eduroam) | Server API | alle 60s + alle 3s |
| **Display-ESP32** | WiFi (HLY-77900) | Server API | alle 60s |
| **Lichtschranken** | I2C | Gate-ESP32 | kontinuierlich (50ms) |
| **Mikrofon** | I2S | Audio-ESP32 | kontinuierlich (16 kHz) |
| **Server-Cron** | lokal | Datenbank | jede Minute |

---

## 🔌 **Verkabelung & Pinouts**

### **Gate-ESP32-C6 (Lichtschranken)**
```
VL53L0X Sensor A (Adresse 0x30):
  SCL → GPIO6, SDA → GPIO7, XSHUT → GPIO2

VL53L0X Sensor B (Adresse 0x29):
  SCL → GPIO6, SDA → GPIO7, XSHUT → GPIO3
```

### **Audio-ESP32-C6 (Mikrofon)**
```
INMP441 I2S Mikrofon:
  SCK (BCLK) → GPIO10
  WS (LRCLK) → GPIO12
  SD (DOUT)  → GPIO11
  GND        → GND
  VCC        → 3.3V
```

### **Display-ESP32-C6 (E-Ink)**
```
GxEPD2 Display (SPI):
  MOSI → GPIO1
  MISO → GPIO2 (optional)
  SCK  → GPIO0
  CS   → GPIO4
  DC   → GPIO5
  RST  → GPIO6
  BUSY → GPIO7
  (Pins können in GxEPD2_display_selection konfiguriert werden)
```

---

## ⚙️ **Wie die Daten verarbeitet werden**

### **Im Server (generate_occupancy_snapshot.php, jede Minute)**

```php
1. Zähle alle IN-Events seit letztem Snapshot
2. Zähle alle OUT-Events seit letztem Snapshot
3. Berechne: people_estimate = net_people
4. Hole letzten Mikrofon-Wert (noise_db)
5. Bestimme Level:
   - LOW  = <6 Personen ODER Lautstärke <50dB
   - MEDIUM = 6-20 Personen ODER Lautstärke 50-65dB
   - HIGH = >20 Personen ODER Lautstärke >65dB
6. Speichere in occupancy_snapshot Tabelle
```

### **Im Display (bildschirm.ino)**

```cpp
1. Jede Minute:
   - Prüfe Zeitfenster (10:00-17:00)
   - Falls außerhalb: Zeige "Keine Daten"
   - Falls innerhalb: Hole aktuellen Snapshot
2. Parse JSON Response
3. Rendere Quadranten mit Farben:
   - Personenzahl (oben links)
   - Auslastung-Level (oben rechts)
   - Lautstärke-Kategorie (unten links)
   - Stoßzeit (unten rechts) [TODO]
4. Aktualisiere E-Ink Display
```

---

## 🔐 **Sicherheit & Authentifizierung**

- **Gate/Audio-ESP32:** API-Key in Sketch hardcoded (`apiKey = "test_key_gate_123456"`)
- **Display-ESP32:** Öffentlicher Zugriff (read-only auf `/occupancy/current`)
- **Cron-Job:** Token-Authentifizierung (nur mit `CRON_SECRET` aus .env)
- **WiFi:** 
  - Gate/Audio: eduroam (WPA2-Enterprise)
  - Display: HLY-77900 (offenes Netz)

---

## 📊 **Zusammenfassung: Was misst was?**

| Komponente | Misst | Speichert in | Update-Frequenz |
|-----------|-------|--------------|-----------------|
| **Lichtschranken** | Durchgänge (IN/OUT) | flow_event | sofort |
| **Mikrofon** | Lautstärke (dB) | reading | alle 60s |
| **Server-Cron** | Auslastungs-Level | occupancy_snapshot | jede Minute |
| **Display** | zeigt an | display_frame | alle 60s |

---

**Stand:** 16. November 2025