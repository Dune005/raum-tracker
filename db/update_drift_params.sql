-- ==========================================
-- Drift-Parameter anpassen
-- ==========================================
-- 
-- Dieses Script hilft dir, die Drift-Korrektur-Parameter
-- flexibel anzupassen, ohne Code zu ändern.
--
-- Verwendung:
-- 1. Werte unten anpassen
-- 2. In phpMyAdmin ausführen
-- 3. Änderungen sind sofort aktiv (kein Server-Neustart)
--

-- Space-ID für IM5 Aufenthaltsraum
SET @space_id = '880e8400-e29b-41d4-a716-446655440001';

-- ==========================================
-- WICHTIGE PARAMETER ZUM ANPASSEN
-- ==========================================

-- scale_factor: Multiplikator für Skalierung bei hohen Werten
-- Aktuell: 1.5 (konservativ)
-- Empfohlen: 1.3 - 2.0
-- Beispiel: Bei 15 Personen gezählt → 15 * 1.5 = 22 angezeigt
SET @scale_factor = 1.5;

-- scale_threshold: Ab wann Skalierung aktiviert wird
-- Aktuell: 15 Personen
-- Empfohlen: 10 - 20
SET @scale_threshold = 15;

-- out_event_multiplier: Gewichtung für OUT-Events
-- Aktuell: 1.3 (OUT-Events zählen 1.3x so viel wie IN-Events)
-- Empfohlen: 1.2 - 1.5
-- Beispiel: 1 OUT-Event zieht 1.3 vom Counter ab statt nur 1
-- ⚠️ WICHTIG: Dieser Wert hilft, den positiven Drift zu reduzieren!
SET @out_event_multiplier = 2.0;

-- drift_max: Maximaler Counter-Wert für Drift-Erkennung
-- Aktuell: 7
-- Empfohlen: 5 - 10
SET @drift_max = 7;

-- drift_window_minutes: Zeitfenster für Event-Analyse
-- Aktuell: 30 Minuten
-- Empfohlen: 20 - 45
SET @drift_window_minutes = 30;

-- ==========================================
-- SQL-UPDATE (nicht ändern!)
-- ==========================================

UPDATE threshold_profile 
SET drift_config = JSON_SET(
    COALESCE(drift_config, '{}'),
    '$.scale_factor', @scale_factor,
    '$.scale_threshold', @scale_threshold,
    '$.out_event_multiplier', @out_event_multiplier,
    '$.drift_max', @drift_max,
    '$.drift_window_minutes', @drift_window_minutes
)
WHERE space_id = @space_id;

-- Verifizierung: Zeige aktuelle Werte an
SELECT 
    space_id,
    JSON_EXTRACT(drift_config, '$.scale_factor') AS scale_factor,
    JSON_EXTRACT(drift_config, '$.scale_threshold') AS scale_threshold,
    JSON_EXTRACT(drift_config, '$.out_event_multiplier') AS out_event_multiplier,
    JSON_EXTRACT(drift_config, '$.drift_max') AS drift_max,
    JSON_EXTRACT(drift_config, '$.drift_window_minutes') AS drift_window_minutes
FROM threshold_profile
WHERE space_id = @space_id;
