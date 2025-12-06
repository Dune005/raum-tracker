# Raum-Tracker – Drift-Problem & Software-Korrektur ohne Hardware-Anpassung

## 1. Kontext

Das System misst die Auslastung eines Raums über Lichtschranken am Eingang.  
Gezählt werden **IN**- und **OUT**-Events, daraus entsteht ein Zähler:

```text
counter_raw = counter_raw + IN - OUT
```

`counter_raw` wird im Dashboard (direkt oder indirekt) als aktuelle Personenzahl angezeigt.

Es gibt **keinen** Bewegungsmelder (PIR).  
Drift und Untererfassung sollen ausschliesslich im Code behandelt werden.

---

## 2. Beobachtungen aus Stichproben

Aus mehreren Tagen Stichproben ergeben sich folgende Muster:

### 2.1 Untererfassung zu Peak-Zeiten (Mittag)

Beispiele (getrackt → effektiv):

- Montag: 12 → ~40  
- Dienstag: 16 → ~35–40  
- Mittwoch: 16 → ~45  
- Donnerstag Mittag: 13 → 19, 21 → 37, 22 → 28  

Tendenz:

- Bei 30–45 realen Personen liegt der Zähler häufig nur bei 12–22.
- Untererfassung liegt ungefähr im Bereich Faktor **1,5–3**, an einzelnen Tagen auch darüber.

### 2.2 Positiver Drift am Nachmittag (Raum leer)

Mehrfach:

- Zähler zeigt 5–7 Personen, obwohl der Raum ziemlich sicher leer ist.  
- Interpretation: **OUT-Events werden systematisch schlechter erfasst als IN-Events.**  
  → Über den Tag entsteht ein positiver Offset („Geisterpersonen“).

---

## 3. Fehlerbild

Aus den Stichproben lassen sich zwei zentrale Fehlerarten ableiten:

1. **Skalierungsfehler (Gain):**  
   - Bei hoher Auslastung wird nur ein Teil der Personen erkannt.  
   - `counter_raw` liegt deutlich unter der realen Personenzahl (z. B. 16 vs. 40).

2. **Offset-Fehler (Drift):**  
   - Am Ende einer Nutzungsphase bleibt ein Rest von 5–7 Personen im Zähler, obwohl der Raum leer ist.  
   - Grund: OUT-Events werden häufiger „verpasst“ als IN-Events.

Für den Use Case reicht eine **grob korrekte Grössenordnung** („ca. 40 statt 10“), keine exakte Zählung.

---

## 4. Ziel

Ohne Hardware-Änderung soll:

- die **Untererfassung bei hoher Auslastung** abgeschwächt  
- der **Drift (Rest von 5–7 Personen)** reduziert  
- die Anzeige auf eine **realistische Grössenordnung** gebracht werden

Das System darf weiterhin nur IN/OUT-Events der bestehenden Sensorik nutzen.

---

## 5. Grundidee im Code

Die Logik trennt zwei Ebenen:

1. **`counter_raw`**  
   - rein eventbasiert: `+IN`, `-OUT`, geklammert auf `>= 0`  
   - repräsentiert den „internen“ Zustand inkl. Drift

2. **`display_count`**  
   - berechnete, „bereinigte“ Anzeige für das Dashboard  
   - basiert auf `counter_raw`, wird aber:
     - bei Bedarf **skalisiert** (Gain)  
     - durch **Heuristiken** gegen Drift korrigiert  
     - mit Min/Max begrenzt

---

## 6. Drift-Korrektur im Code (Offset-Problem)

Ziel: den typischen Rest von **5–7 Personen** am Ende einer Nutzungsphase korrigieren, ohne zusätzliche Hardware.

### 6.1 Heuristische Annahme „Raum leer“

Der Raum gilt als **wahrscheinlich leer**, wenn:

- längere Zeit keine oder kaum IN-Events auftreten  
- und `counter_raw` im typischen Fehlerbereich liegt (z. B. `<= 7`)  
- und im relevanten Zeitfenster überwiegend OUT-Events registriert wurden

Mögliche Kriterien:

- `no_recent_in_window`:  
  - in den letzten `T` Minuten (z. B. 20–30) **keine** IN-Events  
- `more_out_than_in_window`:  
  - im gleichen Zeitfenster mehr OUT- als IN-Events  
- `counter_raw <= DRIFT_MAX`:  
  - z. B. `DRIFT_MAX = 7`

Wenn alle Bedingungen erfüllt sind, kann `counter_raw` auf 0 gesetzt werden.

### 6.2 Beispiel-Heuristik (sprachneutraler Pseudocode)

```pseudo
CONST DRIFT_MAX = 7
CONST DRIFT_WINDOW_MINUTES = 30

function correctDrift(state, now):
    // state enthält:
    // - counter_raw
    // - history der IN/OUT-Events mit Timestamp

    stats = getEventStatsInWindow(state.history, now, DRIFT_WINDOW_MINUTES)

    if stats.in_events == 0 and stats.out_events > 0 and state.counter_raw <= DRIFT_MAX:
        state.counter_raw = 0

    if state.counter_raw < 0:
        state.counter_raw = 0

    return state
```

Die Funktion `getEventStatsInWindow(...)` liefert z. B.:

- `in_events` in den letzten `DRIFT_WINDOW_MINUTES`
- `out_events` im gleichen Zeitraum

---

## 7. Skalierung bei hoher Auslastung (Gain-Problem)

Ziel: bei vielen Personen im Raum die Anzeige näher an die reale Grössenordnung bringen.  
Beobachtung: Für kleine Werte (0–9) ist `counter_raw` oft relativ brauchbar, bei hohen Werten deutlich zu tief.

### 7.1 Schwellenbasierte Skalierung

Ansatz:

- Für kleinere `counter_raw`-Werte **keine Skalierung**, um leere/fast leere Situationen nicht zu verfälschen.
- Ab einer bestimmten Schwelle (z. B. `>= 10`) einen Faktor anwenden.

Beispiel:

```pseudo
CONST SCALE_THRESHOLD = 10
CONST SCALE_FACTOR = 2.0      // Startwert, Feintuning über weitere Beobachtungen
CONST MAX_CAPACITY = 60       // maximale sinnvolle Raumkapazität

function computeDisplayCount(counter_raw):
    if counter_raw >= SCALE_THRESHOLD:
        display = round(counter_raw * SCALE_FACTOR)
    else:
        display = counter_raw

    // Begrenzen
    if display < 0:
        display = 0

    if display > MAX_CAPACITY:
        display = MAX_CAPACITY

    return display
```

Hinweis:

- `SCALE_FACTOR` ist bewusst konservativ gewählt (2.0).  
  Je nach weiteren Stichproben kann der Faktor erhöht oder reduziert werden (z. B. 1.8, 2.3, 2.5).

### 7.2 Erweiterung: unterschiedliche Faktoren für IN und OUT (optional)

Falls später detaillierte Logs vorliegen (`IN_total`, `OUT_total`, reale Beobachtungen), kann der Agent getrennte Faktoren bestimmen:

- `FACTOR_IN = real_in_total / measured_in_total`
- `FACTOR_OUT = real_out_total / measured_out_total`

und dann:

```pseudo
counter_raw += in_events  * FACTOR_IN
counter_raw -= out_events * FACTOR_OUT
```

Diese Variante reduziert Drift zusätzlich, ist aber abhängig von guten Logdaten.  
Für den Einstieg reicht meist eine einfache Schwellen-Skalierung wie in 7.1.

---

## 8. Gesamtablauf (für den Agenten)

Der Agent kann die Logik wie folgt strukturieren:

```pseudo
// 1. Rohzähler aktualisieren
function updateCounter(state, in_events, out_events):
    state.counter_raw += in_events
    state.counter_raw -= out_events

    if state.counter_raw < 0:
        state.counter_raw = 0

    // Event-Historie aktualisieren (für Drift-Analyse)
    appendEventsToHistory(state.history, in_events, out_events)

    return state

// 2. Drift-Korrektur anwenden
function applyDriftCorrection(state, now):
    state = correctDrift(state, now)   // siehe Abschnitt 6.2
    return state

// 3. Anzeige berechnen
function getDisplayCount(state):
    return computeDisplayCount(state.counter_raw)  // siehe Abschnitt 7.1
```

Typischer Zyklus pro Request/Minute/Loop:

1. `updateCounter(...)` mit neuen IN/OUT-Events aufrufen  
2. `applyDriftCorrection(...)` mit aktuellem Zeitpunkt aufrufen  
3. `getDisplayCount(...)` berechnen und im Dashboard anzeigen

---

## 9. Grenzen der Lösung

- Die Logik liefert nur eine **Näherung**, keine exakte Personenzahl.  
- Die Faktoren (`SCALE_FACTOR`, `DRIFT_MAX`, `DRIFT_WINDOW_MINUTES`) sollten anhand weiterer Beobachtungen iterativ angepasst werden.  
- Bei stark veränderter Nutzung (z. B. andere Türsituation, neues Verhalten) kann eine Neukalibrierung notwendig werden.

Für den aktuellen Use Case („ungefähre Auslastung, nicht exakte Zählung“) ist dieses Vorgehen aber ausreichend und ohne Hardwareänderung realisierbar.

###


# ✅ diese Logik gehört *nicht* in den Arduino-Sketch. 
# ✅ **Die Anpassungen passieren im Backend (PHP / Server / App-Logik).**

Ich erkläre dir kurz, warum:

---

# 1. Was der Arduino machen sollte (und was nicht)

Der Arduino ist nur für **einfache, rohe Event-Detektion** zuständig:

- Lichtschranke wurde unterbrochen → `IN++`  
- Lichtschranke wurde rückwärts unterbrochen → `OUT++`

Der Controller sollte also möglichst:

- **gar nichts rechnen**  
- **keine Zähler korrigieren**  
- **keine Drifts managen**  
- **keine Faktoren anwenden**

Warum?

### ➤ Der Arduino hat keinen Überblick über:
- Zeitfenster (z. B. „20 min keine IN-Events“)  
- historische Daten  
- komplexe Drift-Muster  
- Skalierungslogik (Faktor 2, Faktor 2.5 etc.)  
- Reset-Bedingungen  
- Tagesverhalten  

Der Microcontroller liefert nur Live-Impulse – **mehr nicht**.

---

# 2. Wo die Korrektur tatsächlich passieren muss

## 🟦 **Die Drift-Korrektur und Skalierung passieren im Backend**, also:

- in eurem PHP-Script  
- oder in der API, welche die Daten entgegennimmt  
- oder im Server, der die Daten speichert und verarbeitet  
- oder im Dashboard-Backend, das die Zahlen anzeigt

Das Backend hat:

- Zugriff auf die komplette Event-Historie  
- Zeitstempel  
- Sessions / Tageszyklen  
- die Fähigkeit, Heuristiken auszuwerten  
- die Möglichkeit, Faktoren dynamisch anzupassen  
- viel Rechenpower

**Dort gehört die Logik hin.**

---

# 3. Rolle des Arduino (Minimalprinzip)

**Arduino macht:**

```
detektiere Event
schicke IN oder OUT an Server
```

Mehr nicht.

Warum?

- Modularität  
- Stabilität  
- Wiederholbarkeit  
- Einfaches Firmware-Update  
- Code-Agent kann Backend viel leichter anpassen als den Sketch  
- Eventlogs müssen zentral verarbeitet werden, nicht dezentral am Sensor

---

# 4. Datenfluss im Gesamtsystem (übersichtlich)

```
Arduino → sendet Roh-Events → Backend  
Backend → updateCounter() → DriftCorrection() → computeDisplayCount()  
Dashboard → zeigt bereinigten Wert an
```

Arduino „weiss“ nie, wie viele Leute im Raum sind.  
Er darf es auch nicht wissen, sonst wird alles unkontrollierbar.

---

# 5. Wichtig: Warum der Arduino den Drift *nicht* lösen kann

Die Driftkorrektur basiert auf Bedingungen wie:

- „Letzte 20 Minuten keine IN-Events“  
- „Mehr OUT als IN im Zeitfenster“  
- „counter_raw <= 7“

Der Arduino hat:

- keinen Speicher für Event-Historien  
- keine Zeitfensterlogik  
- keine realen Timestamps  
- keine Persistenz über Reboots  
- begrenzten Speicher und CPU  

Der Driftalgorithmus wäre auf dem Controller unsauber, fehleranfällig und schwer debugbar.  
Im Backend ist er robust und flexibel.

---

# 6. Konkrete Antwort auf deine Frage

**Die Anpassungen passieren in eurem Backend, nicht im Arduino-Sketch.**

Du musst also:

- **den Sketch NICHT ändern**  
- im Backend die neuen Funktionen einbauen:
  - `updateCounter()`
  - `applyDriftCorrection()`
  - `computeDisplayCount()`