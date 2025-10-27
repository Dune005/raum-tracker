# Datenbank-Tabellen – Einfach erklärt

> Eine Übersicht aller Tabellen im Raum-Tracker-Projekt in verständlicher Sprache

---

## 📍 **space**
**"Der Raum, den wir überwachen"**

Hier steht drin, welcher Raum überwacht wird (z.B. "Aufenthaltsraum IM5"). Jeder Raum hat einen Namen und eine Beschreibung. Alle anderen Daten (Sensoren, Geräte, Auslastung) sind diesem Raum zugeordnet.

**Beispiel:** "Aufenthaltsraum IM5 - Hauptraum für Studierende"

---

## 🔌 **device**
**"Die physischen Geräte im System"**

Hier werden alle Hardware-Geräte registriert, die am System angeschlossen sind:
- **Mikrocontroller** (ESP32) - messen die Daten
- **Display** (E-Ink) - zeigt die Auslastung an

Jedes Gerät hat einen eindeutigen API-Schlüssel zur Sicherheit und speichert, wann es zuletzt online war.

**Beispiel:** "ESP32-Mikrocontroller am Eingang" oder "E-Ink-Display im Foyer"

---

## 📡 **sensor**
**"Die einzelnen Messgeräte"**

Ein Sensor ist eine konkrete Messquelle an einem Gerät. Das können sein:
- **Lichtschranken** (erkennen Personen)
- **Mikrofon** (misst Lautstärke)
- **Bewegungsmelder/PIR** (erkennt Bewegung)
- **Distanzmesser** (misst Abstand)

Jeder Sensor gehört zu einem Gerät und hat eine bestimmte Aufgabe.

**Beispiel:** "Lichtschranke A am Eingang links" oder "Mikrofon im Raum-Zentrum"

---

## 🚪 **gate**
**"Der Durchgang mit Richtungserkennung"**

Ein Gate besteht aus **zwei Lichtschranken** (Sensor A und B), die zusammen erkennen können, ob jemand **rein** oder **raus** geht. Durch die Reihenfolge der Unterbrechungen (erst A dann B = rein, erst B dann A = raus) kann die Richtung bestimmt werden.

**Beispiel:** "Haupteingang Treppe" mit Lichtschranke A und B

---

## 📊 **reading**
**"Alle Rohmessungen der Sensoren"**

Hier werden alle einzelnen Messungen gespeichert, die die Sensoren durchführen:
- Lautstärke in Dezibel
- Bewegungserkennungen (ja/nein)
- Distanzmessungen in Millimetern

Diese Tabelle ist wie ein Logbuch - jede einzelne Messung wird mit Zeitstempel aufgezeichnet.

**Beispiel:** "Um 14:23:15 Uhr hat das Mikrofon 65 dB gemessen"

---

## 🚶 **flow_event**
**"Wer geht rein oder raus?"**

Jedes Mal, wenn jemand durch ein Gate geht, wird hier ein Ereignis gespeichert:
- **IN** = jemand ist reingegangen
- **OUT** = jemand ist rausgegangen

Diese Events sind die Basis für die Personenzählung. Wenn wir alle IN minus alle OUT rechnen, wissen wir, wie viele Personen im Raum sind.

**Beispiel:** "Um 14:25 Uhr ist 1 Person durch den Haupteingang rein (IN)"

---

## 👥 **occupancy_snapshot**
**"Wie voll ist der Raum gerade?"**

Diese Tabelle enthält die **berechnete Auslastung** zu verschiedenen Zeitpunkten. Hier wird kombiniert:
- Personenzählung (aus flow_events)
- Lautstärke (aus Mikrofon)
- Bewegungen (aus PIR)

Das Ergebnis ist ein **Level**:
- **LOW** = wenig los
- **MEDIUM** = mittelmäßig besucht
- **HIGH** = sehr voll

**Das ist die wichtigste Tabelle für die Anzeige!**

**Beispiel:** "Um 14:30 Uhr: 12 Personen geschätzt, Level: MEDIUM, 58 dB Lautstärke"

---

## ⚙️ **threshold_profile**
**"Wann gilt als 'voll'?"**

Hier werden die Grenzen definiert, ab wann ein Raum als LOW, MEDIUM oder HIGH gilt:
- Bei welcher Lautstärke ist es "voll"?
- Wie viele Bewegungen bedeuten "viel los"?
- Wie gewichten wir die verschiedenen Faktoren?

Diese Werte können angepasst werden, ohne den Code zu ändern.

**Beispiel:** "Ab 60 dB gilt es als MEDIUM, ab 75 dB als HIGH"

---

## 🖼️ **display_frame**
**"Was zeigt das Display an?"**

Jedes Mal, wenn das E-Ink-Display aktualisiert wird, wird hier gespeichert:
- Was wurde angezeigt? (Text, Icon, Farbe)
- Wurde die Anzeige bestätigt?
- War die Übertragung erfolgreich?

**Beispiel:** "Um 14:30 zeigt das Display: '🟡 Mittelmäßig besucht' (Status: bestätigt)"

---

## 💓 **heartbeat**
**"Lebt das Gerät noch?"**

Jedes Gerät sendet regelmäßig ein "Lebenszeichen":
- Wann war es zuletzt online?
- Wie gut ist die WLAN-Verbindung (RSSI)?
- Zusatzinfos wie Temperatur, Speicher, Laufzeit

So können wir überwachen, ob alle Geräte funktionieren.

**Beispiel:** "ESP32 am Eingang: zuletzt online vor 2 Sekunden, WLAN-Signal: -45 dBm (gut)"

---

## 🔧 **calibration**
**"Wann wurde was kalibriert?"**

Wenn ein Sensor neu kalibriert wird (z.B. Lichtschranke justiert, Mikrofon angepasst), wird das hier dokumentiert. So können wir später nachvollziehen, warum sich Messwerte geändert haben.

**Beispiel:** "Am 22.10.2025 wurde Lichtschranke A neu ausgerichtet (Abstand: 800mm)"

---

## 📈 **v_people_net_today** (View)
**"Wie viele Leute sind heute netto reingekommen?"**

Dies ist keine echte Tabelle, sondern eine **automatisch berechnete Ansicht**:
- Zählt alle IN-Events von heute
- Zieht alle OUT-Events ab
- Ergebnis = Netto-Personenzahl seit Mitternacht

**Beispiel:** "Heute sind netto 45 Personen reingekommen (IN: 67, OUT: 22)"

---

## 🔄 Wie hängt alles zusammen?

```
1. SENSOR misst Daten → speichert in READING
2. GATE erkennt Person → erstellt FLOW_EVENT (IN/OUT)
3. Server berechnet regelmäßig → erstellt OCCUPANCY_SNAPSHOT
4. Display fragt ab → zeigt OCCUPANCY_SNAPSHOT an → speichert DISPLAY_FRAME
5. Alle Geräte senden → HEARTBEAT (Lebenszeichen)
```

**Kurz gesagt:**
- **Sensoren** messen
- **Flow-Events** zählen Personen
- **Snapshots** berechnen Auslastung
- **Display** zeigt es an
- **Heartbeat** überwacht alles

---

**Stand:** 23. Oktober 2025