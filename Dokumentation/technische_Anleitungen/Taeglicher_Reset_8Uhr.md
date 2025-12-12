# Täglicher Automatischer Reset um 8:00 Uhr

## Übersicht

Das System führt **täglich um 8:00 Uhr** einen automatischen Reset durch, um Drift-Fehler zu korrigieren und mit frischen Zählern zu starten.

## Was wird zurückgesetzt?

### 1. Arduino-Counter (`counter_raw`)
- Der lokale Zähler auf dem ESP32 wird auf 0 gesetzt
- Variable: `count` in der Firmware

### 2. Datenbank-Werte
- `counter_state.counter_raw` → 0
- `counter_state.display_count` → 0
- Über `update_count.php` mit Parameter `device_reset=true`

## Implementierung

### Arduino-Seite (raum_tracker_lichtschranke_101225.ino)

```cpp
// Neue globale Variable
int lastResetDay = -1;  // Track letzter Reset-Tag (tm_mday)

// Prüfung in loop()
if (hour == 8 && minute == 0 && currentDay != lastResetDay) {
    performDailyReset();
    lastResetDay = currentDay;  // Markiere Tag als resettet
}

// Reset-Funktion
void performDailyReset() {
    // 1. Arduino-Counter zurücksetzen
    count = 0;
    
    // 2. Server-Reset (3x für Sicherheit)
    for (int attempt = 1; attempt <= 3; attempt++) {
        sendResetSignal();
        if (attempt < 3) delay(1000);
    }
}
```

### Server-Seite (update_count.php)

```php
// POST: count=0&direction=RESET&device_reset=true
if (isset($_POST['device_reset']) && $_POST['device_reset'] === 'true') {
    // Update counter_state in DB
    UPDATE counter_state 
    SET counter_raw = 0, 
        display_count = 0,
        last_update = NOW()
    WHERE space_id = :space_id
    
    // Update JSON-File
    $data['count'] = 0;
    $data['display_count'] = 0;
    $data['direction'] = 'RESET';
}
```

## Ablauf

1. **08:00:00 Uhr**: Arduino erkennt Reset-Zeit
2. **Arduino**: Setzt lokalen `count` auf 0
3. **Arduino**: Sendet 3x Reset-Signal an Server
4. **Server**: Setzt `counter_state` in DB auf 0
5. **Server**: Setzt `counter_data.json` auf 0
6. **Dashboard**: Zeigt 0 Personen an

## Vorteile

- **Täglicher Neustart**: Korrigiert akkumulierte Drift-Fehler
- **Konsistenz**: Arduino und Datenbank sind synchron
- **Zuverlässigkeit**: 3x Sendeversuch für Robustheit
- **Keine manuelle Intervention**: Vollautomatisch

## NTP-Synchronisation

**Wichtig**: Der Reset funktioniert nur wenn:
- NTP-Zeit synchronisiert ist (`time(nullptr) > 1577836800`)
- `getLocalTime(&timeinfo)` erfolgreich ist
- Timestamp > 01.01.2020

## Logging

Arduino-Ausgabe:
```
⏰ 8:00 Uhr erreicht - Starte täglichen Reset...
  📊 Arduino Counter: 5 → 0
  Versuch 1/3: 🔄 Sende Reset-Signal an Server... ✅ 200 (123ms)
  Versuch 2/3: 🔄 Sende Reset-Signal an Server... ✅ 200 (98ms)
  Versuch 3/3: 🔄 Sende Reset-Signal an Server... ✅ 200 (102ms)
✅ Täglicher Reset abgeschlossen
```

Server-Log (`error_log`):
```
Arduino-Reset erkannt - Counter-State zurückgesetzt für Space: 880e8400-...
  Alte Werte: counter_raw=5, display_count=5
  SQL-Update: erfolgreich, Rows affected: 1
```

## Zeitfenster-Blockierung

Der Reset findet **vor** der Zeitfenster-Blockierung statt:
- **23:00-06:00 Uhr**: Sensoren deaktiviert (Nachtmodus)
- **08:00 Uhr**: Reset durchgeführt
- **06:00-23:00 Uhr**: Normale Messung

## Manuelle Resets

Neben dem täglichen Reset gibt es auch:
- **Arduino-Neustart**: Sendet automatisch Reset-Signal beim Booten
- **Drift-Korrektur**: Automatisch bei erkanntem Drift (siehe `DriftCorrector.php`)

---

**Stand:** 10. Dezember 2025  
**Version:** 1.0
