<?php
// update_count.php - Erweitert für 2 separate ESP32-Boards + Drift-Korrektur (v2.0)
header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');

// Datenbank- und Drift-Korrektur laden
require_once __DIR__ . '/api/config/Database.php';
require_once __DIR__ . '/api/includes/DriftCorrector.php';

$dataFile = 'counter_data.json';

// Space-ID für IM5 Aufenthaltsraum
$SPACE_ID = '880e8400-e29b-41d4-a716-446655440001';

// POST-Daten vom ESP32 empfangen
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    // File-Handle mit LOCK öffnen
    $lockFile = fopen($dataFile . '.lock', 'w');
    if (!$lockFile || !flock($lockFile, LOCK_EX)) {
        http_response_code(503);
        echo json_encode(['status' => 'error', 'message' => 'Could not acquire lock']);
        exit;
    }
    
    // Bestehende Daten laden (falls vorhanden)
    $existingData = [];
    if (file_exists($dataFile)) {
        $existingData = json_decode(file_get_contents($dataFile), true) ?: [];
    }
    
    // Neue Daten initialisieren
    $data = $existingData;
    $timestamp = date('Y-m-d H:i:s');
    $updated = false;
    
    // ===== DEVICE RESET: Arduino-Neustart erkannt =====
    if (isset($_POST['device_reset']) && $_POST['device_reset'] === 'true') {
        try {
            $db = Database::getInstance()->getConnection();
            
            // Prüfe aktuellen Wert VOR Reset
            $queryCheck = "SELECT counter_raw, display_count FROM counter_state WHERE space_id = :space_id";
            $stmtCheck = $db->prepare($queryCheck);
            $stmtCheck->bindParam(':space_id', $SPACE_ID);
            $stmtCheck->execute();
            $oldState = $stmtCheck->fetch(PDO::FETCH_ASSOC);
            
            // Reset durchführen
            $query = "UPDATE counter_state 
                      SET counter_raw = 0, 
                          display_count = 0,
                          last_update = NOW()
                      WHERE space_id = :space_id";
            $stmt = $db->prepare($query);
            $stmt->bindParam(':space_id', $SPACE_ID);
            $success = $stmt->execute();
            $rowsAffected = $stmt->rowCount();
            
            // Auch counter_data.json zurücksetzen
            $data['count'] = 0;
            $data['display_count'] = 0;
            $data['direction'] = 'RESET';
            $data['last_count_update'] = time();
            $data['last_update'] = time();
            $data['timestamp'] = $timestamp;
            $data['drift_corrected'] = false;
            
            // JSON-Datei aktualisieren
            file_put_contents($dataFile, json_encode($data));
            
            error_log("Arduino-Reset erkannt - Counter-State zurückgesetzt für Space: $SPACE_ID");
            error_log("  Alte Werte: counter_raw=" . ($oldState['counter_raw'] ?? 'N/A') . ", display_count=" . ($oldState['display_count'] ?? 'N/A'));
            error_log("  SQL-Update: " . ($success ? "erfolgreich" : "fehlgeschlagen") . ", Rows affected: " . $rowsAffected);
            
            // Lock freigeben
            flock($lockFile, LOCK_UN);
            fclose($lockFile);
            
            echo json_encode([
                'status' => 'success',
                'message' => 'Counter reset after device restart',
                'count' => 0,
                'display_count' => 0,
                'drift_corrected' => false
            ]);
            exit;
        } catch (Exception $e) {
            error_log("Fehler beim Arduino-Reset: " . $e->getMessage());
        }
    }
    
    // ===== BOARD 1: Lichtschranken (sendet count + direction) =====
    if (isset($_POST['count'])) {
        $counterRaw = intval($_POST['count']);
        $direction = isset($_POST['direction']) ? $_POST['direction'] : 'IDLE';
        
        $data['count'] = $counterRaw;
        $data['direction'] = $direction;
        $data['last_count_update'] = time();
        $data['timestamp'] = $timestamp;
        $updated = true;
        
        // **NEU: Drift-Korrektur in Datenbank speichern**
        try {
            $db = Database::getInstance()->getConnection();
            $driftCorrector = new DriftCorrector($db);
            
            // Config laden
            $driftConfig = $driftCorrector->getDriftConfig($SPACE_ID);
            
            // **NEU: Inkrementiere/Dekrementiere Counter basierend auf Direction**
            if ($direction === 'IN') {
                $driftCorrector->incrementCounter($SPACE_ID, 1);
            } elseif ($direction === 'OUT') {
                // OUT-Events mit Multiplikator gewichten
                $driftCorrector->decrementCounter($SPACE_ID, $driftConfig['out_event_multiplier']);
            }
            
            // Counter-State neu laden nach Update
            $counterState = $driftCorrector->getCounterState($SPACE_ID);
            $counterRaw = $counterState ? (int)$counterState['counter_raw'] : $counterRaw;
            
            // Prüfe ob Drift-Korrektur notwendig
            $needsDriftCorrection = $driftCorrector->shouldCorrectDrift($SPACE_ID, $counterRaw, $driftConfig);
            
            if ($needsDriftCorrection) {
                // Drift erkannt! Counter auf 0 setzen
                $driftCorrector->applyDriftCorrection($SPACE_ID);
                $data['drift_corrected'] = true;
                $data['count'] = 0; // Counter wurde auf 0 gesetzt
                $counterRaw = 0;
                
                error_log("Drift-Korrektur angewendet für Space $SPACE_ID (Counter war $counterRaw)");
            } else {
                $data['drift_corrected'] = false;
            }
            
            // Berechne display_count mit Skalierung
            $displayCount = $driftCorrector->computeDisplayCount($counterRaw, $driftConfig);
            $data['display_count'] = $displayCount;
            
            // Aktualisiere counter_state in DB
            $driftCorrector->updateCounterState($SPACE_ID, $counterRaw, $displayCount);
            
        } catch (Exception $e) {
            error_log("Fehler bei Drift-Korrektur: " . $e->getMessage());
            $data['drift_corrected'] = false;
            $data['display_count'] = $counterRaw;
        }
    }
    
    // ===== BOARD 2: Mikrofon (sendet nur sound_level) =====
    if (isset($_POST['sound_level'])) {
        $data['sound_level'] = intval($_POST['sound_level']);
        $data['last_sound_update'] = time();
        if (!isset($data['timestamp'])) {
            $data['timestamp'] = $timestamp;
        }
        $updated = true;
    }
    
    // ===== BOARD 3: Display Heartbeat =====
    if (isset($_POST['display_ping'])) {
        $data['last_display_update'] = time();
        if (isset($_POST['device_id'])) {
            $data['display_device_id'] = $_POST['device_id'];
        }
        if (!isset($data['timestamp'])) {
            $data['timestamp'] = $timestamp;
        }
        $updated = true;
    }
    
    // ===== BOARD 4: Raspberry Pi (Cron / Heartbeat) =====
    if (isset($_POST['raspi_ping'])) {
        $data['last_raspi_update'] = time();
        if (isset($_POST['raspi_status'])) {
            $data['raspi_status'] = $_POST['raspi_status'];
        }
        if (isset($_POST['device_id'])) {
            $data['raspi_device_id'] = $_POST['device_id'];
        }
        $data['timestamp'] = $timestamp;
        $updated = true;
    }
    
    // Globales last_update setzen
    if ($updated) {
        $data['last_update'] = time();
        
        // Daten in Datei speichern
        file_put_contents($dataFile, json_encode($data));
        
        // Lock freigeben
        flock($lockFile, LOCK_UN);
        fclose($lockFile);
        
        echo json_encode([
            'status' => 'success',
            'count' => isset($data['count']) ? $data['count'] : null,
            'display_count' => isset($data['display_count']) ? $data['display_count'] : null,
            'drift_corrected' => isset($data['drift_corrected']) ? $data['drift_corrected'] : false,
            'sound_level' => isset($data['sound_level']) ? $data['sound_level'] : null,
            'message' => 'Data merged successfully'
        ]);
        exit;
    }
    
    // Lock freigeben bei Fehler
    flock($lockFile, LOCK_UN);
    fclose($lockFile);
    
    // Falls keine relevanten POST-Daten
    http_response_code(400);
    echo json_encode([
        'status' => 'error', 
        'message' => 'No valid data received (expected: count or sound_level)'
    ]);
    exit;
}

// GET-Anfrage: Aktuelle Daten zurückgeben
if ($_SERVER['REQUEST_METHOD'] === 'GET') {
    if (file_exists($dataFile)) {
        $data = json_decode(file_get_contents($dataFile), true);
        
        // **NEU: Lade counter_state aus Datenbank falls verfügbar**
        try {
            $db = Database::getInstance()->getConnection();
            $driftCorrector = new DriftCorrector($db);
            $counterState = $driftCorrector->getCounterState($SPACE_ID);
            
            if ($counterState) {
                $data['counter_raw'] = (int)$counterState['counter_raw'];
                $data['display_count'] = (int)$counterState['display_count'];
                $data['drift_corrections_today'] = (int)$counterState['drift_corrections_today'];
            }
        } catch (Exception $e) {
            error_log("Fehler beim Laden des Counter-State: " . $e->getMessage());
        }
        
        // Berechne Sekunden seit letztem Update (global)
        $data['seconds_since_update'] = isset($data['last_update']) ? time() - $data['last_update'] : 999999;
        
        // Separate Timestamps für Count und Sound
        $data['seconds_since_count_update'] = isset($data['last_count_update']) ? time() - $data['last_count_update'] : 999999;
        $data['seconds_since_sound_update'] = isset($data['last_sound_update']) ? time() - $data['last_sound_update'] : 999999;
        $data['seconds_since_raspi_update'] = isset($data['last_raspi_update']) ? time() - $data['last_raspi_update'] : 999999;
        $data['seconds_since_display_update'] = isset($data['last_display_update']) ? time() - $data['last_display_update'] : 999999;
        
        $data['has_data'] = true;
        
        // Sound Level in dB umrechnen (30-60 dB Range - realistischer für Aufenthaltsraum)
        if (isset($data['sound_level']) && $data['sound_level'] !== null) {
            $data['sound_db'] = round(30 + ($data['sound_level'] / 100 * 30), 1);
        } else {
            $data['sound_db'] = null;
        }
        
        // Defaults setzen falls Daten fehlen
        if (!isset($data['count'])) $data['count'] = 0;
        if (!isset($data['sound_level'])) $data['sound_level'] = null;
        if (!isset($data['direction'])) $data['direction'] = 'IDLE';
        
        echo json_encode($data);
    } else {
        // Keine Daten vorhanden - Arduino war noch nie verbunden
        echo json_encode([
            'count' => 0,
            'sound_level' => null,
            'sound_db' => null,
            'direction' => 'IDLE',
            'timestamp' => '',
            'last_update' => 0,
            'seconds_since_update' => 999999,
            'seconds_since_count_update' => 999999,
            'seconds_since_sound_update' => 999999,
            'seconds_since_raspi_update' => 999999,
            'seconds_since_display_update' => 999999,
            'has_data' => false
        ]);
    }
    exit;
}

http_response_code(400);
echo json_encode(['status' => 'error', 'message' => 'Invalid request']);
?>
