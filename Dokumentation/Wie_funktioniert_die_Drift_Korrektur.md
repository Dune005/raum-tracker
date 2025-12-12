# So funktioniert die Drift-Korrektur

---

## Das Problem: Unsere Lichtschranke zählt nicht perfekt


### 1. Schnelle Durchgänge werden verpasst
Wenn zwei Personen gleichzeitig durchgehen, sieht die Lichtschranke nur **eine** Person. Das ist, als würde man beim Zählen blinzeln – man verpasst eine Person.

**Beispiel:** 20 Personen gehen rein → Lichtschranke zählt nur 12

### 2. OUT-Events gehen häufiger verloren als IN-Events
Wenn jemand den Raum verlässt, passiert das oft schneller oder unachtsamer:
- Personen laufen seitlich an der Wand vorbei
- Mehrere gehen gleichzeitig raus (z.B. zum Mittagessen)
- Die Lichtschranke "schläft" kurz

**Ergebnis:** Der Zähler zeigt nach und nach zu viele Personen an – auch wenn der Raum längst leer ist.

### 3. "Geister-Personen" bleiben im Zähler
Am Ende des Tages zeigt der Zähler 5-7 Personen, obwohl niemand mehr da ist. Diese "Geister" sind der Drift – also die Abweichung zwischen gezählten und echten Personen.

---

## Die Lösung: Zwei-Stufen-Korrektur

Unser System korrigiert diese Fehler automatisch mit zwei cleveren Mechanismen:

### ✅ Stufe 1: Drift-Korrektur (Geister eliminieren)

**Das System fragt sich:**
> "Ist der Raum eigentlich leer, aber der Zähler zeigt noch Personen?"

**Wie erkennt es das?**

Das System schaut sich die letzten 30 Minuten an:
- ❌ **Niemand kam rein** (0 IN-Events)
- ✅ **Aber mehrere gingen raus** (mindestens 2 OUT-Events)
- 🔢 **Zähler zeigt nur noch 1-7 Personen**

**Logische Schlussfolgerung:**  
→ Der Raum ist vermutlich leer! Die restlichen Zahlen sind Drift.

**Reaktion:**  
→ Zähler wird auf **0** zurückgesetzt.

#### Beispiel
- **14:30 Uhr:** Zähler zeigt 5 Personen
- **14:31 Uhr:** Eine Person geht raus → Zähler: 4
- **14:45 Uhr:** Noch jemand geht raus → Zähler: 3
- **15:00 Uhr:** System prüft:
  - Seit 30 Minuten niemand reingekommen ❌
  - 2 Personen gingen raus ✅
  - Zähler zeigt nur noch 3 🔢
- **Aktion:** Zähler wird auf **0** gesetzt ✨

---

### ✅ Stufe 2: Skalierung (Unter-Zählung kompensieren)

**Das Problem bei vielen Personen:**  
Bei Stoßzeiten (z.B. Mittagspause) sind 30-40 Personen im Raum, aber die Lichtschranke zählt nur 12-15. Der Fehler wird größer, je voller der Raum ist.

**Die Lösung: Intelligente Hochrechnung**

Das System unterscheidet zwischen:

#### Klein ist genau genug
- **Unter 10 Personen:** Zählung ist meist korrekt
- → Keine Korrektur

#### Groß wird multipliziert
- **Ab 10 Personen:** Systematische Unter-Zählung beginnt
- → Wert wird mit **Faktor 2.0** multipliziert

**Beispiel-Tabelle:**

| Arduino zählt | System rechnet | Display zeigt |
|---------------|----------------|---------------|
| 5 Personen | 5 × 1 | **5 Personen** |
| 9 Personen | 9 × 1 | **9 Personen** |
| 10 Personen | 10 × 2 | **20 Personen** |
| 15 Personen | 15 × 2 | **30 Personen** |
| 25 Personen | 25 × 2 | **50 Personen** |

**Warum ab 10?**  
- Bei wenigen Personen gleichen sich Fehler oft aus (mal +1, mal -1)
- Ab 10 Personen wird der Fehler systematisch (immer zu niedrig)
- Tests haben gezeigt: Schwellenwert 10 ist der beste Kompromiss

---

## Wie arbeiten die beiden Stufen zusammen?

```
┌─────────────────┐
│  Arduino-Board  │  Zählt rohe Events
│  Lichtschranken │  Sendet: counter_raw = 12
└────────┬────────┘
         │
         ↓
┌─────────────────────────────────┐
│  Backend-Server (PHP)           │
│  ───────────────────────────    │
│  1️⃣  Drift-Prüfung              │
│     → Raum leer? Dann auf 0     │
│                                  │
│  2️⃣  Skalierung                 │
│     → Ab 10: × Faktor 2.0        │
│                                  │
│  Ergebnis: display_count = 24   │
└────────┬────────────────────────┘
         │
         ↓
┌─────────────────┐
│  Dashboard &    │  Zeigt korrigierten
│  Display        │  Wert an: 24 Personen
└─────────────────┘
```

---

## Zwei Werte, ein System

Das System arbeitet intern mit **zwei separaten Zahlen**:

### 1. `counter_raw` – Der rohe Zähler
- Kommt direkt vom Arduino
- Kann Drift enthalten
- Kann zu niedrig sein bei vielen Personen
- **Wird NUR für interne Berechnungen verwendet**

### 2. `display_count` – Der korrigierte Wert
- Wurde durch Drift-Korrektur geprüft
- Wurde bei Bedarf hochskaliert
- **Das ist die Zahl, die du siehst!**

**Im Dashboard siehst du beide:**
- Große Zahl = `display_count` (zuverlässig)
- Kleine graue Zahl = `counter_raw` (zum Vergleich)

---

## Warum ist das clever?

### ✅ Vorteile

1. **Keine Hardware-Änderung nötig**  
   Die vorhandenen Sensoren bleiben so, wie sie sind.

2. **Selbstkorrigierend**  
   Das System merkt automatisch, wenn etwas nicht stimmt.

3. **Transparent**  
   Du siehst beide Werte und kannst den Unterschied nachvollziehen.

4. **Anpassbar**  
   Parameter können einfach justiert werden, ohne Arduino neu zu programmieren.

### ⚠️ Grenzen

- Liefert **ungefähre Werte**, keine exakte Personenzahl
- Bei ungewöhnlichen Nutzungsmustern kann Nachjustierung nötig sein
- Funktioniert am besten während regulären Schulzeiten (11:00-15:00 Uhr)

---

## Für Interessierte: Was passiert im Detail?

### Synchronisation zwischen Arduino und Server

**Problem ohne Sync:**
- Arduino zählt: 7 Personen
- Server erkennt Drift und setzt auf 0
- Arduino weiß nichts davon und zählt weiter bei 7 ❌

**Lösung: JSON-Response**
- Arduino sendet: `{"count": 7}`
- Server antwortet: `{"drift_corrected": true, "count": 0}`
- Arduino liest die Antwort und setzt **seinen** Zähler auf 0 ✅

**Resultat:** Beide Systeme sind synchron!

---

## Zusammenfassung

**Unser Raum-Tracker korrigiert automatisch zwei Arten von Fehlern:**

1. **Geister-Personen** (Drift nach oben)  
   → Automatischer Reset auf 0, wenn Raum erkennbar leer ist

2. **Unter-Zählung** (zu niedrige Werte bei vielen Personen)  
   → Hochrechnung mit Faktor 2 ab 10 gezählten Personen

**Das Ergebnis:**  
Statt "12 Personen" (falsch) zeigt das System "24 Personen" (realistisch) – auch wenn die Hardware nicht perfekt zählt.

---

**Stand:** 9. Dezember 2025  
**System-Version:** 2.2

_Diese Anleitung ist bewusst einfach gehalten. Für technische Details siehe [Drift_Korrektur_Dokumentation.md](Drift_Korrektur_Dokumentation.md)_
