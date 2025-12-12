-- =====================================================
-- Drift-Korrektur Migration Script
-- Projekt: Raum-Tracker IM5
-- Version: 1.0
-- Datum: 6. Dezember 2025
-- =====================================================

-- Setze Character Set und Timezone
SET NAMES utf8mb4;
SET time_zone = '+01:00';

-- =====================================================
-- 1. NEUE TABELLE: counter_state
-- =====================================================

CREATE TABLE IF NOT EXISTS counter_state (
    id INT PRIMARY KEY AUTO_INCREMENT,
    space_id CHAR(36) NOT NULL,
    counter_raw INT NOT NULL DEFAULT 0 COMMENT 'Roher Zählerstand vom Arduino (kann driften)',
    display_count INT NOT NULL DEFAULT 0 COMMENT 'Korrigierter Wert für Anzeige (nach Drift-Korrektur + Skalierung)',
    last_in_event DATETIME NULL COMMENT 'Zeitstempel des letzten IN-Events',
    last_out_event DATETIME NULL COMMENT 'Zeitstempel des letzten OUT-Events',
    in_events_today INT NOT NULL DEFAULT 0 COMMENT 'Kumulative IN-Events heute',
    out_events_today INT NOT NULL DEFAULT 0 COMMENT 'Kumulative OUT-Events heute',
    drift_corrections_today INT NOT NULL DEFAULT 0 COMMENT 'Anzahl automatischer Drift-Korrekturen heute',
    last_drift_correction DATETIME NULL COMMENT 'Zeitpunkt der letzten Drift-Korrektur',
    last_update DATETIME NOT NULL COMMENT 'Letztes Update von Arduino oder Cron-Job',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    FOREIGN KEY (space_id) REFERENCES space(id) ON DELETE CASCADE,
    UNIQUE KEY unique_space (space_id),
    INDEX idx_last_in_event (last_in_event),
    INDEX idx_last_out_event (last_out_event),
    INDEX idx_last_update (last_update)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci 
COMMENT='Persistenter Counter-State mit Event-Historie für Drift-Tracking';

-- =====================================================
-- 2. INITIAL-DATEN: counter_state befüllen
-- =====================================================

-- Füge Eintrag für Aufenthaltsraum IM5 ein
INSERT INTO counter_state (space_id, counter_raw, display_count, last_update)
SELECT id, 0, 0, NOW()
FROM space
WHERE id = '880e8400-e29b-41d4-a716-446655440001'
ON DUPLICATE KEY UPDATE 
    counter_raw = counter_raw,  -- Behalte existierenden Wert
    display_count = display_count;

-- =====================================================
-- 3. THRESHOLD_PROFILE: drift_config Spalte hinzufügen
-- =====================================================

-- Prüfe ob Spalte bereits existiert, sonst hinzufügen
SET @dbname = DATABASE();
SET @tablename = 'threshold_profile';
SET @columnname = 'drift_config';
SET @preparedStatement = (SELECT IF(
    (
        SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
        WHERE 
            TABLE_SCHEMA = @dbname
            AND TABLE_NAME = @tablename
            AND COLUMN_NAME = @columnname
    ) > 0,
    'SELECT 1',
    CONCAT('ALTER TABLE ', @tablename, ' ADD COLUMN ', @columnname, ' JSON DEFAULT NULL COMMENT ''Drift-Korrektur Parameter (drift_max, window_minutes, scale_factor)''')
));
PREPARE alterIfNotExists FROM @preparedStatement;
EXECUTE alterIfNotExists;
DEALLOCATE PREPARE alterIfNotExists;

-- =====================================================
-- 4. THRESHOLD_PROFILE: Drift-Config Werte setzen
-- =====================================================

-- Standard-Werte für Aufenthaltsraum IM5
UPDATE threshold_profile 
SET drift_config = JSON_OBJECT(
    'drift_max', 7,
    'drift_window_minutes', 30,
    'min_out_events_for_reset', 2,
    'scale_threshold', 10,
    'scale_factor', 2.0,
    'max_capacity', 60,
    'min_correction_interval_minutes', 5
) 
WHERE space_id = '880e8400-e29b-41d4-a716-446655440001' 
AND is_default = 1
AND (drift_config IS NULL OR JSON_LENGTH(drift_config) = 0);

-- =====================================================
-- 5. OCCUPANCY_SNAPSHOT: Neue Spalten hinzufügen
-- =====================================================

-- counter_raw Spalte
SET @columnname = 'counter_raw';
SET @preparedStatement = (SELECT IF(
    (
        SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
        WHERE 
            TABLE_SCHEMA = @dbname
            AND TABLE_NAME = 'occupancy_snapshot'
            AND COLUMN_NAME = @columnname
    ) > 0,
    'SELECT 1',
    'ALTER TABLE occupancy_snapshot ADD COLUMN counter_raw INT NULL COMMENT ''Roher Zählerstand vom Arduino'' AFTER people_estimate'
));
PREPARE alterIfNotExists FROM @preparedStatement;
EXECUTE alterIfNotExists;
DEALLOCATE PREPARE alterIfNotExists;

-- display_count Spalte
SET @columnname = 'display_count';
SET @preparedStatement = (SELECT IF(
    (
        SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
        WHERE 
            TABLE_SCHEMA = @dbname
            AND TABLE_NAME = 'occupancy_snapshot'
            AND COLUMN_NAME = @columnname
    ) > 0,
    'SELECT 1',
    'ALTER TABLE occupancy_snapshot ADD COLUMN display_count INT NULL COMMENT ''Korrigierter Wert nach Drift-Korrektur + Skalierung'' AFTER counter_raw'
));
PREPARE alterIfNotExists FROM @preparedStatement;
EXECUTE alterIfNotExists;
DEALLOCATE PREPARE alterIfNotExists;

-- drift_corrected Spalte
SET @columnname = 'drift_corrected';
SET @preparedStatement = (SELECT IF(
    (
        SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
        WHERE 
            TABLE_SCHEMA = @dbname
            AND TABLE_NAME = 'occupancy_snapshot'
            AND COLUMN_NAME = @columnname
    ) > 0,
    'SELECT 1',
    'ALTER TABLE occupancy_snapshot ADD COLUMN drift_corrected BOOLEAN DEFAULT FALSE COMMENT ''Wurde Drift korrigiert?'' AFTER display_count'
));
PREPARE alterIfNotExists FROM @preparedStatement;
EXECUTE alterIfNotExists;
DEALLOCATE PREPARE alterIfNotExists;

-- scale_applied Spalte
SET @columnname = 'scale_applied';
SET @preparedStatement = (SELECT IF(
    (
        SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
        WHERE 
            TABLE_SCHEMA = @dbname
            AND TABLE_NAME = 'occupancy_snapshot'
            AND COLUMN_NAME = @columnname
    ) > 0,
    'SELECT 1',
    'ALTER TABLE occupancy_snapshot ADD COLUMN scale_applied BOOLEAN DEFAULT FALSE COMMENT ''Wurde Skalierung angewendet?'' AFTER drift_corrected'
));
PREPARE alterIfNotExists FROM @preparedStatement;
EXECUTE alterIfNotExists;
DEALLOCATE PREPARE alterIfNotExists;

-- =====================================================
-- 6. OCCUPANCY_SNAPSHOT: Index für Analyse hinzufügen
-- =====================================================

-- Index für drift_corrected + ts
SET @indexname = 'idx_drift_corrected';
SET @preparedStatement = (SELECT IF(
    (
        SELECT COUNT(*) FROM INFORMATION_SCHEMA.STATISTICS
        WHERE 
            TABLE_SCHEMA = @dbname
            AND TABLE_NAME = 'occupancy_snapshot'
            AND INDEX_NAME = @indexname
    ) > 0,
    'SELECT 1',
    'CREATE INDEX idx_drift_corrected ON occupancy_snapshot(drift_corrected, ts)'
));
PREPARE alterIfNotExists FROM @preparedStatement;
EXECUTE alterIfNotExists;
DEALLOCATE PREPARE alterIfNotExists;

-- =====================================================
-- 7. VERIFIKATION: Prüfe ob Migration erfolgreich war
-- =====================================================

-- Zeige neue Tabelle
SELECT 
    'counter_state' as table_name,
    COUNT(*) as row_count 
FROM counter_state;

-- Zeige drift_config
SELECT 
    space_id,
    name,
    drift_config
FROM threshold_profile
WHERE space_id = '880e8400-e29b-41d4-a716-446655440001';

-- Zeige neue Spalten in occupancy_snapshot
SELECT 
    COLUMN_NAME,
    COLUMN_TYPE,
    COLUMN_COMMENT
FROM INFORMATION_SCHEMA.COLUMNS
WHERE TABLE_SCHEMA = DATABASE()
AND TABLE_NAME = 'occupancy_snapshot'
AND COLUMN_NAME IN ('counter_raw', 'display_count', 'drift_corrected', 'scale_applied')
ORDER BY ORDINAL_POSITION;

-- =====================================================
-- Ende der Migration
-- =====================================================

SELECT '✅ Drift-Korrektur Migration erfolgreich abgeschlossen!' as status;
