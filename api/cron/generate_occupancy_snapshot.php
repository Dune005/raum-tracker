<?php
/**
 * Cron-Job: Occupancy-Snapshot-Generierung
 * 
 * Läuft alle 60 Sekunden und erstellt einen neuen occupancy_snapshot
 * basierend auf Flow-Events, Sensor-Readings und Schwellenwerten.
 * 
 * Setup:
 * - Via Server-Cron: Jede Minute ausführen
 * - Crontab: (star)/1 (star) (star) (star) (star) /usr/bin/php /pfad/zu/generate_occupancy_snapshot.php
 * - Oder manuell testen: php generate_occupancy_snapshot.php
 */

require_once __DIR__ . '/../config/Database.php';

// Timezone setzen
date_default_timezone_set($_ENV['TIMEZONE'] ?? 'Europe/Zurich');

// ========== ZEITFENSTER-KONFIGURATION ==========
// Snapshots nur in diesem Zeitfenster erstellen
$ACTIVE_START_TIME = '17:34';  // Ab 17:18 Uhr
$ACTIVE_END_TIME = '17:50';    // Bis 17:30 Uhr (nicht inklusiv)

// Aktuelle Zeit prüfen
$currentTime = date('H:i');
if ($currentTime < $ACTIVE_START_TIME || $currentTime >= $ACTIVE_END_TIME) {
    // Außerhalb des Zeitfensters - nichts tun
    exit(0);
}
// ===============================================

// Logging-Funktion
function logMessage($message) {
    $timestamp = date('Y-m-d H:i:s');
    echo "[{$timestamp}] {$message}\n";
}

try {
    $database = new Database();
    $db = $database->getConnection();
    
    // Alle Spaces holen
    $querySpaces = "SELECT id, name FROM space";
    $stmtSpaces = $db->query($querySpaces);
    $spaces = $stmtSpaces->fetchAll(PDO::FETCH_ASSOC);
    
    if (empty($spaces)) {
        logMessage("Keine aktiven Spaces gefunden");
        exit(0);
    }
    
    foreach ($spaces as $space) {
        $spaceId = $space['id'];
        $spaceName = $space['name'];
        
        logMessage("Verarbeite Space: {$spaceName} ({$spaceId})");
        
        // 1. Netto-Personenzahl aus Flow-Events (heute)
        $queryNet = "SELECT net_people FROM v_people_net_today WHERE space_id = :space_id";
        $stmtNet = $db->prepare($queryNet);
        $stmtNet->bindParam(':space_id', $spaceId);
        $stmtNet->execute();
        $netResult = $stmtNet->fetch(PDO::FETCH_ASSOC);
        $peopleEstimate = $netResult ? (int)$netResult['net_people'] : 0;
        
        // Negative Werte auf 0 setzen
        if ($peopleEstimate < 0) {
            $peopleEstimate = 0;
        }
        
        logMessage("  → Personen (Flow-basiert): {$peopleEstimate}");
        
        // 2. Durchschnittliche Lautstärke (letzte 24 Stunden - TEMPORÄR FÜR TESTS)
        $queryNoise = "SELECT AVG(r.value_num) as avg_noise
                       FROM reading r
                       JOIN sensor s ON r.sensor_id = s.id
                       JOIN device d ON s.device_id = d.id
                       WHERE d.space_id = :space_id
                       AND s.type = 'MICROPHONE'
                       AND r.ts >= DATE_SUB(NOW(), INTERVAL 24 HOUR)";
        $stmtNoise = $db->prepare($queryNoise);
        $stmtNoise->bindParam(':space_id', $spaceId);
        $stmtNoise->execute();
        $noiseResult = $stmtNoise->fetch(PDO::FETCH_ASSOC);
        $noiseDelta = $noiseResult['avg_noise'] ? round($noiseResult['avg_noise'], 1) : null;
        
        logMessage("  → Lautstärke (Ø 24h): " . ($noiseDelta ? "{$noiseDelta} dB" : "keine Daten"));
        
        // 3. Bewegungszähler (letzte 24 Stunden - TEMPORÄR FÜR TESTS)
        $queryMotion = "SELECT COUNT(*) as motion_count
                        FROM reading r
                        JOIN sensor s ON r.sensor_id = s.id
                        JOIN device d ON s.device_id = d.id
                        WHERE d.space_id = :space_id
                        AND s.type = 'PIR'
                        AND r.ts >= DATE_SUB(NOW(), INTERVAL 24 HOUR)
                        AND r.value_num > 0";
        $stmtMotion = $db->prepare($queryMotion);
        $stmtMotion->bindParam(':space_id', $spaceId);
        $stmtMotion->execute();
        $motionResult = $stmtMotion->fetch(PDO::FETCH_ASSOC);
        $motionCount = (int)$motionResult['motion_count'];
        
        logMessage("  → Bewegungen (24h): {$motionCount}");
        
        // 4. Schwellenwerte holen (JSON-Format)
        $queryThresholds = "SELECT noise_levels, motion_levels
                            FROM threshold_profile
                            WHERE space_id = :space_id
                            AND is_default = 1
                            LIMIT 1";
        $stmtThresholds = $db->prepare($queryThresholds);
        $stmtThresholds->bindParam(':space_id', $spaceId);
        $stmtThresholds->execute();
        $thresholdRow = $stmtThresholds->fetch(PDO::FETCH_ASSOC);
        
        // Standard-Schwellenwerte
        $noiseLevels = ['low' => 45, 'medium' => 60, 'high' => 75];
        $peopleLevels = ['low' => 5, 'medium' => 10];  // LOW < 5, MEDIUM 5-9, HIGH >= 10
        
        if ($thresholdRow && $thresholdRow['noise_levels']) {
            $noiseLevels = json_decode($thresholdRow['noise_levels'], true) ?: $noiseLevels;
        }
        
        logMessage("  → Schwellenwerte: Personen LOW<{$peopleLevels['low']}, MEDIUM<{$peopleLevels['medium']}, Noise LOW<{$noiseLevels['medium']}dB, HIGH>={$noiseLevels['high']}dB");
        
        // 5. Level berechnen (Primary: Personenzahl, Secondary: Lautstärke)
        $level = 'LOW';
        
        // Primär: Personenzahl
        if ($peopleEstimate >= $peopleLevels['medium']) {
            $level = 'HIGH';
        } elseif ($peopleEstimate >= $peopleLevels['low']) {
            $level = 'MEDIUM';
        } else {
            // Fallback auf Lautstärke wenn wenig Personen
            if ($noiseDelta !== null) {
                if ($noiseDelta >= $noiseLevels['high']) {
                    $level = 'HIGH';  // Sehr laut trotz wenig Personen
                } elseif ($noiseDelta >= $noiseLevels['medium']) {
                    $level = 'MEDIUM';  // Mittellaut
                }
            }
        }
        
        logMessage("  → Berechnetes Level: {$level}");
        
        // 6. Methode bestimmen
        $method = 'FLOW_ONLY';
        if ($noiseDelta !== null && $motionCount > 0) {
            $method = 'FUSION';
        } elseif ($noiseDelta !== null) {
            $method = 'NOISE_ONLY';
        }
        
        // 7. Snapshot speichern
        $queryInsert = "INSERT INTO occupancy_snapshot 
                        (space_id, ts, people_estimate, level, noise_db, motion_count, method)
                        VALUES 
                        (:space_id, NOW(), :people, :level, :noise, :motion, :method)";
        
        $stmtInsert = $db->prepare($queryInsert);
        $stmtInsert->bindParam(':space_id', $spaceId);
        $stmtInsert->bindParam(':people', $peopleEstimate);
        $stmtInsert->bindParam(':level', $level);
        $stmtInsert->bindValue(':noise', $noiseDelta, PDO::PARAM_STR);
        $stmtInsert->bindParam(':motion', $motionCount);
        $stmtInsert->bindParam(':method', $method);
        
        if ($stmtInsert->execute()) {
            $snapshotId = $db->lastInsertId();
            logMessage("  ✅ Snapshot erstellt (ID: {$snapshotId})");
        } else {
            logMessage("  ❌ Fehler beim Speichern");
        }
        
        logMessage("");
    }
    
    logMessage("Cron-Job erfolgreich abgeschlossen");
    
} catch (PDOException $e) {
    logMessage("❌ Datenbankfehler: " . $e->getMessage());
    error_log("Occupancy-Snapshot Cron Error: " . $e->getMessage());
    exit(1);
} catch (Exception $e) {
    logMessage("❌ Fehler: " . $e->getMessage());
    error_log("Occupancy-Snapshot Cron Error: " . $e->getMessage());
    exit(1);
}
