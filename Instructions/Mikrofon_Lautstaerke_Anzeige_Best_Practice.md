# Mikrofon-Controller: Lautstärke-Anzeige (Problem + Best Practice)

Datum: 11.12.2025

## Kurzfassung
Unsere aktuelle „dB“-Anzeige (Live + Tageschart) wirkt unplausibel, weil die Werte sehr niedrig bleiben (z.B. Peak um ~34 dB), obwohl es im Raum subjektiv laut ist. Mit einem günstigen I2S/MEMS-Mikrofon (z.B. INMP441/SPH0645) sind **absolute dB-SPL** ohne saubere Kalibrierung meist nicht zuverlässig.

**Empfehlung:**
- Intern weiterhin RMS (oder LAeq/RMS über Zeitfenster) messen.
- Im Dashboard primär **relativen Pegel** anzeigen: $\Delta\mathrm{dB} = \mathrm{Level} - \mathrm{Baseline}$.
- Parallel die Firmware-Messpipeline prüfen/korrekt machen (sehr wahrscheinlich werden „laute“ Samples aktuell zu aggressiv verworfen oder falsch skaliert).
- Wichtig fürs Display (Snapshots): **nicht** das bestehende `noise_db` „umdeuten“, sondern **ein neues Feld (Spalte)** für ΔdB in den Snapshots einführen (z.B. `noise_delta_db`).

---

## Ausgangslage / Symptome
- Live-Ansicht zeigt bei hoher Auslastung nur kleine Ausschläge.
- „dB“-Werte erreichen typischerweise nur ~30–35 dB (Peak ~34 dB), obwohl der Raum laut ist.
- Tageschart musste bereits auf 40 dB max skaliert werden, weil 90 dB nie erreicht wird.

Warum das kritisch ist:
- dB ist logarithmisch. Wenn die Pipeline falsch skaliert ist oder Samples wegschneidet, bleibt die Anzeige dauerhaft „flach“.
- Eine Zahl mit Einheit „dB“ wird von Nutzern als *echter Schallpegel* interpretiert. Wenn das nicht stimmt, leidet die Glaubwürdigkeit.

---

## Wahrscheinliche technische Ursachen (typische Fehlerpunkte)

### 1) Sample-Interpretation / Bit-Alignment (I2S 24-bit in 32-bit Frames)
Viele I2S-MEMS liefern 24-bit Samples in 32-bit Frames (oft linksbündig). Wenn der Shift nicht passt, ist die Amplitude falsch.

**Prüfen:**
- Wird korrekt aus dem 32-bit Frame auf 24-bit Nutzdaten extrahiert?
- Ist das Mikro auf „left only“ / mono korrekt konfiguriert?

### 2) Zu aggressives Verwerfen von Samples (Hard Threshold)
Im aktuellen Ansatz wird häufig mit einer Bedingung wie `abs(sample) < 32000` gefiltert.

Problem:
- Für 24-bit Audio ist `32000` extrem klein.
- Laute Samples werden dann verworfen → RMS bleibt klein → dB bleibt klein.

**Best Practice:**
- Nicht hart so niedrig clippen.
- Wenn überhaupt: „outlier removal“ sehr vorsichtig oder saturating/clamping auf sinnvolles 24-bit Limit.

### 3) Falsche/unklare dB-Konstante (Offset)
Eine Umrechnung von dBFS → dB SPL benötigt eine echte Kalibrierung. Ein fester Offset wie „MIC_OFFSET_DB“ ist ohne Referenz nur eine Annahme.

**Konsequenz:**
- Absolute Werte sind unzuverlässig.

### 4) Glättung / Time Window zu groß oder unpassend
Wenn zu stark geglättet wird, „fühlt“ sich Live träge an.

**Best Practice:**
- Live: RMS-Fenster 250ms–1s + moderate Glättung.
- Historie: 1min LAeq/RMS (glatter, aussagekräftiger).

---

## Best-Practice Lösungsvorschlag (empfohlenes Zielbild)

### A) Was wir im Dashboard anzeigen (verständlich + ehrlich)

#### Primär: Relativer Pegel (ΔdB)
- Definiere „Ruhepegel“ als Baseline $B$ (z.B. Median/RMS in ruhiger Phase nach Boot oder kontinuierlich nachführend).
- Aktueller Pegel $L$ (RMS in dBFS oder pseudo-dB).
- Anzeige: $\Delta\mathrm{dB} = L - B$.

Vorteile:
- „0 dB“ bedeutet: wie Ruhepegel.
- „+6 dB“ bedeutet: deutlich lauter (≈ doppelte Amplitude).
- Funktioniert auch ohne absolute dB-SPL Kalibrierung.

Optional für UI (ohne Missverständnisse):
- Eine Balkenanzeige 0–100% ist ok, **wenn** sie klar definiert ist, z.B. `100% = +12 dB über Baseline`.

#### Sekundär: Kategorien
- `leise` / `mittel` / `laut` basierend auf ΔdB Schwellen, z.B.
  - leise: < +3 dB
  - mittel: +3 bis +8 dB
  - laut: > +8 dB

### B) Was die Firmware liefern soll (sauberer Datenvertrag)

Empfohlenes Payload/State (Beispiel):
- `rms_raw` (oder `rms`) als Debug (optional)
- `level_dbfs` (dBFS) **oder** „pseudo-dB“ konsistent berechnet
- `baseline_dbfs`
- `delta_db` (= level - baseline)
- `quality`/`clipping` Flag: z.B. `is_clipping=true` wenn Samples saturieren

Minimal reicht für Dashboard:
- `delta_db` (Float)

---

### C) Datenhaltung / Snapshots (wichtig, weil das Display Snapshots nutzt)

Ziel: Wir wollen umstellen, ohne bestehende Dashboards/Displays zu „brechen“ und ohne die Bedeutung von bestehenden Feldern zu verändern.

**Empfehlung (sauber & risikoarm): Neue Spalten statt neue Tabelle.**
- In `occupancy_snapshot` eine zusätzliche Spalte anlegen, z.B. `noise_delta_db`.
- Optional (für Debug/Transparenz): `noise_baseline_db` und/oder `noise_level_db`.
- `noise_db` bleibt wie es heute ist (Legacy/kompatibel), bis alles migriert ist.

Warum die optionalen Spalten manchmal Gold wert sind:
- `noise_baseline_db`: Erklärt im Nachhinein *warum* ΔdB klein/groß war. Wenn der „Ruhepegel“ an einem Tag höher ist (Lüftung, Fenster offen, dauerhaftes Grundrauschen), wirkt ΔdB sonst „zu klein“, obwohl die Logik korrekt ist.
- `noise_level_db`: Hilft beim Debugging der Messpipeline. Du siehst sofort, ob das Mikro überhaupt dynamisch misst (Level bewegt sich) oder ob irgendwo Samples/Scaling/Filter „klemmen“.

Minimalstart (voll ok):
- Erstmal nur `noise_delta_db` hinzufügen und verwenden.
- Baseline/Level nur ergänzen, falls ihr bei der Fehlersuche später zu wenig Kontext habt.

Warum *Spalte* statt *neue Tabelle*?
- Snapshot ist der „Single Source of Truth“ fürs Display → eine zusätzliche Spalte ist am einfachsten durchzureichen.
- Keine zusätzliche Join-Logik nötig.
- Historie/Charts können schnell auf das neue Feld umgestellt werden.

Wenn ihr unbedingt trennen wollt (weniger empfohlen): separate Tabelle wie `occupancy_snapshot_audio` ist möglich, verursacht aber mehr Aufwand (Joins/Queries/API-Anpassungen) und hat wenig Mehrwert, solange Audio immer 1:1 zu einem Snapshot gehört.

---

## Konkrete Debug-Checkliste für den Mikrofon-Controller
Ziel: Herausfinden, warum der Ausschlag so klein ist.

1) Rohwerte loggen (Serial)
- min/max Sample im Fenster
- RMS im Fenster
- Anteil der verworfenen Samples (falls Filter aktiv)

2) Filter/Threshold temporär deaktivieren
- Wenn RMS/dB dann deutlich steigt, ist der Threshold die Ursache.

3) Bit-Shift verifizieren
- Varianten testen (z.B. >>8, >>14, >>16) und prüfen, welche realistische Amplituden liefert.
- Bei normaler Sprache in 0.5–2m Abstand sollten RMS-Werte klar über „Noise Floor“ liegen.

4) Clipping erkennen
- Wenn max/min oft am Limit kleben, ist entweder Gain/Scaling zu hoch oder Sample-Interpretation falsch.

5) Baseline-Strategie
- Baseline nicht nur einmalig beim Boot nehmen, sondern robust:
  - z.B. gleitender Median/Quantil (nur wenn Raum „ruhig“)
  - oder Baseline langsam nachführen (sehr langsame Zeitkonstante)

---

## Kalibrierung (nur falls wir wirklich dB-SPL wollen)
Wenn wir absolute dB-SPL wirklich brauchen:
- Referenz-Schallquelle/Kalibrator (94 dB @ 1 kHz)
- Kalibrierkonstante pro Gerät
- optional A-Weighting Filter

Hinweis: Das ist deutlich mehr Aufwand und bleibt trotzdem stark positions- und raumabhängig.

---

## Voraussichtlich betroffene Dateien (bei unserer favorisierten Lösung mit `noise_delta_db`)

Ziel der favorisierten Änderung: Wir führen **eine neue Snapshot-Spalte** `occupancy_snapshot.noise_delta_db` ein und lassen `noise_db` als Legacy/Kompatibilität erstmal bestehen. Damit können Display und Dashboard schrittweise auf ΔdB umgestellt werden.

**1) Datenbank / SQL**
- SQL-Migration/Script (wo ihr es ablegt, ist flexibel; z.B. unter `db/` oder direkt in phpMyAdmin ausführen): `ALTER TABLE occupancy_snapshot ADD COLUMN noise_delta_db ...` (wurde bereits erstellt).

**2) Snapshot-Erzeugung (Cron)**
- `api/cron/generate_occupancy_snapshot.php` (INSERT/UPDATE erweitern, sodass `noise_delta_db` geschrieben wird).

**3) API-Endpoints, die Snapshot-Daten ausliefern**
- `api/v1/display/current.php` (damit das Display `noise_delta_db` bekommt; ideal: neues Feld zusätzlich ausgeben und in der Response behalten, was bisher da war).
- `api/v1/occupancy/current.php` (für Dashboard-Kachel/aktuellen Snapshot).
- `api/v1/occupancy/history.php` (für Tageschart: zusätzlich `AVG(noise_delta_db)` liefern, z.B. als `noise_delta_db_avg`).

**4) Arduino Display-Firmware (Interpretation der Werte)**
- `arduino/aktuelle_codes/display.ino`
- `arduino/aktuelle_codes/display_281125.ino`

Hier wird aktuell `noise_db` als „dB“ angezeigt und in Kategorien eingeteilt (Schwellen wie 50/65/80). Bei ΔdB müssen Label + Schwellen neu definiert werden (z.B. 0/3/8/12).

**5) Web-Dashboard**
- `index.html` (Tageschart + Tooltips: statt `noise_db_avg` künftig `noise_delta_db_avg` verwenden und Achsen-/Tooltip-Label auf „ΔdB“ anpassen).

**6) Optional / je nach Nutzung**
- `api/v1/statistics/today.php` und `api/v1/statistics/week.php` (falls ihr dort „Noise“-Aggregationen nutzt und diese künftig auch auf ΔdB basieren sollen).
- `update_count.php` (nur wenn ihr die Live-Kachel ebenfalls auf ΔdB umstellen wollt; das Display nutzt Snapshots, nicht diesen Live-Feed).
- Dokumentation: `Anleitungen/API_Documentation.md` / `Anleitungen/API_Endpoints_Kompakt.md` (wenn ihr den neuen Response-Key dokumentieren wollt).

Faustregel: **Minimum** für „Display + Tageschart funktionieren mit ΔdB“ ist DB-Spalte + Cron + (display/current + occupancy/history) + Display-Sketch + `index.html`.

---

## Empfehlung: Nächste Schritte (für Aaron mit dem aktuellen Mikrofon-Sketch)
1) Messpipeline prüfen: Threshold/Bit-Shift/Scaling → RMS muss sichtbar auf „laut“ reagieren.
2) Output erweitern: `delta_db` (und idealerweise `baseline`/`level`) berechnen und bereitstellen.
3) DB/Snapshots erweitern: In `occupancy_snapshot` neue Spalte `noise_delta_db` (Baseline/Level nur optional).
4) Snapshot-Generator so anpassen, dass er `noise_delta_db` schreibt.
5) Display + Dashboard umstellen, dass sie bevorzugt `noise_delta_db` anzeigen (mit korrektem Label „ΔdB“).

Wenn wir das so machen, bekommen wir:
- stärkeren, nachvollziehbaren Live-Ausschlag
- weniger Missverständnisse bei der Interpretation
- robuste Werte trotz günstiger Sensorik
