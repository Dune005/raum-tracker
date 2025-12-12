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

// Lade .env-Datei direkt (vor allen anderen Prüfungen)
$envFile = __DIR__ . '/../config/.env';
if (file_exists($envFile)) {
    $lines = file($envFile, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
    foreach ($lines as $line) {
        if (strpos(trim($line), '#') === 0) continue;
        if (strpos($line, '=') === false) continue;
        list($key, $value) = explode('=', $line, 2);
        $_ENV[trim($key)] = trim($value);
        putenv(trim($key) . '=' . trim($value));
    }
}

require_once __DIR__ . '/../config/Database.php';
require_once __DIR__ . '/../includes/DriftCorrector.php';

// ========== SICHERHEIT: TOKEN-AUTHENTIFIZIERUNG ==========
// Nur Aufrufe mit gültigem Token erlauben (für Raspberry Pi Cron)
// Token wird aus .env geladen (sicherer!)
$CRON_SECRET = $_ENV['CRON_SECRET'] ?? getenv('CRON_SECRET') ?? null;

if (!$CRON_SECRET) {
    http_response_code(500);
    error_log("CRON_SECRET nicht in .env definiert!");
    die("Configuration error\n");
}

// Prüfe ob Token vorhanden und korrekt ist
$providedToken = $_GET['token'] ?? $_SERVER['HTTP_X_CRON_TOKEN'] ?? null;

if ($providedToken !== $CRON_SECRET) {
    http_response_code(403);
    die("Forbidden: Invalid or missing token\n");
}
// =========================================================

// Timezone setzen
date_default_timezone_set($_ENV['TIMEZONE'] ?? 'Europe/Zurich');

// ========== ZEITFENSTER-KONFIGURATION ==========
// Snapshots nur in diesem Zeitfenster erstellen
$ACTIVE_START_TIME = '11:00';  // Ab 11:00 Uhr
$ACTIVE_END_TIME = '15:00';    // Bis 15:00 Uhr (nicht inklusiv)

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
        
        // **NEU: Drift-Korrektur initialisieren**
        $driftCorrector = new DriftCorrector($db);
        $driftConfig = $driftCorrector->getDriftConfig($spaceId);
        
        // 1. Hole Counter-State aus Datenbank
        $counterState = $driftCorrector->getCounterState($spaceId);
        $counterRaw = 0;
        $displayCount = 0;
        $driftCorrected = false;
        $scaleApplied = false;
        
        if ($counterState) {
            $counterRaw = (int)$counterState['counter_raw'];
            $displayCount = (int)$counterState['display_count'];
            
            logMessage("  → Counter-State: raw={$counterRaw}, display={$displayCount}");
            
            // **Drift-Check durchführen**
            if ($driftCorrector->shouldCorrectDrift($spaceId, $counterRaw, $driftConfig)) {
                logMessage("  → ⚠️ Drift erkannt! Korrektur wird angewendet...");
                $driftCorrector->applyDriftCorrection($spaceId);
                $counterRaw = 0;
                $displayCount = 0;
                $driftCorrected = true;
            } else {
                // Keine Drift-Korrektur notwendig, aber Skalierung prüfen
                $newDisplayCount = $driftCorrector->computeDisplayCount($counterRaw, $driftConfig);
                if ($newDisplayCount != $counterRaw) {
                    $scaleApplied = true;
                    logMessage("  → Skalierung angewendet: {$counterRaw} → {$newDisplayCount}");
                }
                $displayCount = $newDisplayCount;
                
                // Counter-State aktualisieren
                $driftCorrector->updateCounterState($spaceId, $counterRaw, $displayCount);
            }
        } else {
            logMessage("  → Kein Counter-State vorhanden, verwende Fallback");
        }
        
        logMessage("  → Verwende für Snapshot: people_estimate={$displayCount} (korrigierter Wert!)");
        
        // 2. Fallback auf Arduino Live-Daten (nur wenn counter_state fehlt)
        $arduinoAvailable = false;
        if (!$counterState) {
            $arduinoUrl = 'https://corner.klaus-klebband.ch/update_count.php';
            
            try {
                // cURL verwenden (statt file_get_contents, da oft auf Servern deaktiviert)
                if (function_exists('curl_init')) {
                    $ch = curl_init($arduinoUrl);
                    curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
                    curl_setopt($ch, CURLOPT_TIMEOUT, 5);
                    curl_setopt($ch, CURLOPT_SSL_VERIFYPEER, true);
                    curl_setopt($ch, CURLOPT_FOLLOWLOCATION, true);
                    
                    $arduinoResponse = curl_exec($ch);
                    $httpCode = curl_getinfo($ch, CURLINFO_HTTP_CODE);
                    $curlError = curl_error($ch);
                    curl_close($ch);
                    
                    if ($arduinoResponse && $httpCode == 200) {
                        $arduinoData = json_decode($arduinoResponse, true);
                        if ($arduinoData && isset($arduinoData['display_count']) && $arduinoData['has_data']) {
                            $counterRaw = max(0, (int)($arduinoData['count'] ?? 0));
                            $displayCount = max(0, (int)$arduinoData['display_count']);
                            $arduinoAvailable = true;
                            logMessage("  → Personen (Arduino Fallback): counter_raw={$counterRaw}, display_count={$displayCount}");
                        } else {
                            logMessage("  → Arduino: Keine Daten verfügbar (has_data=false)");
                        }
                    } else {
                        logMessage("  → Arduino: Verbindungsfehler (HTTP {$httpCode}, cURL: {$curlError})");
                    }
                } else {
                    logMessage("  → Arduino: cURL nicht verfügbar");
                }
            } catch (Exception $e) {
                logMessage("  → Arduino: Fehler beim Abrufen: " . $e->getMessage());
            }
        }
        
        // Fallback: Flow-Events nur wenn Arduino NICHT verfügbar ist UND kein counter_state
        if (!$arduinoAvailable && !$counterState) {
            $queryNet = "SELECT net_people FROM v_people_net_today WHERE space_id = :space_id";
            $stmtNet = $db->prepare($queryNet);
            $stmtNet->bindParam(':space_id', $spaceId);
            $stmtNet->execute();
            $netResult = $stmtNet->fetch(PDO::FETCH_ASSOC);
            $counterRaw = $netResult ? max(0, (int)$netResult['net_people']) : 0;
            $displayCount = $counterRaw;  // Keine Skalierung bei Fallback
            logMessage("  → Personen (Fallback Flow-Events): counter_raw={$counterRaw}, display_count={$displayCount}");
        }
        
        // 2. Aktuelle Lautstärke (letzte Messung vom Mikrofon)
        $queryNoise = "SELECT r.value_num AS latest_noise
                       FROM reading r
                       JOIN sensor s ON r.sensor_id = s.id
                       JOIN device d ON s.device_id = d.id
                       WHERE d.space_id = :space_id
                       AND s.type = 'MICROPHONE'
                       ORDER BY r.ts DESC
                       LIMIT 1";
        $stmtNoise = $db->prepare($queryNoise);
        $stmtNoise->bindParam(':space_id', $spaceId);
        $stmtNoise->execute();
        $noiseResult = $stmtNoise->fetch(PDO::FETCH_ASSOC);
        $noiseDelta = isset($noiseResult['latest_noise']) ? (float)$noiseResult['latest_noise'] : null;
        
        logMessage("  → Lautstärke (letzte Messung): " . ($noiseDelta !== null ? "{$noiseDelta} dB" : "keine Daten"));
        
        // 3. Schwellenwerte holen (JSON-Format)
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
        
        // 5. Level berechnen (basierend auf display_count = korrigierter Wert!)
        $level = 'LOW';
        
        if ($displayCount >= $peopleLevels['medium']) {
            $level = 'HIGH';
        } elseif ($displayCount >= $peopleLevels['low']) {
            $level = 'MEDIUM';
        }
        
        logMessage("  → Berechnetes Level: {$level} (basierend auf display_count={$displayCount})");
        
        // 6. Methode bestimmen (Auslastung basiert nur auf Flow)
        $method = 'FLOW_ONLY';
        
        // 7. Snapshot speichern (MIT NEUEN FELDERN!)
        // WICHTIG: people_estimate = display_count (korrigierter Wert für Dashboard!)
        $queryInsert = "INSERT INTO occupancy_snapshot 
                        (space_id, ts, people_estimate, level, noise_db, method, 
                         counter_raw, display_count, drift_corrected, scale_applied)
                        VALUES 
                        (:space_id, NOW(), :people, :level, :noise, :method,
                         :counter_raw, :display_count, :drift_corrected, :scale_applied)";
        
        $stmtInsert = $db->prepare($queryInsert);
        $stmtInsert->bindParam(':space_id', $spaceId);
        $stmtInsert->bindParam(':people', $displayCount, PDO::PARAM_INT);  // ← display_count statt peopleEstimate!
        $stmtInsert->bindParam(':level', $level);
        $stmtInsert->bindValue(':noise', $noiseDelta, PDO::PARAM_STR);
        $stmtInsert->bindParam(':method', $method);
        $stmtInsert->bindParam(':counter_raw', $counterRaw, PDO::PARAM_INT);
        $stmtInsert->bindParam(':display_count', $displayCount, PDO::PARAM_INT);
        $stmtInsert->bindValue(':drift_corrected', $driftCorrected ? 1 : 0, PDO::PARAM_INT);
        $stmtInsert->bindValue(':scale_applied', $scaleApplied ? 1 : 0, PDO::PARAM_INT);
        
        if ($stmtInsert->execute()) {
            $snapshotId = $db->lastInsertId();
            $driftStatus = $driftCorrected ? " [DRIFT-KORREKTUR!]" : ($scaleApplied ? " [Skaliert]" : "");
            logMessage("  ✅ Snapshot erstellt (ID: {$snapshotId}){$driftStatus}");
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
